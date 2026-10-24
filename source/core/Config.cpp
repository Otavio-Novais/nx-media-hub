#include "core/Config.hpp"

#include "core/Storage.hpp"

#include <borealis/core/logger.hpp>
#include <borealis/extern/nlohmann/json.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace {

// iptv-org's Portuguese-language, sports and news lists merged, without the channels that are down
// (https://github.com/Otavio-Novais/nx-media-hub-lists, rechecked every 6 hours)
const std::vector<std::string> DEFAULT_M3U = {
    "https://raw.githubusercontent.com/Otavio-Novais/nx-media-hub-lists/main/lista_limpa.m3u",
};

#ifndef DEFAULT_TMDB_API_KEY
#define DEFAULT_TMDB_API_KEY ""
#endif
#ifndef DEFAULT_ADDONS
#define DEFAULT_ADDONS ""  // comma-separated
#endif

std::vector<std::string> splitList(const std::string& list) {
    std::vector<std::string> items;
    size_t start = 0;
    while (start < list.size()) {
        size_t end = list.find(',', start);
        if (end == std::string::npos) end = list.size();
        if (end > start) items.push_back(list.substr(start, end - start));
        start = end + 1;
    }
    return items;
}

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
    loadFile();
    // Build-time defaults (from .env): used when config.json leaves them empty, never written back
    if (tmdbApiKey.empty()) {
        tmdbApiKey       = DEFAULT_TMDB_API_KEY;
        tmdbKeyIsDefault = !tmdbApiKey.empty();
    }
    if (addons.empty()) {
        addons           = splitList(DEFAULT_ADDONS);
        addonsAreDefault = !addons.empty();
    }
}

void Config::loadFile() {
    Storage::recoverTemp(configPath());
    std::ifstream file(configPath());
    if (!file) {
        brls::Logger::info("config: {} not found, creating defaults", configPath());
        m3uUrls = DEFAULT_M3U;
        save();
        return;
    }

    nlohmann::json json = nlohmann::json::parse(file, nullptr, false);
    if (json.is_discarded() || !json.is_object()) {
        // Keep the user's file as is: overwriting it would lose their settings
        brls::Logger::error("config: {} is not valid JSON, using defaults without saving", configPath());
        m3uUrls = DEFAULT_M3U;
        return;
    }
    try {
        tmdbApiKey = json.value("tmdbApiKey", "");
        addons     = json.value("addons", std::vector<std::string>{});
        m3uUrls    = json.value("m3uUrls", std::vector<std::string>{});
        countries  = json.value("countries", std::vector<std::string>{});
    } catch (const nlohmann::json::exception& e) {
        // Wrong field types (ex: "m3uUrls": "url" instead of a list)
        brls::Logger::error("config: invalid field in {}: {}", configPath(), e.what());
        m3uUrls = DEFAULT_M3U;
    }
}

void Config::save() const {
    nlohmann::json json = {
        {"tmdbApiKey", tmdbKeyIsDefault ? "" : tmdbApiKey},
        {"addons", addonsAreDefault ? std::vector<std::string>{} : addons},
        {"m3uUrls", m3uUrls},
        {"countries", countries}};
    if (!Storage::writeJson(configPath(), json)) brls::Logger::error("config: cannot write {}", configPath());
}

void Config::setTmdbApiKey(const std::string& key) {
    tmdbApiKey       = key;
    tmdbKeyIsDefault = false;
    save();
}

void Config::setAddons(const std::vector<std::string>& list) {
    addons           = list;
    addonsAreDefault = false;
    save();
}

void Config::setM3uUrls(const std::vector<std::string>& list) {
    m3uUrls = list;
    m3uUrlsVersion++;
    save();
}
