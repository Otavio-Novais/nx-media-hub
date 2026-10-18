#pragma once

#include <string>
#include <vector>

#include "models/MediaItem.hpp"

namespace TmdbClient {

enum class ImageSize { Poster, Backdrop };

// One page of a TMDB list (20 items per page)
struct ResultPage {
    std::vector<MediaItem> items;
    int page       = 1;
    int totalPages = 1;
};

// Full URL of a TMDB image path ("" when the path is empty)
std::string imageUrl(const std::string& path, ImageSize size);

// Blocking calls (wrap them in brls::async). Always in pt-BR. Return false and set `error` on failure.
bool trending(int page, ResultPage& out, std::string& error);
bool search(const std::string& query, int page, ResultPage& out, std::string& error);
// Details and seasons are cached for the session (they do not change while the app runs)
bool details(MediaType type, int tmdbId, MediaDetails& out, std::string& error);
bool season(int tmdbId, int seasonNumber, std::vector<Episode>& out, std::string& error);

// JSON -> models (pure, unit tested on the host)
bool parseResults(const std::string& json, ResultPage& out);
bool parseDetails(const std::string& json, MediaType type, MediaDetails& out);
bool parseSeason(const std::string& json, std::vector<Episode>& out);

}  // namespace TmdbClient
