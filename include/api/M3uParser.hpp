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

// Keeps channels whose tvg-id country ("Name.<cc>@Feed") is in `countries` (lowercase ISO codes, iptv-org
// uses "uk" for the United Kingdom) and drops regional feeds of other countries ("@DK", "@Poland"...).
// Channels without a tvg-id in that format are kept. Empty `countries` = no filtering. Pure.
std::vector<Channel> filterByCountry(std::vector<Channel> channels, const std::vector<std::string>& countries);

}  // namespace M3uParser
