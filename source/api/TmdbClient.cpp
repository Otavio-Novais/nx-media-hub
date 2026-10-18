#include "api/TmdbClient.hpp"

#include <cctype>
#include <map>
#include <mutex>

#include "api/HttpClient.hpp"
#include "core/Config.hpp"

namespace {

const std::string API = "https://api.themoviedb.org/3";

// Session caches (details and seasons do not change while the app runs)
std::mutex cacheMutex;
std::map<std::string, MediaDetails> detailsCache;
std::map<std::string, std::vector<Episode>> seasonCache;

std::string urlEncode(const std::string& value) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : value) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += (char)c;
        else out += {'%', hex[c >> 4], hex[c & 15]};
    }
    return out;
}

bool get(const std::string& path, const std::string& query, std::string& body, std::string& error) {
    const std::string& key = Config::instance().tmdbApiKey;
    if (key.empty()) {
        error = "chave da API do TMDB não configurada (tmdbApiKey)";
        return false;
    }
    HttpResponse response = HttpClient::get(API + path + "?api_key=" + key + "&language=pt-BR" + query);
    if (!response.ok()) {
        error = response.error.empty() ? "HTTP " + std::to_string(response.status) : response.error;
        return false;
    }
    body = std::move(response.body);
    return true;
}

}  // namespace

namespace TmdbClient {

bool trending(int page, ResultPage& out, std::string& error) {
    std::string body;
    if (!get("/trending/all/week", "&page=" + std::to_string(page), body, error)) return false;
    if (!parseResults(body, out)) error = "resposta inválida do TMDB";
    return error.empty();
}

bool search(const std::string& query, int page, ResultPage& out, std::string& error) {
    std::string body;
    if (!get("/search/multi", "&query=" + urlEncode(query) + "&page=" + std::to_string(page), body, error))
        return false;
    if (!parseResults(body, out)) error = "resposta inválida do TMDB";
    return error.empty();
}

bool details(MediaType type, int tmdbId, MediaDetails& out, std::string& error) {
    std::string key = (type == MediaType::Movie ? "movie/" : "tv/") + std::to_string(tmdbId);
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        if (auto it = detailsCache.find(key); it != detailsCache.end()) {
            out = it->second;
            return true;
        }
    }
    std::string body;
    if (!get("/" + key, "&append_to_response=external_ids", body, error)) return false;
    if (!parseDetails(body, type, out)) {
        error = "resposta inválida do TMDB";
        return false;
    }
    std::lock_guard<std::mutex> lock(cacheMutex);
    detailsCache[key] = out;
    return true;
}

bool season(int tmdbId, int seasonNumber, std::vector<Episode>& out, std::string& error) {
    std::string key = std::to_string(tmdbId) + "/season/" + std::to_string(seasonNumber);
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        if (auto it = seasonCache.find(key); it != seasonCache.end()) {
            out = it->second;
            return true;
        }
    }
    std::string body;
    if (!get("/tv/" + key, "", body, error)) return false;
    if (!parseSeason(body, out)) {
        error = "resposta inválida do TMDB";
        return false;
    }
    std::lock_guard<std::mutex> lock(cacheMutex);
    seasonCache[key] = out;
    return true;
}

}  // namespace TmdbClient
