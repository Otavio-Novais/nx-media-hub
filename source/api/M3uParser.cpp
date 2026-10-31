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
