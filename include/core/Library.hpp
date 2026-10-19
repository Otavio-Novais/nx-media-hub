#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "models/MediaItem.hpp"

// "Continue watching" entry: one per movie or series (series keep their last episode)
struct WatchEntry {
    MediaItem item;
    int season       = 0;  // series only
    int episode      = 0;
    double position  = 0;  // seconds
    double duration  = 0;
    int64_t updatedAt = 0;  // unix time

    std::string key() const;
};

// history.json. Pure logic + file I/O (path injected): unit tested on the host.
class History {
public:
    explicit History(std::string path);
    static History& instance();  // sdmc:/switch/nx-media-hub/history.json (core/AppData.cpp)

    void load();
    std::vector<WatchEntry> entries() const;  // most recent first

    // Saved position to resume `item` (and episode, for series); 0 when there is nothing to resume
    double resumePosition(const MediaItem& item, int season, int episode) const;

    // Records progress (updatedAt = now when 0). Finished titles leave the list.
    void record(WatchEntry entry);
    void remove(const std::string& key);

    static bool isFinished(double position, double duration);

    // Incremented on every change (lets screens rebuild only when needed)
    int revision() const { return rev; }

private:
    void save();

    std::string path;
    std::vector<WatchEntry> list;  // most recent first
    int rev = 0;
};

// favorites.json: movies/series and channels.
class Favorites {
public:
    explicit Favorites(std::string path);
    static Favorites& instance();  // sdmc:/switch/nx-media-hub/favorites.json (core/AppData.cpp)

    void load();
    const std::vector<MediaItem>& media() const { return mediaList; }
    const std::vector<Channel>& channels() const { return channelList; }

    bool contains(const MediaItem& item) const;
    bool contains(const Channel& channel) const;
    // Adds or removes; returns true when the item is now a favorite
    bool toggle(const MediaItem& item);
    bool toggle(const Channel& channel);

    int revision() const { return rev; }

private:
    void save();

    std::string path;
    int rev = 0;
    std::vector<MediaItem> mediaList;  // newest first
    std::vector<Channel> channelList;
};
