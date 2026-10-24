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


std::string lowerAscii(std::string_view s) {
    std::string lower(s);
    for (char& c : lower) c = (char)std::tolower((unsigned char)c);
    return lower;
}

// "720p" / "1080i" -> 720 / 1080, anything else -> 0
int parseHeight(std::string_view word) {
    char last = word.empty() ? 0 : (char)std::tolower((unsigned char)word.back());
    if (word.size() < 4 || word.size() > 5 || (last != 'p' && last != 'i')) return 0;
    int height = 0;
    for (char c : word.substr(0, word.size() - 1)) {
        if (c < '0' || c > '9') return 0;
        height = height * 10 + (c - '0');
    }
    return height;
}

// A quality mark: true when `word` is one; height stays 0 for marks without a resolution (codecs, frame rates,
// backup markers like "ALT2" that lists use for a second link of the same channel)
bool qualityWord(std::string_view word, int& height) {
    std::string w = lowerAscii(word);
    height        = parseHeight(w);
    if (height) return true;
    if (w == "4k" || w == "uhd" || w == "2160") height = 2160;
    else if (w == "fhd" || w == "fullhd" || w == "1080") height = 1080;
    else if (w == "hd" || w == "hd+" || w == "720") height = 720;
    else if (w == "sd") height = 480;
    else if (w == "h265" || w == "h.265" || w == "hevc" || w == "h264" || w == "h.264" || w == "x265" ||
             w == "x264" || w == "avc" || w == "25fps" || w == "30fps" || w == "50fps" || w == "60fps" ||
             w == "backup" || w == "bkp" || w == "alt" || w == "reserva")
        return true;
    else if (w.size() == 4 && w.compare(0, 3, "alt") == 0 && w[3] >= '1' && w[3] <= '9') return true;  // ALT2
    else return false;
    return true;
}

// "FHD/H265", "HD H.264", "720p" inside (...) or [...]: true when every word is a quality mark
bool qualityGroup(std::string_view inner, int& height) {
    height     = 0;
    bool any   = false;
    size_t pos = 0;
    while (pos < inner.size()) {
        size_t end = inner.find_first_of(" \t/|,+-", pos);
        if (end == std::string_view::npos) end = inner.size();
        std::string_view word = inner.substr(pos, end - pos);
        // "HD+" keeps its "+": the separator search cut it off
        if (end < inner.size() && inner[end] == '+' && (end + 1 == inner.size() || inner[end + 1] == ' ')) {
            word = inner.substr(pos, end - pos + 1);
            end++;
        }
        if (!word.empty()) {
            int h = 0;
            if (!qualityWord(word, h)) return false;
            any = true;
            // explicit "720p" wins over a word, otherwise the first resolution found
            if (h && (!height || parseHeight(word))) height = h;
        }
        pos = end + 1;
    }
    return any;
}

// Superscript digits used as backup markers ("Canal HD ¹"), UTF-8: ¹ ² ³ are C2 B9/B2/B3, ⁰ ⁴-⁹ are E2 81 B0-B9
bool stripSuperscript(std::string_view& s) {
    size_t n = s.size();
    if (n >= 2 && (unsigned char)s[n - 2] == 0xC2 &&
        ((unsigned char)s[n - 1] == 0xB9 || (unsigned char)s[n - 1] == 0xB2 || (unsigned char)s[n - 1] == 0xB3)) {
        s.remove_suffix(2);
        return true;
    }
    if (n >= 3 && (unsigned char)s[n - 3] == 0xE2 && (unsigned char)s[n - 2] == 0x81 &&
        (unsigned char)s[n - 1] >= 0xB0 && (unsigned char)s[n - 1] <= 0xB9) {
        s.remove_suffix(3);
        return true;
    }
    return false;
}

// Separators left between the name and a removed mark: "Canal | HD", "Canal - FHD", "Canal-HD"
std::string_view trimSeparators(std::string_view s) {
    s = trim(s);
    while (!s.empty() && (s.back() == '|' || s.back() == '-' || s.back() == ':' || s.back() == '/' ||
                          s.back() == ',' || s.back() == '.' || s.back() == '_')) {
        s.remove_suffix(1);
        s = trim(s);
    }
    // en dash "–" (E2 80 93) and bullet "•" (E2 80 A2)
    while (s.size() >= 3 && (unsigned char)s[s.size() - 3] == 0xE2 && (unsigned char)s[s.size() - 2] == 0x80 &&
           ((unsigned char)s[s.size() - 1] == 0x93 || (unsigned char)s[s.size() - 1] == 0xA2)) {
        s.remove_suffix(3);
        s = trim(s);
    }
    return s;
}

// Sort key of a version, higher is better (see groupVersions)
int qualityRank(int height) {
    if (height == 0) return 600;    // unknown: below 720p, above SD
    if (height > 1080) return 1000;  // 4K: right below 1080p
    return height;
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

NameQuality splitQuality(std::string_view name) {
    NameQuality result;
    std::string_view s = trim(name);
    bool wordHeight    = false;  // a resolution word was removed: a second one is part of the name ("FunBox UHD SD")
    auto apply         = [&result](int height) {
        if (!result.height) result.height = height;  // parsed from the end: "(576p)" comes before "HD"
    };
    // Every cut must leave a name: a channel is never only a quality mark
    auto cut = [&s](std::string_view rest) {
        rest = trimSeparators(rest);
        if (rest.empty()) return false;
        s = rest;
        return true;
    };
    while (!s.empty()) {
        std::string_view before = s;
        if (stripSuperscript(before)) {
            if (!cut(before)) break;
            continue;
        }
        if (s.back() == ']' || s.back() == ')') {
            size_t open = s.find_last_of(s.back() == ']' ? '[' : '(');
            if (open == std::string_view::npos) break;
            std::string_view inner = trim(s.substr(open + 1, s.size() - open - 2));
            int height             = 0;
            bool quality           = qualityGroup(inner, height);
            // "(United States)" is part of the name; "[Not 24/7]" and other [...] tags are not
            if (s.back() == ')' && !quality) break;
            if (!cut(s.substr(0, open))) break;
            if (quality) apply(height);
            continue;
        }
        size_t sep = s.find_last_of(" \t|-");
        if (sep == std::string_view::npos) break;
        std::string_view word = s.substr(sep + 1);
        int height            = 0;
        if (!qualityWord(word, height)) break;
        bool resolutionWord = height && !parseHeight(word);
        if (resolutionWord && wordHeight) break;
        if (!cut(s.substr(0, sep))) break;
        wordHeight |= resolutionWord;
        // "FULL HD"
        if (height == 720 && s.size() > 5 && lowerAscii(s.substr(s.size() - 5)) == " full") {
            height = 1080;
            if (!cut(s.substr(0, s.size() - 5))) break;
        }
        apply(height);
    }
    result.base = std::string(s);
    return result;
}

std::vector<Channel> groupVersions(std::vector<Channel> channels) {
    struct Group {
        Channel channel;  // stream = first version seen, alternatives = the others
        std::vector<int> heights;
        std::string idBase;  // tvg-id without "@Feed": two different ones are two different channels
    };
    std::vector<Group> groups;
    std::unordered_map<std::string, std::vector<size_t>> byName;
    for (auto& channel : channels) {
        NameQuality quality = splitQuality(channel.name);
        if (quality.base.empty()) quality.base = channel.name;
        std::string idBase = channel.tvgId.substr(0, channel.tvgId.find('@'));
        auto& candidates   = byName[lowerAscii(quality.base)];
        Group* group       = nullptr;
        for (size_t index : candidates) {
            Group& g = groups[index];
            if (g.idBase.empty() || idBase.empty() || g.idBase == idBase) {
                group = &g;
                break;
            }
        }
        if (!group) {
            candidates.push_back(groups.size());
            groups.push_back({std::move(channel), {quality.height}, idBase});
            groups.back().channel.name = quality.base;
            continue;
        }
        if (group->idBase.empty()) group->idBase = idBase;
        Channel& kept = group->channel;
        for (auto& g : channel.groups)
            if (std::find(kept.groups.begin(), kept.groups.end(), g) == kept.groups.end()) kept.groups.push_back(g);
        if (kept.logo.empty()) kept.logo = channel.logo;
        if (kept.tvgId.empty()) kept.tvgId = channel.tvgId;
        kept.alternatives.push_back(std::move(channel.stream));
        group->heights.push_back(quality.height);
    }

    std::vector<Channel> result;
    result.reserve(groups.size());
    for (auto& group : groups) {
        Channel& channel = group.channel;
        if (!channel.alternatives.empty()) {
            std::vector<size_t> order(group.heights.size());
            for (size_t i = 0; i < order.size(); i++) order[i] = i;
            std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
                return qualityRank(group.heights[a]) > qualityRank(group.heights[b]);
            });
            std::vector<StreamSource> versions = channel.versions();
            channel.stream                     = std::move(versions[order[0]]);
            channel.alternatives.clear();
            for (size_t i = 1; i < order.size(); i++) channel.alternatives.push_back(std::move(versions[order[i]]));
        }
        result.push_back(std::move(channel));
    }
    return result;
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
