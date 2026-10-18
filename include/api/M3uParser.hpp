#pragma once

#include <string_view>
#include <vector>

#include "models/MediaItem.hpp"

namespace M3uParser {

// Parses an extended M3U playlist (#EXTINF, #EXTGRP, #EXTVLCOPT and Kodi-style "url|Header=value" suffixes).
// Pure function: no I/O, safe to unit test on the host.
std::vector<Channel> parse(std::string_view content);

}  // namespace M3uParser
