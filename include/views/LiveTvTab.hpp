#pragma once

#include <borealis.hpp>
#include <memory>

#include "models/MediaItem.hpp"
#include "views/RemoteImage.hpp"

class ChannelCell : public brls::RecyclerCell {
public:
    ChannelCell();
    static ChannelCell* create();

    void setChannel(const Channel& channel);

    BRLS_BIND(RemoteImage, logo, "logo");
    BRLS_BIND(brls::Label, name, "name");

private:
    Channel channel;  // copy: the cell is recycled for other channels
};

// Loaded channels, shared by the session cache and the tab instead of copied (a big list takes tens of MB)
using ChannelList = std::shared_ptr<const std::vector<Channel>>;

// Live channels from the M3U lists in config.json: categories on the left, channels on the right.
class LiveTvTab : public brls::Box {
public:
    LiveTvTab();
    ~LiveTvTab() override;
    static brls::View* create();

    void showChannels(ChannelList loaded, const std::string& error);

private:
    void loadLists();
    void selectCategory(const std::string& category);

    BRLS_BIND(brls::Box, categoryList, "categories");
    BRLS_BIND(brls::RecyclerFrame, channelList, "channels");
    BRLS_BIND(brls::Label, status, "status");

    ChannelList channels;
    std::vector<const Channel*> visible;  // channels of the selected category
    std::vector<std::pair<std::string, brls::RadioCell*>> categoryCells;
};
