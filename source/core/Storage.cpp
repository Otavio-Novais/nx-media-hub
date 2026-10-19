#include "core/Storage.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>

using nlohmann::json;

void to_json(json& j, const MediaItem& item) {
    j = {{"tmdbId", item.tmdbId},
         {"type", item.type == MediaType::Movie ? "movie" : "series"},
         {"title", item.title},
         {"posterPath", item.posterPath},
         {"backdropPath", item.backdropPath},
         {"year", item.year},
         {"rating", item.rating}};
}

void from_json(const json& j, MediaItem& item) {
    item.tmdbId       = j.value("tmdbId", 0);
    item.type         = j.value("type", "movie") == "series" ? MediaType::Series : MediaType::Movie;
    item.title        = j.value("title", "");
    item.posterPath   = j.value("posterPath", "");
    item.backdropPath = j.value("backdropPath", "");
    item.year         = j.value("year", "");
    item.rating       = j.value("rating", 0.0);
}

void to_json(json& j, const StreamSource& source) {
    j = {{"title", source.title},         {"url", source.url},       {"userAgent", source.userAgent},
         {"referer", source.referer},     {"headers", source.headers}, {"isLive", source.isLive}};
}

void from_json(const json& j, StreamSource& source) {
    source.title     = j.value("title", "");
    source.url       = j.value("url", "");
    source.userAgent = j.value("userAgent", "");
    source.referer   = j.value("referer", "");
    source.headers   = j.value("headers", std::map<std::string, std::string>{});
    source.isLive    = j.value("isLive", false);
}

void to_json(json& j, const Channel& channel) {
    j = {{"name", channel.name}, {"logo", channel.logo}, {"groups", channel.groups}, {"stream", channel.stream}};
}

void from_json(const json& j, Channel& channel) {
    channel.name   = j.value("name", "");
    channel.logo   = j.value("logo", "");
    channel.groups = j.value("groups", std::vector<std::string>{});
    channel.stream = j.value("stream", StreamSource{});
}

namespace Storage {

void recoverTemp(const std::string& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) && std::filesystem::exists(path + ".tmp", ec))
        std::rename((path + ".tmp").c_str(), path.c_str());
}

json readJson(const std::string& path) {
    recoverTemp(path);
    std::ifstream file(path);
    if (!file) return json(json::value_t::discarded);
    return json::parse(file, nullptr, false);
}

bool writeJson(const std::string& path, const json& data) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    std::string tmp = path + ".tmp";
    {
        std::ofstream file(tmp, std::ios::trunc);
        if (!file) return false;
        file << data.dump(2) << "\n";
        if (!file.good()) return false;
    }
    // rename() over an existing file is not guaranteed everywhere (newlib on the Switch): remove first
    std::remove(path.c_str());
    return std::rename(tmp.c_str(), path.c_str()) == 0;
}

std::string mediaKey(MediaType type, int tmdbId) {
    return (type == MediaType::Movie ? "movie/" : "series/") + std::to_string(tmdbId);
}

}  // namespace Storage
