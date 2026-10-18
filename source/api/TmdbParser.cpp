#include "api/TmdbClient.hpp"

#include <borealis/extern/nlohmann/json.hpp>

// Pure JSON -> model conversion (no network, no Borealis runtime): linked into the host unit tests.

using nlohmann::json;

namespace {

std::string str(const json& j, const char* key) {
    auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : "";
}

MediaItem parseItem(const json& j, MediaType type) {
    MediaItem item;
    item.tmdbId       = j.value("id", 0);
    item.type         = type;
    bool movie        = type == MediaType::Movie;
    item.title        = str(j, movie ? "title" : "name");
    item.overview     = str(j, "overview");
    item.posterPath   = str(j, "poster_path");
    item.backdropPath = str(j, "backdrop_path");
    item.year         = str(j, movie ? "release_date" : "first_air_date").substr(0, 4);
    item.rating       = j.value("vote_average", 0.0);
    return item;
}

}  // namespace

namespace TmdbClient {

std::string imageUrl(const std::string& path, ImageSize size) {
    if (path.empty()) return "";
    return std::string("https://image.tmdb.org/t/p/") + (size == ImageSize::Poster ? "w342" : "w780") + path;
}

bool parseResults(const std::string& body, ResultPage& out) {
    json j = json::parse(body, nullptr, false);
    if (j.is_discarded() || !j.contains("results") || !j["results"].is_array()) return false;
    out            = ResultPage{};
    out.page       = j.value("page", 1);
    out.totalPages = j.value("total_pages", 1);
    for (const auto& result : j["results"]) {
        // search/multi and trending/all also return people
        std::string mediaType = str(result, "media_type");
        if (mediaType == "movie") out.items.push_back(parseItem(result, MediaType::Movie));
        else if (mediaType == "tv") out.items.push_back(parseItem(result, MediaType::Series));
    }
    return true;
}

bool parseDetails(const std::string& body, MediaType type, MediaDetails& out) {
    json j = json::parse(body, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("id")) return false;
    out      = MediaDetails{};
    out.item = parseItem(j, type);
    if (j.contains("external_ids")) out.imdbId = str(j["external_ids"], "imdb_id");
    if (out.imdbId.empty()) out.imdbId = str(j, "imdb_id");
    if (j.contains("genres") && j["genres"].is_array())
        for (const auto& genre : j["genres"]) out.genres.push_back(str(genre, "name"));
    if (j.contains("runtime") && j["runtime"].is_number()) out.runtime = j["runtime"].get<int>();
    if (j.contains("seasons") && j["seasons"].is_array()) {
        for (const auto& s : j["seasons"]) {
            out.seasons.push_back(
                {s.value("season_number", 0), str(s, "name"), s.value("episode_count", 0)});
        }
    }
    return true;
}

bool parseSeason(const std::string& body, std::vector<Episode>& out) {
    json j = json::parse(body, nullptr, false);
    if (j.is_discarded() || !j.contains("episodes") || !j["episodes"].is_array()) return false;
    out.clear();
    for (const auto& e : j["episodes"]) {
        Episode episode;
        episode.season   = e.value("season_number", 0);
        episode.number   = e.value("episode_number", 0);
        episode.name     = str(e, "name");
        episode.overview = str(e, "overview");
        if (e.contains("runtime") && e["runtime"].is_number()) episode.runtime = e["runtime"].get<int>();
        out.push_back(episode);
    }
    return true;
}

}  // namespace TmdbClient
