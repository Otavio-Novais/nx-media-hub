#include "core/Config.hpp"

#include <borealis/core/logger.hpp>
#include <borealis/extern/nlohmann/json.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace {

// Public list of free Brazilian channels (https://github.com/iptv-org/iptv)
const char* DEFAULT_M3U = "https://iptv-org.github.io/iptv/countries/br.m3u";

std::string configPath() { return Config::dataDir() + "/config.json"; }

}  // namespace

Config& Config::instance() {
    static Config config;
    return config;
}

std::string Config::dataDir() {
#ifdef __SWITCH__
    return "sdmc:/switch/nx-media-hub";
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) return std::string(xdg) + "/nx-media-hub";
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.config/nx-media-hub";
#endif
}

void Config::load() {
    std::ifstream file(configPath());
    if (!file) {
        brls::Logger::info("config: {} not found, creating defaults", configPath());
        m3uUrls = {DEFAULT_M3U};
        save();
        return;
    }

    nlohmann::json json = nlohmann::json::parse(file, nullptr, false);
    if (json.is_discarded() || !json.is_object()) {
        // Keep the user's file as is: overwriting it would lose their settings
        brls::Logger::error("config: {} is not valid JSON, using defaults without saving", configPath());
        m3uUrls = {DEFAULT_M3U};
        return;
    }
    try {
        tmdbApiKey = json.value("tmdbApiKey", "");
        addons     = json.value("addons", std::vector<std::string>{});
        m3uUrls    = json.value("m3uUrls", std::vector<std::string>{});
    } catch (const nlohmann::json::exception& e) {
        // Wrong field types (ex: "m3uUrls": "url" instead of a list)
        brls::Logger::error("config: invalid field in {}: {}", configPath(), e.what());
        m3uUrls = {DEFAULT_M3U};
    }
}

void Config::save() const {
    std::error_code ec;
    std::filesystem::create_directories(dataDir(), ec);
    nlohmann::json json = {{"tmdbApiKey", tmdbApiKey}, {"addons", addons}, {"m3uUrls", m3uUrls}};
    std::ofstream file(configPath());
    if (!file) {
        brls::Logger::error("config: cannot write {}", configPath());
        return;
    }
    file << json.dump(2) << "\n";
}
