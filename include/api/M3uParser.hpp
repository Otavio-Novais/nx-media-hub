#pragma once

#include <string_view>
#include <vector>

#include "models/MediaItem.hpp"

namespace M3uParser {

// Parses an extended M3U playlist (#EXTINF, #EXTGRP, #EXTVLCOPT and Kodi-style "url|Header=value" suffixes).
// Pure function: no I/O, safe to unit test on the host.
std::vector<Channel> parse(std::string_view content);

// Merges channels that share the same stream URL (the same channel coming from several lists): keeps the
// first occurrence and its order, unions the categories and fills a missing logo. Pure.
std::vector<Channel> dedupe(std::vector<Channel> channels);

}  // namespace M3uParser
