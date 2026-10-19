#pragma once

#include <string>
#include <vector>

#include "models/MediaItem.hpp"

// Stremio addon protocol (stream resource): GET {base}/stream/{type}/{id}.json
namespace AddonClient {

// Accepts a base URL or a manifest URL ("https://x/manifest.json", "stremio://x/manifest.json") -> "https://x"
std::string normalizeBaseUrl(const std::string& url);

// "movie/tt0133093" or "series/tt0903747:1:2"
std::string streamPath(MediaType type, const std::string& imdbId, int season = 0, int episode = 0);

// Direct-URL streams from a {"streams": [...]} response. Torrent (infoHash), YouTube and external-link
// streams are skipped: the player only handles direct http(s) URLs.
bool parseStreams(const std::string& json, std::vector<StreamSource>& out);

// Readable single-line label: emojis the Switch font lacks are replaced (👤 -> "Seeds", 💾/⚙️ -> "·") or removed
std::string cleanLabel(const std::string& text);

// Orders streams for the Switch: debrid-cached first ("[RD+]"), uncached ("[RD download]") last, then
// Portuguese/dubbed, then 1080p > 720p > others > 4K (too heavy over the Switch Wi-Fi). Stable.
void rankStreams(std::vector<StreamSource>& streams);

// Blocking (wrap it in runInBackground): asks every addon, concatenates and ranks their streams.
// `errors` lists the addons that failed; returns the streams found (possibly empty).
std::vector<StreamSource> fetchStreams(const std::vector<std::string>& addons, MediaType type,
                                       const std::string& imdbId, int season, int episode, std::string& errors);

}  // namespace AddonClient
