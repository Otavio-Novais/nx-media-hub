#pragma once

#include <string>
#include <vector>

// User settings persisted in config.json (Switch: sdmc:/switch/nx-media-hub/, desktop: ~/.config/nx-media-hub/).
class Config {
public:
    static Config& instance();

    // Reads config.json; creates it with defaults when missing. A malformed file is left untouched.
    void load();
    void save() const;

    // Directory holding config.json, history.json and favorites.json
    static std::string dataDir();

    std::string tmdbApiKey;
    std::vector<std::string> addons;   // Stremio addon base URLs
    std::vector<std::string> m3uUrls;  // M3U list URLs or local paths

private:
    Config() = default;
    void loadFile();

    bool tmdbKeyIsDefault = false;  // key came from the build (.env), not from config.json
};
