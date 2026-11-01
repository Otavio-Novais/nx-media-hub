#include "core/Library.hpp"

#include <algorithm>
#include <ctime>

#include "api/M3uParser.hpp"
#include "core/Storage.hpp"

using nlohmann::json;

namespace {

const size_t MAX_HISTORY      = 50;
const double MIN_RESUME       = 30;   // seconds: shorter progress is not worth resuming
const double FINISHED_RATIO   = 0.95;
const double FINISHED_REMAINS = 60;   // seconds left (long videos: end credits)

// The grouped channel, not one of its sources: favorites survive the best source changing, and the ones saved
// before grouping ("ESPN 4 FHD", one URL) still match. A name made only of tags falls back to the URL.
std::string channelKey(const Channel& channel) {
    std::string key = M3uParser::channelGroupKey(channel.name);
    return key.empty() ? channel.stream.url : key;
}

}  // namespace

// Global scope (not in the anonymous namespace): nlohmann finds these through ADL on WatchEntry
static void to_json(json& j, const WatchEntry& e) {
    j = {{"item", e.item},         {"season", e.season},     {"episode", e.episode},
         {"position", e.position}, {"duration", e.duration}, {"updatedAt", e.updatedAt}};
}

static void from_json(const json& j, WatchEntry& e) {
    e.item      = j.value("item", MediaItem{});
    e.season    = j.value("season", 0);
    e.episode   = j.value("episode", 0);
    e.position  = j.value("position", 0.0);
    e.duration  = j.value("duration", 0.0);
    e.updatedAt = j.value("updatedAt", (int64_t)0);
}

std::string WatchEntry::key() const { return Storage::mediaKey(item.type, item.tmdbId); }

// History

History::History(std::string path) : path(std::move(path)) {}

void History::load() {
    list.clear();
    json data = Storage::readJson(path);
    if (data.is_discarded() || !data.is_array()) return;  // missing or broken: left untouched on disk
    for (const auto& entry : data) {
        try {
            list.push_back(entry.get<WatchEntry>());
        } catch (const json::exception&) {
            // skip a malformed entry, keep the rest
        }
    }
    std::stable_sort(list.begin(), list.end(),
                     [](const WatchEntry& a, const WatchEntry& b) { return a.updatedAt > b.updatedAt; });
}

std::vector<WatchEntry> History::entries() const { return list; }

double History::resumePosition(const MediaItem& item, int season, int episode) const {
    std::string key = Storage::mediaKey(item.type, item.tmdbId);
    for (const auto& entry : list) {
        if (entry.key() != key) continue;
        bool sameEpisode = item.type == MediaType::Movie || (entry.season == season && entry.episode == episode);
        return sameEpisode && entry.position >= MIN_RESUME ? entry.position : 0;
    }
    return 0;
}

bool History::isFinished(double position, double duration) {
    if (duration <= 0) return false;
    return position >= duration * FINISHED_RATIO || (duration > 600 && duration - position < FINISHED_REMAINS);
}

void History::record(WatchEntry entry) {
    if (entry.updatedAt == 0) entry.updatedAt = (int64_t)std::time(nullptr);
    std::string key = entry.key();
    list.erase(std::remove_if(list.begin(), list.end(), [&](const WatchEntry& e) { return e.key() == key; }),
               list.end());
    if (!isFinished(entry.position, entry.duration)) list.insert(list.begin(), entry);
    if (list.size() > MAX_HISTORY) list.resize(MAX_HISTORY);
    save();
}

void History::remove(const std::string& key) {
    list.erase(std::remove_if(list.begin(), list.end(), [&](const WatchEntry& e) { return e.key() == key; }),
               list.end());
    save();
}

void History::save() {
    rev++;
    Storage::writeJson(path, json(list));
}

// Favorites

Favorites::Favorites(std::string path) : path(std::move(path)) {}

void Favorites::load() {
    mediaList.clear();
    channelList.clear();
    json data = Storage::readJson(path);
    if (data.is_discarded() || !data.is_object()) return;
    try {
        mediaList   = data.value("media", std::vector<MediaItem>{});
        channelList = data.value("channels", std::vector<Channel>{});
    } catch (const json::exception&) {
        mediaList.clear();
        channelList.clear();
    }
}

bool Favorites::contains(const MediaItem& item) const {
    return std::any_of(mediaList.begin(), mediaList.end(),
                       [&](const MediaItem& m) { return m.type == item.type && m.tmdbId == item.tmdbId; });
}

bool Favorites::contains(const Channel& channel) const {
    return std::any_of(channelList.begin(), channelList.end(),
                       [&](const Channel& c) { return channelKey(c) == channelKey(channel); });
}

bool Favorites::toggle(const MediaItem& item) {
    bool added = !contains(item);
    if (added) mediaList.insert(mediaList.begin(), item);
    else
        mediaList.erase(std::remove_if(mediaList.begin(), mediaList.end(),
                                       [&](const MediaItem& m) { return m.type == item.type && m.tmdbId == item.tmdbId; }),
                        mediaList.end());
    save();
    return added;
}

bool Favorites::toggle(const Channel& channel) {
    bool added = !contains(channel);
    if (added) channelList.insert(channelList.begin(), channel);
    else
        channelList.erase(std::remove_if(channelList.begin(), channelList.end(),
                                         [&](const Channel& c) { return channelKey(c) == channelKey(channel); }),
                          channelList.end());
    save();
    return added;
}

void Favorites::save() {
    rev++;
    Storage::writeJson(path, json{{"media", mediaList}, {"channels", channelList}});
}
