#pragma once

#include <borealis/extern/nlohmann/json.hpp>
#include <string>

#include "models/MediaItem.hpp"

// JSON (de)serialization of the models persisted on the SD card, plus safe file helpers.
// Pure (no Borealis runtime): unit tested on the host.

void to_json(nlohmann::json& j, const MediaItem& item);
void from_json(const nlohmann::json& j, MediaItem& item);
void to_json(nlohmann::json& j, const StreamSource& source);
void from_json(const nlohmann::json& j, StreamSource& source);
void to_json(nlohmann::json& j, const Channel& channel);
void from_json(const nlohmann::json& j, Channel& channel);

namespace Storage {

// A crash between the remove() and rename() of writeJson() leaves only "<path>.tmp" (complete):
// moves it back into place. Call before deciding that a file is missing.
void recoverTemp(const std::string& path);

// Parses a JSON file (recovering a leftover temporary file); discarded value when missing or invalid
nlohmann::json readJson(const std::string& path);

// Writes `path` through a temporary file + rename, so a crash or power loss mid-write never leaves a
// truncated file behind. Creates the parent directory. Returns false on failure.
bool writeJson(const std::string& path, const nlohmann::json& json);

// "movie/603", "series/1396": identifies a title in history and favorites
std::string mediaKey(MediaType type, int tmdbId);

}  // namespace Storage
