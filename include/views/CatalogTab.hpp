#pragma once

#include <borealis.hpp>

#include "models/MediaItem.hpp"
#include "views/RemoteImage.hpp"

// Focusable poster + title; opens the details screen on click. The poster loads on demand.
class PosterCard : public brls::Box {
public:
    explicit PosterCard(const MediaItem& item);

    void loadPoster();

private:
    BRLS_BIND(RemoteImage, poster, "poster");
    BRLS_BIND(brls::Label, title, "title");
    BRLS_BIND(brls::Label, subtitle, "subtitle");

    std::string posterUrl;
};

// Movies and series from TMDB: trending by default, Y opens the search keyboard.
// Pages are appended while scrolling; posters load only around the focused row.
class CatalogTab : public brls::Box {
public:
    CatalogTab();
    static brls::View* create();

    void onChildFocusGained(brls::View* directChild, brls::View* focusedView) override;

private:
    // query empty = trending
    void startList(const std::string& query);
    void loadPage(int page);
    void appendCards(const std::vector<MediaItem>& items, size_t firstIndex);
    void loadPostersAround(int row);

    BRLS_BIND(brls::Label, heading, "heading");
    BRLS_BIND(brls::Label, status, "status");
    BRLS_BIND(brls::Box, grid, "grid");

    int listGeneration = 0;  // ignores pages of a list the user already replaced
    bool loadingPage   = false;
};
