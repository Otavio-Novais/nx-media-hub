#pragma once

#include <borealis.hpp>

#include "models/MediaItem.hpp"
#include "views/RemoteImage.hpp"

// Focusable poster + title; opens the details screen on click. The poster loads on demand.
class PosterCard : public brls::Box {
public:
    // subtitle: replaces the default "year · type" line (ex: progress in "continue watching")
    explicit PosterCard(const MediaItem& item, const std::string& subtitle = "");

    void loadPoster();

private:
    BRLS_BIND(RemoteImage, poster, "poster");
    BRLS_BIND(brls::Label, title, "title");
    BRLS_BIND(brls::Label, subtitleLabel, "subtitle");

    std::string posterUrl;
};
