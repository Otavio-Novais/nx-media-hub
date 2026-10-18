#include "api/M3uParser.hpp"

#include <cctype>
#include <map>
#include <string>

namespace {

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

}  // namespace

namespace M3uParser {

std::vector<Channel> parse(std::string_view content) {
    std::vector<Channel> channels;
    if (startsWith(content, "\xEF\xBB\xBF")) content.remove_prefix(3);

    Channel pending;  // filled by #EXTINF / #EXTGRP / #EXTVLCOPT until its URL line
    bool hasInfo = false;

    size_t pos = 0;
    while (pos < content.size()) {
        size_t end            = content.find('\n', pos);
        std::string_view line = trim(content.substr(pos, end == std::string_view::npos ? end : end - pos));
        pos                   = end == std::string_view::npos ? content.size() : end + 1;
        if (line.empty()) continue;

        if (startsWith(line, "#EXTINF:")) {
            ExtInf info  = parseExtInf(line);
            pending      = Channel{};
            hasInfo      = true;
            pending.name = !info.title.empty() ? info.title : info.attributes["tvg-name"];
            pending.logo = info.attributes["tvg-logo"];
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
    return channels;
}

}  // namespace M3uParser
