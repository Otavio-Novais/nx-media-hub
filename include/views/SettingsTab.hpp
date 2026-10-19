#pragma once

#include <borealis.hpp>

// Settings: TMDB key, addon and M3U list management (written to config.json) and app info.
class SettingsTab : public brls::Box {
public:
    SettingsTab();
    static brls::View* create();

private:
    void rebuild();
    void addHeader(const std::string& title);
    brls::DetailCell* addCell(const std::string& title, const std::string& detail);
    // Editable list of URLs: one cell per entry (A asks to remove) + an "add" cell (system keyboard)
    void addUrlList(const std::vector<std::string>& urls, const std::string& addLabel, const std::string& keyboardHeader,
                    std::function<void(std::vector<std::string>)> save);
    void scheduleRebuild();

    BRLS_BIND(brls::Box, content, "content");
    BRLS_BIND(brls::Label, parking, "parking");
};
