#include "api/M3uParser.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <unordered_map>
#include <string>

namespace {

const size_t MAX_LINE = 64 * 1024;  // longest line kept across chunks: real #EXTINF lines are well under 1 KB

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

bool startsWith(std::string_view s, std::string_view prefix) { return s.substr(0, prefix.size()) == prefix; }

std::vector<std::string> splitGroups(std::string_view value) {
    std::vector<std::string> groups;
    size_t start = 0;
    while (start <= value.size()) {
        size_t end          = value.find(';', start);
        std::string_view g  = trim(value.substr(start, end == std::string_view::npos ? end : end - start));
        if (!g.empty()) groups.emplace_back(g);
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return groups;
}

struct ExtInf {
    std::map<std::string, std::string> attributes;
    std::string title;
};

// "#EXTINF:-1 key="value" key2="a, b",Title" -> attributes + title (commas inside quotes are not separators)
ExtInf parseExtInf(std::string_view line) {
    ExtInf info;
    std::string_view rest = line.substr(line.find(':') + 1);
    size_t i              = 0;
    while (i < rest.size() && rest[i] != ',') {
        if (rest[i] == '"') {
            i = rest.find('"', i + 1);
            if (i == std::string_view::npos) return info;
            i++;
            continue;
        }
        if (rest[i] == '=' && i + 1 < rest.size() && rest[i + 1] == '"') {
            size_t keyStart = rest.find_last_of(" \t", i);
            keyStart        = keyStart == std::string_view::npos ? 0 : keyStart + 1;
            size_t valueEnd = rest.find('"', i + 2);
            if (valueEnd == std::string_view::npos) return info;
            info.attributes[std::string(rest.substr(keyStart, i - keyStart))] =
                std::string(rest.substr(i + 2, valueEnd - i - 2));
            i = valueEnd + 1;
            continue;
        }
        i++;
    }
    if (i < rest.size()) info.title = std::string(trim(rest.substr(i + 1)));
    return info;
}

// Applies a header to the stream, mapping the ones libmpv exposes as dedicated options.
void applyHeader(StreamSource& stream, const std::string& key, const std::string& value) {
    std::string lower;
    for (char c : key) lower += (char)std::tolower((unsigned char)c);
    if (lower == "user-agent" || lower == "http-user-agent") stream.userAgent = value;
    else if (lower == "referer" || lower == "referrer" || lower == "http-referrer" || lower == "http-referer")
        stream.referer = value;
    else stream.headers[key] = value;
}

// "https://host/a.m3u8|User-Agent=x&Referer=y" -> URL + headers
void parseUrl(std::string_view line, StreamSource& stream) {
    size_t pipe = line.find('|');
    stream.url  = std::string(line.substr(0, pipe));
    if (pipe == std::string_view::npos) return;
    std::string_view params = line.substr(pipe + 1);
    while (!params.empty()) {
        size_t amp           = params.find('&');
        std::string_view kv  = params.substr(0, amp);
        size_t eq            = kv.find('=');
        if (eq != std::string_view::npos)
            applyHeader(stream, std::string(trim(kv.substr(0, eq))), std::string(trim(kv.substr(eq + 1))));
        if (amp == std::string_view::npos) break;
        params.remove_prefix(amp + 1);
    }
}

// Movies and series episodes, which Xtream panel exports mix with the live channels: "/movie/" or "/series/"
// in the path, or a video file (".mp4", ".mkv", ".avi")
bool isVod(const std::string& url) {
    std::string path = url.substr(0, url.find_first_of("?#"));
    for (char& c : path) c = (char)std::tolower((unsigned char)c);
    if (path.find("/movie/") != std::string::npos || path.find("/series/") != std::string::npos) return true;
    for (const char* ext : {".mp4", ".mkv", ".avi"}) {
        size_t len = std::char_traits<char>::length(ext);
        if (path.size() >= len && path.compare(path.size() - len, len, ext) == 0) return true;
    }
    return false;
}

// "Globo SP [ALT] FHD*" -> {"GLOBO", "SP", "FHD"}: uppercase words without [..]/(..) parts, symbols or emoji.
// "." is dropped inside a word ("H.265" -> "H265"); accented Latin letters are kept (and uppercased).
std::vector<std::string> nameWords(std::string_view name) {
    std::vector<std::string> words;
    std::string word;
    auto flush = [&] {
        if (!word.empty()) words.push_back(std::move(word));
        word.clear();
    };
    int depth = 0;  // inside [..] or (..)
    for (size_t i = 0; i < name.size(); i++) {
        unsigned char c = name[i];
        if (c == '[' || c == '(') {
            flush();
            depth++;
        } else if (c == ']' || c == ')') {
            if (depth > 0) depth--;
        } else if (depth > 0) {
            continue;
        } else if (std::isalnum(c) || c == '+' || c == '&') {
            word += (char)std::toupper(c);
        } else if (c == '.') {
            continue;
        } else if (c == 0xC3 && i + 1 < name.size()) {
            // Latin-1 letters (á, ç, õ...): uppercase is 0x20 lower in the second byte
            unsigned char next = name[++i];
            if (next >= 0xA0 && next <= 0xBE && next != 0xB7) next -= 0x20;
            word += (char)c;
            word += (char)next;
        } else {
            flush();
            // Other UTF-8 sequences are symbols here (emoji, "ᴴᴰ"): skip their continuation bytes
            if (c >= 0xC0)
                while (i + 1 < name.size() && ((unsigned char)name[i + 1] & 0xC0) == 0x80) i++;
        }
    }
    flush();
    return words;
}

enum class Quality { FullHd, Hd, None, UltraHd, Sd };  // the order sources are tried in

bool isTag(const std::string& word) {
    static const std::set<std::string> tags = {"4K",   "UHD",  "2160P", "FHD",  "FULLHD", "1080P", "1080",
                                               "HD",   "720P", "720",   "SD",   "480P",   "H265",  "HEVC",
                                               "H264", "60FPS", "50FPS", "ALT", "ALTERNATIVO"};
    return tags.count(word) > 0;
}

Quality qualityOf(const std::vector<std::string>& words) {
    bool fullHd = false, hd = false, sd = false;
    for (size_t i = 0; i < words.size(); i++) {
        const auto& w = words[i];
        if (w == "4K" || w == "UHD" || w == "2160P") return Quality::UltraHd;
        if (w == "FHD" || w == "FULLHD" || w == "1080P" || w == "1080" || (w == "FULL" && i + 1 < words.size() && words[i + 1] == "HD"))
            fullHd = true;
        else if (w == "HD" || w == "720P" || w == "720") hd = true;
        else if (w == "SD" || w == "480P") sd = true;
    }
    return fullHd ? Quality::FullHd : hd ? Quality::Hd : sd ? Quality::Sd : Quality::None;
}

std::string qualityLabel(Quality quality) {
    switch (quality) {
        case Quality::FullHd: return "FHD";
        case Quality::Hd: return "HD";
        case Quality::UltraHd: return "4K";
        case Quality::Sd: return "SD";
        default: return "";
    }
}

// Words that are not tags ("FULL" only counts as a tag before "HD")
std::vector<std::string> withoutTags(const std::vector<std::string>& words) {
    std::vector<std::string> kept;
    for (size_t i = 0; i < words.size(); i++) {
        if (isTag(words[i]) || (words[i] == "FULL" && i + 1 < words.size() && words[i + 1] == "HD")) continue;
        kept.push_back(words[i]);
    }
    return kept;
}

// "http://host:80/user/pass/1" -> "host:80"
std::string serverOf(const std::string& url) {
    size_t start = url.find("://");
    start        = start == std::string::npos ? 0 : start + 3;
    return url.substr(start, url.find('/', start) - start);
}

// "ESPN 4 FHD H.265" -> "ESPN 4": the original spelling, minus [..]/(..) parts and tag or symbol-only words
std::string displayName(std::string_view name) {
    std::string kept, bracketFree;
    int depth = 0;
    for (char c : name) {
        if (c == '[' || c == '(') depth++;
        else if ((c == ']' || c == ')') && depth > 0) depth--;
        else if (depth == 0) bracketFree += c;
    }
    std::vector<std::string> raw;
    size_t start = 0;
    while (start < bracketFree.size()) {
        size_t end = bracketFree.find_first_of(" \t", start);
        if (end == std::string::npos) end = bracketFree.size();
        if (end > start) raw.push_back(bracketFree.substr(start, end - start));
        start = end + 1;
    }
    for (size_t i = 0; i < raw.size(); i++) {
        auto words = nameWords(raw[i]);
        bool fullBeforeHd = words.size() == 1 && words[0] == "FULL" && i + 1 < raw.size() && nameWords(raw[i + 1]) == std::vector<std::string>{"HD"};
        if (fullBeforeHd || withoutTags(words).empty()) continue;  // "FHD", "H.265", "⚽", "FULL HD"
        if (!kept.empty()) kept += ' ';
        kept += raw[i];
    }
    return kept.empty() ? std::string(trim(name)) : kept;
}

bool isLatinLetter(unsigned char second) { return second >= 0x80 && second <= 0xBE && second != 0x97 && second != 0xB7; }
bool isLatinLower(unsigned char second) { return second >= 0xA0 && second <= 0xBE && second != 0xB7; }

bool isSeparator(char c) { return c == ' ' || c == '-' || c == '=' || c == ':' || c == '|' || c == '\t'; }

// "CANAIS: GLOBO CAPITAIS  - ✨" -> "GLOBO CAPITAIS": no emoji/symbols, no "CANAIS" / "Z - CANAIS" prefix,
// no separators at the ends, single spaces
std::string stripCategory(std::string_view raw) {
    std::string text;
    for (size_t i = 0; i < raw.size(); i++) {
        unsigned char c = raw[i];
        if (c < 0x80) {
            text += (char)c;
        } else if (c == 0xC3 && i + 1 < raw.size() && isLatinLetter(raw[i + 1])) {
            text += (char)c;
            text += raw[++i];
        } else {
            text += ' ';  // emoji and other symbols: skip the whole UTF-8 sequence
            if (c >= 0xC0)
                while (i + 1 < raw.size() && ((unsigned char)raw[i + 1] & 0xC0) == 0x80) i++;
        }
    }
    auto trimEnds = [](std::string& s) {
        size_t start = 0, end = s.size();
        while (start < end && isSeparator(s[start])) start++;
        while (end > start && isSeparator(s[end - 1])) end--;
        s = s.substr(start, end - start);
    };
    auto startsWithWord = [](const std::string& s, const char* word) {
        size_t n = std::char_traits<char>::length(word);
        if (s.size() <= n) return false;
        for (size_t i = 0; i < n; i++)
            if (std::toupper((unsigned char)s[i]) != word[i]) return false;
        return isSeparator(s[n]);
    };
    trimEnds(text);
    if (startsWithWord(text, "Z")) {
        text.erase(0, 1);
        trimEnds(text);
    }
    if (startsWithWord(text, "CANAIS")) {
        text.erase(0, 6);
        trimEnds(text);
    }
    std::string single;
    for (char c : text)
        if (c != ' ' || (!single.empty() && single.back() != ' ')) single += c;
    return single;
}

// Case- and accent-insensitive identity of a stripped category ("Documentários" == "DOCUMENTARIOS")
std::string categoryKey(const std::string& name) {
    // Latin-1 letters by second UTF-8 byte (0x80-0xBF), uppercase and lowercase rows share a base letter
    static const char* base = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
    std::string key;
    for (size_t i = 0; i < name.size(); i++) {
        unsigned char c = name[i];
        if (c == 0xC3 && i + 1 < name.size()) key += (char)std::toupper((unsigned char)base[(unsigned char)name[++i] - 0x80]);
        else key += (char)std::toupper(c);
    }
    return key;
}

// An all-caps name reads better capitalized: "AREA DO CLIENTE" -> "Area do Cliente". Words of up to 4
// letters stay (ESPN, HBO, SBT) except Portuguese connectives; words with digits stay ("24HRS")
std::string readableCategory(const std::string& name) {
    bool hasLower = false;
    for (size_t i = 0; i < name.size(); i++) {
        unsigned char c = name[i];
        if (std::islower(c) || (c == 0xC3 && i + 1 < name.size() && isLatinLower(name[i + 1]))) hasLower = true;
    }
    if (hasLower) return name;

    static const std::set<std::string> connectives = {"DE", "DO", "DA", "DOS", "DAS", "E", "EM"};
    std::string result;
    size_t start = 0;
    while (start <= name.size()) {
        size_t end       = name.find(' ', start);
        std::string word = name.substr(start, end == std::string::npos ? std::string::npos : end - start);
        size_t letters = 0;
        bool digits    = false;
        for (size_t i = 0; i < word.size(); i++) {
            unsigned char c = word[i];
            if (std::isdigit(c)) digits = true;
            if (std::isalpha(c)) letters++;
            if (c == 0xC3) letters++, i++;
        }
        if (!result.empty() && connectives.count(word)) {
            for (char& c : word) c = (char)std::tolower((unsigned char)c);
        } else if (letters > 4 && !digits) {
            for (size_t i = 1; i < word.size(); i++) {
                unsigned char c = word[i];
                if (c == 0xC3 && i + 1 < word.size()) {
                    unsigned char& next = (unsigned char&)word[++i];
                    if (next >= 0x80 && next <= 0x9E && next != 0x97) next += 0x20;
                } else {
                    word[i] = (char)std::tolower(c);
                }
            }
        }
        if (!result.empty()) result += ' ';
        result += word;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return result;
}

// Accents first, then a panel's own mixed case, then our capitalization of an all-caps name:
// "Documentários" (from "DOCUMENTÁRIOS") > "Documentarios"; "SporTV" > "Sportv" (from "SPORTV") > "BAND"
int readability(const std::string& name, bool capitalized) {
    bool lower = false, accent = false;
    for (size_t i = 0; i < name.size(); i++) {
        unsigned char c = name[i];
        if (std::islower(c)) lower = true;
        if (c == 0xC3) accent = true;
    }
    return (accent ? 4 : 0) + (lower ? (capitalized ? 1 : 2) : 0);
}

}  // namespace

namespace M3uParser {

std::vector<Channel> parse(std::string_view content) {
    Parser parser;
    parser.feed(content);
    return parser.finish();
}

void Parser::feed(std::string_view chunk) {
    while (!chunk.empty() && !full()) {
        size_t end = chunk.find('\n');
        if (end == std::string_view::npos) {
            // A line this long is not a playlist line (binary or HTML garbage): drop it instead of buffering it
            if (partial.size() + chunk.size() > MAX_LINE) partial.clear();
            else partial.append(chunk);
            return;
        }
        if (partial.empty()) {
            parseLine(chunk.substr(0, end));
        } else {
            if (partial.size() + end <= MAX_LINE) {
                partial.append(chunk.substr(0, end));
                parseLine(partial);
            }
            partial.clear();
        }
        chunk.remove_prefix(end + 1);
    }
}

std::vector<Channel> Parser::finish() {
    if (!partial.empty() && !full()) parseLine(partial);
    partial.clear();
    return std::move(channels);
}

void Parser::parseLine(std::string_view line) {
    if (firstLine) {
        firstLine = false;
        if (startsWith(line, "\xEF\xBB\xBF")) line.remove_prefix(3);
    }
    line = trim(line);
    if (line.empty() || full()) return;

    if (startsWith(line, "#EXTINF:")) {
        ExtInf info    = parseExtInf(line);
        pending        = Channel{};
        hasInfo        = true;
        pending.name   = !info.title.empty() ? info.title : info.attributes["tvg-name"];
        pending.logo   = info.attributes["tvg-logo"];
        pending.tvgId  = info.attributes["tvg-id"];
        pending.groups = splitGroups(info.attributes["group-title"]);
    } else if (startsWith(line, "#EXTGRP:")) {
        if (pending.groups.empty()) pending.groups = splitGroups(line.substr(8));
    } else if (startsWith(line, "#EXTVLCOPT:")) {
        std::string_view opt = line.substr(11);
        size_t eq            = opt.find('=');
        if (eq != std::string_view::npos)
            applyHeader(pending.stream, std::string(opt.substr(0, eq)), std::string(opt.substr(eq + 1)));
    } else if (line[0] != '#' && line.find("://") != std::string_view::npos) {
        parseUrl(line, pending.stream);
        if (isVod(pending.stream.url)) {
            pending = Channel{};
            hasInfo = false;
            return;
        }
        if (!hasInfo || pending.name.empty()) pending.name = pending.stream.url;
        pending.stream.title  = pending.name;
        pending.stream.isLive = true;
        channels.push_back(std::move(pending));
        pending = Channel{};
        hasInfo = false;
    }
}

std::vector<Channel> dedupe(std::vector<Channel> channels) {
    std::vector<Channel> unique;
    std::unordered_map<std::string, size_t> byUrl;
    for (auto& channel : channels) {
        auto [it, inserted] = byUrl.try_emplace(channel.stream.url, unique.size());
        if (inserted) {
            unique.push_back(std::move(channel));
            continue;
        }
        Channel& kept = unique[it->second];
        for (auto& group : channel.groups)
            if (std::find(kept.groups.begin(), kept.groups.end(), group) == kept.groups.end())
                kept.groups.push_back(group);
        if (kept.logo.empty()) kept.logo = channel.logo;
    }
    return unique;
}

std::string channelGroupKey(std::string_view name) {
    std::string key;
    for (const auto& word : withoutTags(nameWords(name))) {
        if (!key.empty()) key += ' ';
        key += word;
    }
    return key;
}

std::vector<Channel> groupSources(std::vector<Channel> channels) {
    struct Entry {
        Quality quality;
        int turn;  // how many earlier entries of this quality came from the same server
        Channel channel;
    };
    std::vector<std::vector<Entry>> groups;
    std::unordered_map<std::string, size_t> byKey;
    for (auto& channel : channels) {
        auto words         = nameWords(channel.name);
        std::string key    = channelGroupKey(channel.name);
        Quality quality    = qualityOf(words);
        // A name made only of tags says nothing about the channel: never merge it
        if (key.empty()) key = "\n" + channel.stream.url;
        auto [it, inserted] = byKey.try_emplace(key, groups.size());
        if (inserted) groups.emplace_back();
        groups[it->second].push_back({quality, 0, std::move(channel)});
    }

    std::vector<Channel> grouped;
    grouped.reserve(groups.size());
    for (auto& entries : groups) {
        std::string name = displayName(entries.front().channel.name);
        std::string logo;
        std::vector<std::string> categories;
        for (const auto& entry : entries) {
            if (logo.empty()) logo = entry.channel.logo;
            for (const auto& group : entry.channel.groups)
                if (std::find(categories.begin(), categories.end(), group) == categories.end()) categories.push_back(group);
        }
        // Within a quality, alternate between servers: one that is down costs a single try, not one per source
        std::map<std::pair<Quality, std::string>, int> perServer;
        for (auto& entry : entries) entry.turn = perServer[{entry.quality, serverOf(entry.channel.stream.url)}]++;
        std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
            return a.quality != b.quality ? a.quality < b.quality : a.turn < b.turn;
        });
        for (auto& entry : entries) {
            entry.channel.stream.title   = name;
            entry.channel.stream.quality = qualityLabel(entry.quality);
        }

        Channel channel = std::move(entries.front().channel);
        channel.name    = name;
        channel.logo    = logo;
        channel.groups  = std::move(categories);
        for (size_t i = 1; i < entries.size(); i++) channel.alternates.push_back(std::move(entries[i].channel.stream));
        grouped.push_back(std::move(channel));
    }
    return grouped;
}

std::vector<Channel> tidyCategories(std::vector<Channel> channels) {
    // First pass: the best spelling of every category; second pass: rename and drop repeats
    std::unordered_map<std::string, std::pair<std::string, int>> shown;  // key -> best spelling, its score
    std::unordered_map<std::string, std::string> keyOf;
    for (const auto& channel : channels) {
        for (const auto& group : channel.groups) {
            if (keyOf.count(group)) continue;
            std::string stripped = stripCategory(group);
            if (stripped.empty()) stripped = std::string(trim(group));
            std::string name = readableCategory(stripped);
            std::string key  = categoryKey(stripped);
            int score        = readability(name, name != stripped);
            keyOf[group]     = key;
            auto it          = shown.find(key);
            if (it == shown.end()) shown[key] = {name, score};
            else if (score > it->second.second) it->second = {name, score};
        }
    }
    for (auto& channel : channels) {
        std::vector<std::string> groups;
        for (const auto& group : channel.groups) {
            const std::string& name = shown[keyOf[group]].first;
            if (std::find(groups.begin(), groups.end(), name) == groups.end()) groups.push_back(name);
        }
        channel.groups = std::move(groups);
    }
    return channels;
}

std::vector<Channel> filterByCountry(std::vector<Channel> channels, const std::vector<std::string>& countries) {
    if (countries.empty()) return channels;
    std::set<std::string> allowed;
    for (const auto& c : countries) {
        std::string lower;
        for (char ch : c) lower += (char)std::tolower((unsigned char)ch);
        allowed.insert(lower);
    }
    // Regional feed names seen in iptv-org ids ("Channel.us@Poland"): country -> code, multi-country region -> ""
    static const std::map<std::string, std::string> regions = {
        {"sweden", "se"}, {"germany", "de"}, {"norway", "no"}, {"denmark", "dk"}, {"finland", "fi"},
        {"poland", "pl"}, {"romania", "ro"}, {"hungary", "hu"}, {"czechrepublic", "cz"}, {"netherlands", "nl"},
        {"france", "fr"}, {"italy", "it"}, {"spain", "es"}, {"turkey", "tr"}, {"russia", "ru"}, {"india", "in"},
        {"vietnam", "vn"}, {"arabia", "sa"}, {"latinamerica", ""}, {"europe", ""}, {"asia", ""}, {"adria", ""},
        {"middleeast", ""}, {"nordic", ""}, {"balkans", ""}, {"cee", ""}, {"mena", ""}, {"africa", ""}};

    std::vector<Channel> kept;
    for (auto& channel : channels) {
        // "Name.cc" or "Name.cc@Feed"
        const std::string& id = channel.tvgId;
        size_t at             = id.find('@');
        std::string base      = id.substr(0, at);
        size_t dot            = base.rfind('.');
        if (dot == std::string::npos || base.size() - dot != 3) {
            kept.push_back(std::move(channel));  // not an iptv-org style id: no country information
            continue;
        }
        std::string country = base.substr(dot + 1);
        if (!allowed.count(country)) continue;

        std::string feed = at == std::string::npos ? "" : id.substr(at + 1);
        std::string lowerFeed;
        for (char ch : feed) lowerFeed += (char)std::tolower((unsigned char)ch);
        if (lowerFeed.size() > 2 && lowerFeed.compare(lowerFeed.size() - 2, 2, "hd") == 0)
            lowerFeed.resize(lowerFeed.size() - 2);
        bool countryCodeFeed = feed.size() == 2 && std::isupper((unsigned char)feed[0]) &&
                               std::isupper((unsigned char)feed[1]) && feed != "HD" && feed != "SD";
        if (countryCodeFeed && !allowed.count(lowerFeed)) continue;  // "@DK", "@NO"...
        auto region = regions.find(lowerFeed);
        if (region != regions.end() && !allowed.count(region->second)) continue;  // "@Poland", "@Europe"...
        kept.push_back(std::move(channel));
    }
    return kept;
}

}  // namespace M3uParser
