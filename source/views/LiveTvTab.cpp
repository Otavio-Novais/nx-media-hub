#include "views/LiveTvTab.hpp"

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>

#include "api/HttpClient.hpp"
#include "api/M3uParser.hpp"
#include "core/Config.hpp"
#include "player/PlayerActivity.hpp"

using namespace brls::literals;

namespace {

const long LOGO_TIMEOUT = 8;  // seconds: dead logo hosts must not stall the task queue

// "All" and "Others" are synthetic categories (keys never produced by a playlist)
const std::string ALL_KEY    = "\x01all";
const std::string OTHERS_KEY = "\x01others";

// Reads an M3U source: http(s) URL or local file (ex: sdmc:/switch/nx-media-hub/lista.m3u)
bool readSource(const std::string& source, std::string& content, std::string& error) {
    if (source.find("://") != std::string::npos) {
        HttpResponse response = HttpClient::get(source);
        if (!response.ok()) {
            error = response.error.empty() ? "HTTP " + std::to_string(response.status) : response.error;
            return false;
        }
        content = std::move(response.body);
        return true;
    }
    std::ifstream file(source, std::ios::binary);
    if (!file) {
        error = "arquivo não encontrado";
        return false;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    content = buffer.str();
    return true;
}

class ChannelDataSource : public brls::RecyclerDataSource {
public:
    explicit ChannelDataSource(const std::vector<const Channel*>& channels) : channels(channels) {}

    int numberOfRows(brls::RecyclerFrame*, int) override { return (int)channels.size(); }

    brls::RecyclerCell* cellForRow(brls::RecyclerFrame* recycler, brls::IndexPath index) override {
        auto* cell = (ChannelCell*)recycler->dequeueReusableCell("Channel");
        cell->setChannel(*channels[index.row]);
        return cell;
    }

    void didSelectRowAt(brls::RecyclerFrame*, brls::IndexPath index) override {
        brls::Application::pushActivity(new PlayerActivity(channels[index.row]->stream));
    }

private:
    const std::vector<const Channel*>& channels;
};

}  // namespace

ChannelCell::ChannelCell() { this->inflateFromXMLRes("xml/cells/channel.xml"); }

ChannelCell* ChannelCell::create() { return new ChannelCell(); }

void ChannelCell::setChannel(const Channel& channel) {
    name->setText(channel.name);
    logo->clear();
    logoUrl = channel.logo;
    if (logoUrl.empty()) return;

    // Cells are recycled: only apply the logo if the cell still shows the same channel
    std::string url = logoUrl;
    ASYNC_RETAIN
    brls::async([ASYNC_TOKEN, url] {
        HttpResponse response = HttpClient::get(url, {}, LOGO_TIMEOUT);
        brls::sync([ASYNC_TOKEN, url, response] {
            ASYNC_RELEASE
            if (url != this->logoUrl) return;
            if (!response.ok() || response.body.empty()) {
                brls::Logger::warning("live: logo {} failed: {}", url,
                                      response.error.empty() ? "HTTP " + std::to_string(response.status) : response.error);
                return;
            }
            this->logo->setImageFromMem((const unsigned char*)response.body.data(), (int)response.body.size());
            // The fixed-size view does not relayout on a new texture: recompute the drawn bounds and paint,
            // otherwise the logo is drawn with the previous logo's ratio (and its freed texture)
            this->logo->onLayout();
        });
    });
}

LiveTvTab::LiveTvTab() {
    this->inflateFromXMLRes("xml/tabs/live.xml");
    channelList->estimatedRowHeight = 70;
    channelList->registerCell("Channel", [] { return ChannelCell::create(); });
    channelList->setVisibility(brls::Visibility::GONE);
    loadLists();
}

brls::View* LiveTvTab::create() { return new LiveTvTab(); }

void LiveTvTab::loadLists() {
    std::vector<std::string> sources = Config::instance().m3uUrls;
    if (sources.empty()) {
        status->setText("live/no_lists"_i18n);
        return;
    }

    ASYNC_RETAIN
    brls::async([ASYNC_TOKEN, sources] {
        std::vector<Channel> loaded;
        std::string errors;
        for (const auto& source : sources) {
            std::string content, error;
            if (!readSource(source, content, error)) {
                brls::Logger::error("live: cannot load {}: {}", source, error);
                errors += source + ": " + error + "\n";
                continue;
            }
            auto parsed = M3uParser::parse(content);
            brls::Logger::info("live: {} channels from {}", parsed.size(), source);
            std::move(parsed.begin(), parsed.end(), std::back_inserter(loaded));
        }
        brls::sync([ASYNC_TOKEN, loaded = std::move(loaded), errors]() mutable {
            ASYNC_RELEASE
            this->showChannels(std::move(loaded), errors);
        });
    });
}

void LiveTvTab::showChannels(std::vector<Channel> loaded, const std::string& error) {
    channels = std::move(loaded);
    if (channels.empty()) {
        status->setText(error.empty() ? "live/empty"_i18n : "live/error"_i18n + "\n" + error);
        return;
    }
    status->setVisibility(brls::Visibility::GONE);
    channelList->setVisibility(brls::Visibility::VISIBLE);

    // Category list: All, playlist groups in alphabetical order, Others (channels without group)
    std::set<std::string> groups;
    bool hasUngrouped = false;
    for (const auto& channel : channels) {
        groups.insert(channel.groups.begin(), channel.groups.end());
        hasUngrouped |= channel.groups.empty();
    }
    std::vector<std::pair<std::string, std::string>> categories = {{ALL_KEY, "live/all"_i18n}};
    for (const auto& group : groups) categories.emplace_back(group, group);
    if (hasUngrouped) categories.emplace_back(OTHERS_KEY, "live/others"_i18n);

    for (const auto& [key, label] : categories) {
        auto* cell = new brls::RadioCell();
        cell->title->setText(label);
        cell->registerClickAction([this, key = key](brls::View*) {
            selectCategory(key);
            return true;
        });
        categoryList->addView(cell);
        categoryCells.emplace_back(key, cell);
    }

    selectCategory(ALL_KEY);
}

void LiveTvTab::selectCategory(const std::string& category) {
    visible.clear();
    for (const auto& channel : channels) {
        bool match = category == ALL_KEY || (category == OTHERS_KEY && channel.groups.empty()) ||
                     std::find(channel.groups.begin(), channel.groups.end(), category) != channel.groups.end();
        if (match) visible.push_back(&channel);
    }
    for (auto& [key, cell] : categoryCells) cell->setSelected(key == category);

    // RecyclerFrame reads out of bounds when (re)loaded with zero rows: never hand it an empty list
    // (every category comes from at least one channel, so this only guards future changes)
    if (visible.empty()) return;
    if (channelList->getDataSource()) channelList->reloadData();
    else channelList->setDataSource(new ChannelDataSource(visible));
}
