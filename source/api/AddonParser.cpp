#include <borealis/extern/nlohmann/json.hpp>
#include <cctype>

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
        source.title = name.empty() ? description : description.empty() ? name : name + " - " + description;
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

}  // namespace AddonClient
