#pragma once

#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "models/MediaItem.hpp"

namespace M3uParser {

// Parses an extended M3U playlist (#EXTINF, #EXTGRP, #EXTVLCOPT and Kodi-style "url|Header=value" suffixes).
// Pure function: no I/O, safe to unit test on the host.
std::vector<Channel> parse(std::string_view content);

// Incremental parse(): takes the playlist in chunks of any size (ex: as it downloads), so the whole file never
// has to be in memory. Keeps at most maxChannels channels: full() tells the caller it can stop reading.
class Parser {
public:
    explicit Parser(size_t maxChannels = std::numeric_limits<size_t>::max()) : maxChannels(maxChannels) {}

    void feed(std::string_view chunk);
    std::vector<Channel> finish();  // parses the last line (files may not end with a newline)

    bool full() const { return channels.size() >= maxChannels; }
    size_t size() const { return channels.size(); }

private:
    void parseLine(std::string_view line);

    size_t maxChannels;
    std::vector<Channel> channels;
    std::string partial;      // line cut by the end of the previous chunk
    Channel pending;          // filled by #EXTINF / #EXTGRP / #EXTVLCOPT until its URL line
    bool hasInfo   = false;
    bool firstLine = true;
};

// Merges channels that share the same stream URL (the same channel coming from several lists): keeps the
// first occurrence and its order, unions the categories and fills a missing logo. Pure.
std::vector<Channel> dedupe(std::vector<Channel> channels);

// Keeps channels whose tvg-id country ("Name.<cc>@Feed") is in `countries` (lowercase ISO codes, iptv-org
// uses "uk" for the United Kingdom) and drops regional feeds of other countries ("@DK", "@Poland"...).
// Channels without a tvg-id in that format are kept. Empty `countries` = no filtering. Pure.
std::vector<Channel> filterByCountry(std::vector<Channel> channels, const std::vector<std::string>& countries);

}  // namespace M3uParser
