#pragma once

#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "models/MediaItem.hpp"

namespace M3uParser {

// Parses an extended M3U playlist (#EXTINF, #EXTGRP, #EXTVLCOPT and Kodi-style "url|Header=value" suffixes).
// Live channels only: movies and series episodes (Xtream "/movie/", "/series/", video files) are skipped.
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

// The name a channel is grouped by: uppercase, without [..]/(..), symbols, emoji and quality/codec tags
// ("ESPN 4 FHD H.265" -> "ESPN 4"). Empty when the name is only tags. Pure.
std::string channelGroupKey(std::string_view name);

// Merges the entries that share channelGroupKey() (the same channel in several qualities and lists) into one
// channel: `stream` is the best source and `alternates` the others, in the order FHD, HD, no tag, 4K, SD
// (the Switch shows at most 1080p); within a quality, servers alternate (a server that is down costs one try)
// and list order breaks ties. The name loses its tags, categories are the union,
// the logo is the first one found and the tvg-id the best source's. Keeps the order of first appearance. Pure.
std::vector<Channel> groupSources(std::vector<Channel> channels);

// Unifies the category names of lists from several panels: drops the "CANAIS =" / "CANAIS:" / "Canais |"
// prefixes and emoji, merges names that differ only in case or accents ("CANAIS: ESPN ⚽" and "Canais | ESPN"
// -> "ESPN") and shows the most readable spelling (lowercase and accents preferred; all-caps words longer
// than 4 letters become capitalized). Pure.
std::vector<Channel> tidyCategories(std::vector<Channel> channels);

// Keeps channels whose tvg-id country ("Name.<cc>@Feed") is in `countries` (lowercase ISO codes, iptv-org
// uses "uk" for the United Kingdom) and drops regional feeds of other countries ("@DK", "@Poland"...).
// Channels without a tvg-id in that format are kept. Empty `countries` = no filtering. Pure.
std::vector<Channel> filterByCountry(std::vector<Channel> channels, const std::vector<std::string>& countries);

}  // namespace M3uParser
