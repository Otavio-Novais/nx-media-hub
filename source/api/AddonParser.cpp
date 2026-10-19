#include <borealis/extern/nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <cstdint>

#include "api/AddonClient.hpp"

// Pure parts of the addon client (no network, no Borealis runtime): linked into the host unit tests.

using nlohmann::json;

namespace {

std::string str(const json& j, const char* key) {
    auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : "";
}

// Addons put quality/language on several lines ("1080p\nDublado"): keep it on one line for the picker
std::string singleLine(std::string text) {
    for (char& c : text)
        if (c == '\n' || c == '\r') c = ' ';
    return text;
}

bool startsWith(const std::string& s, const std::string& prefix) { return s.rfind(prefix, 0) == 0; }

bool contains(const std::string& haystack, const char* needle) { return haystack.find(needle) != std::string::npos; }

// Decodes the UTF-8 sequence at `i` (advancing it); invalid bytes decode as themselves
uint32_t nextCodePoint(const std::string& s, size_t& i) {
    auto c   = (unsigned char)s[i];
    int len  = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
    if (i + len > s.size()) len = 1;
    uint32_t cp = len == 1 ? c : len == 2 ? (c & 0x1F) : len == 3 ? (c & 0x0F) : (c & 0x07);
    for (int k = 1; k < len; k++) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
    i += len;
    return cp;
}

int streamScore(const StreamSource& stream) {
    std::string t;
    for (char c : stream.title) t += (char)std::tolower((unsigned char)c);
    int score = 0;
    // Debrid cache markers used by Torrentio-like addons: "[RD+]" cached, "[RD download]" not yet
    if (contains(t, "+]")) score += 1000;
    if (contains(t, " download]")) score -= 1000;
    if (contains(t, "dual") || contains(t, "dublado") || contains(t, "pt-br") || contains(t, "ptbr") ||
        contains(t, "nacional") || contains(t, "portugu"))
        score += 100;
    if (contains(t, "2160p") || contains(t, "4k")) score -= 20;
    else if (contains(t, "1080p")) score += 30;
    else if (contains(t, "720p")) score += 20;
    return score;
}

}  // namespace

namespace AddonClient {

std::string normalizeBaseUrl(const std::string& input) {
    std::string url = input;
    while (!url.empty() && (url.back() == ' ' || url.back() == '/')) url.pop_back();
    if (startsWith(url, "stremio://")) url = "https://" + url.substr(10);
    const std::string manifest = "/manifest.json";
    if (url.size() >= manifest.size() && url.compare(url.size() - manifest.size(), manifest.size(), manifest) == 0)
        url.erase(url.size() - manifest.size());
    return url;
}

std::string streamPath(MediaType type, const std::string& imdbId, int season, int episode) {
    if (type == MediaType::Movie) return "movie/" + imdbId;
    return "series/" + imdbId + ":" + std::to_string(season) + ":" + std::to_string(episode);
}

bool parseStreams(const std::string& body, std::vector<StreamSource>& out) {
    json j = json::parse(body, nullptr, false);
    if (j.is_discarded() || !j.contains("streams") || !j["streams"].is_array()) return false;
    out.clear();
    for (const auto& s : j["streams"]) {
        std::string url = str(s, "url");
        if (!startsWith(url, "http://") && !startsWith(url, "https://")) continue;

        StreamSource source;
        source.url             = url;
        std::string name        = singleLine(str(s, "name"));
        std::string description = singleLine(!str(s, "description").empty() ? str(s, "description") : str(s, "title"));
        source.title = cleanLabel(name.empty() ? description : description.empty() ? name : name + " - " + description);
        if (source.title.empty()) source.title = url;

        if (s.contains("behaviorHints") && s["behaviorHints"].contains("proxyHeaders")) {
            const auto& headers = s["behaviorHints"]["proxyHeaders"];
            if (headers.contains("request") && headers["request"].is_object()) {
                for (const auto& [key, value] : headers["request"].items()) {
                    if (!value.is_string()) continue;
                    std::string lower;
                    for (char c : key) lower += (char)std::tolower((unsigned char)c);
                    if (lower == "user-agent") source.userAgent = value;
                    else if (lower == "referer" || lower == "referrer") source.referer = value;
                    else source.headers[key] = value;
                }
            }
        }

        if (s.contains("subtitles") && s["subtitles"].is_array()) {
            for (const auto& sub : s["subtitles"]) {
                std::string subUrl = str(sub, "url");
                if (!subUrl.empty()) source.subtitles.push_back({str(sub, "id"), str(sub, "lang"), subUrl});
            }
        }
        out.push_back(std::move(source));
    }
    return true;
}

std::string cleanLabel(const std::string& text) {
    std::string out;
    for (size_t i = 0; i < text.size();) {
        size_t start = i;
        uint32_t cp  = nextCodePoint(text, i);
        if (cp == 0x1F464) out += " Seeds ";                       // 👤
        else if (cp == 0x1F4BE || cp == 0x2699) out += " · ";      // 💾 ⚙
        else if (cp >= 0x10000 || (cp >= 0x2600 && cp <= 0x27BF) || (cp >= 0x2B00 && cp <= 0x2BFF) ||
                 (cp >= 0xFE00 && cp <= 0xFE0F) || cp == 0x200D)
            out += ' ';                                              // other emojis, flags, joiners
        else out.append(text, start, i - start);
    }
    // Collapse whitespace
    std::string collapsed;
    for (char c : out) {
        bool space = c == ' ' || c == '\t';
        if (space && (collapsed.empty() || collapsed.back() == ' ')) continue;
        collapsed += space ? ' ' : c;
    }
    while (!collapsed.empty() && collapsed.back() == ' ') collapsed.pop_back();
    return collapsed;
}

void rankStreams(std::vector<StreamSource>& streams) {
    std::stable_sort(streams.begin(), streams.end(),
                     [](const StreamSource& a, const StreamSource& b) { return streamScore(a) > streamScore(b); });
}

}  // namespace AddonClient
