#pragma once

#include <borealis.hpp>

#include "models/MediaItem.hpp"

class ChannelCell : public brls::RecyclerCell {
public:
    ChannelCell();
    static ChannelCell* create();

    void setChannel(const Channel& channel);

    BRLS_BIND(brls::Image, logo, "logo");
    BRLS_BIND(brls::Label, name, "name");

private:
    std::string logoUrl;
};

// Live channels from the M3U lists in config.json: categories on the left, channels on the right.
class LiveTvTab : public brls::Box {
public:
    LiveTvTab();
    static brls::View* create();

private:
    void loadLists();
    void showChannels(std::vector<Channel> loaded, const std::string& error);
    void selectCategory(const std::string& category);

    BRLS_BIND(brls::Box, categoryList, "categories");
    BRLS_BIND(brls::RecyclerFrame, channelList, "channels");
    BRLS_BIND(brls::Label, status, "status");

    std::vector<Channel> channels;
    std::vector<const Channel*> visible;  // channels of the selected category
    std::vector<std::pair<std::string, brls::RadioCell*>> categoryCells;
};
