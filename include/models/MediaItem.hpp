#pragma once

#include <string>
#include <vector>

#include "models/StreamSource.hpp"

// A live TV channel or event from an M3U list.
struct Channel {
    std::string name;
    std::string logo;
    std::string tvgId;                // iptv-org style "Channel.<country>@<feed>", ex: "AuctionHunters.se@NO"
    std::vector<std::string> groups;  // from group-title (";"-separated) or #EXTGRP
    StreamSource stream;              // isLive = true; the best source when the channel groups several
    std::vector<StreamSource> alternates;  // the channel's other sources, best first (M3uParser::groupSources)

    // Every source in the order the player tries them
    std::vector<StreamSource> sources() const {
        std::vector<StreamSource> all{stream};
        all.insert(all.end(), alternates.begin(), alternates.end());
        return all;
    }
};

enum class MediaType { Movie, Series };

// A movie or series as listed by TMDB (search, trending).
struct MediaItem {
    int tmdbId     = 0;
    MediaType type = MediaType::Movie;
    std::string title;
    std::string overview;
    std::string posterPath;    // TMDB image path, ex: "/abc.jpg"
    std::string backdropPath;
    std::string year;
    double rating = 0;
};

struct Season {
    int number = 0;
    std::string name;
    int episodeCount = 0;
};

struct Episode {
    int season = 0;
    int number = 0;
    std::string name;
    std::string overview;
    int runtime = 0;  // minutes
};

// Full details of a movie or series (TMDB details endpoint).
struct MediaDetails {
    MediaItem item;
    std::string imdbId;  // ex: "tt0133093": key for the Stremio addons
    std::vector<std::string> genres;
    int runtime = 0;            // movies, minutes
    std::vector<Season> seasons;  // series
};
