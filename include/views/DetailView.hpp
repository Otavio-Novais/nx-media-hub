#pragma once

#include <borealis.hpp>

#include "models/MediaItem.hpp"
#include "views/RemoteImage.hpp"

// Movie or series details: poster, synopsis and, for series, season selector + episode list.
class DetailView : public brls::Box {
public:
    explicit DetailView(MediaItem item);

    // Pushes the details screen (inside an AppletFrame titled with the item)
    static void open(const MediaItem& item);

private:
    void showDetails(const MediaDetails& details, const std::string& error);
    void loadSeason(int seasonNumber);
    void showEpisodes(const std::vector<Episode>& episodes, const std::string& error);
    void play(int season = 0, int episode = 0);

    BRLS_BIND(RemoteImage, poster, "poster");
    BRLS_BIND(brls::Label, title, "title");
    BRLS_BIND(brls::Label, meta, "meta");
    BRLS_BIND(brls::Label, overview, "overview");
    BRLS_BIND(brls::Button, playButton, "play");
    BRLS_BIND(brls::Box, series, "series");
    BRLS_BIND(brls::SelectorCell, seasonSelector, "season");
    BRLS_BIND(brls::Box, episodeList, "episodes");

    MediaItem item;
    MediaDetails details;
    int seasonRequest = 0;  // ignores late responses after the user picked another season
};
