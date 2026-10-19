#pragma once

#include <borealis.hpp>

#include "models/MediaItem.hpp"

// Home: "continue watching", favorite titles and favorite channels.
class HomeTab : public brls::Box {
public:
    HomeTab();
    ~HomeTab() override;
    static brls::View* create();

    // Called when the main screen comes back (player/details closed): rebuilds if the data changed
    static void refreshActive();

private:
    void rebuild();
    void addSection(const std::string& title, const std::vector<brls::View*>& cards);

    BRLS_BIND(brls::Box, sections, "sections");
    BRLS_BIND(brls::Label, empty, "empty");

    int historyRevision   = -1;
    int favoritesRevision = -1;
};
