#include "views/LiveTvTab.hpp"

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include "core/Background.hpp"

#include "api/HttpClient.hpp"
#include "api/M3uParser.hpp"
#include "core/Config.hpp"
#include "core/Library.hpp"
#include "player/PlayerActivity.hpp"

using namespace brls::literals;

namespace {

// "All" and "Others" are synthetic categories (keys never produced by a playlist)
const std::string ALL_KEY    = "\x01all";
const std::string OTHERS_KEY = "\x01others";

// Session cache: the tab is recreated on every tab switch, the lists only reload on demand (X).
// A load in progress is shared: its result goes to whichever tab instance is alive when it finishes.
std::vector<Channel> cachedChannels;
std::string cachedCategory = ALL_KEY;
bool listsLoading          = false;
LiveTvTab* activeTab       = nullptr;

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

ChannelCell::ChannelCell() {
    this->inflateFromXMLRes("xml/cells/channel.xml");
    this->registerAction("live/favorite"_i18n, brls::BUTTON_Y, [this](brls::View*) {
        bool added = Favorites::instance().toggle(channel);
        brls::Application::notify(added ? "detail/favorite_added"_i18n : "detail/favorite_removed"_i18n);
        return true;
    });
}

ChannelCell* ChannelCell::create() { return new ChannelCell(); }

void ChannelCell::setChannel(const Channel& newChannel) {
    channel = newChannel;
    name->setText(channel.name);
    logo->setImageFromUrl(channel.logo);
}

LiveTvTab::LiveTvTab() {
    this->inflateFromXMLRes("xml/tabs/live.xml");
    channelList->estimatedRowHeight = 70;
    channelList->registerCell("Channel", [] { return ChannelCell::create(); });
    channelList->setVisibility(brls::Visibility::GONE);

    this->registerAction("live/reload"_i18n, brls::BUTTON_X, [this](brls::View*) {
        loadLists();
        return true;
    });

    activeTab = this;
    if (!cachedChannels.empty()) showChannels(cachedChannels, "");
    else loadLists();
}

LiveTvTab::~LiveTvTab() {
    if (activeTab == this) activeTab = nullptr;
}

brls::View* LiveTvTab::create() { return new LiveTvTab(); }

void LiveTvTab::loadLists() {
    // Back to the loading state; the current channels stay alive until the new ones replace them.
    // clearViews() deletes the category cells, possibly the focused one: park the focus on the status label
    status->setFocusable(true);
    status->setHideHighlight(true);
    if (!categoryCells.empty()) brls::Application::giveFocus(status);
    categoryList->clearViews();
    categoryCells.clear();
    channelList->setVisibility(brls::Visibility::GONE);
    status->setText("live/loading"_i18n);
    status->setVisibility(brls::Visibility::VISIBLE);

    std::vector<std::string> sources = Config::instance().m3uUrls;
    if (sources.empty()) {
        status->setText("live/no_lists"_i18n);
        return;
    }
    if (listsLoading) return;
    listsLoading = true;

    // Own thread: the shared brls::async queue is busy with image downloads
    std::vector<std::string> countries = Config::instance().countries;
    runInBackground([sources, countries] {
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
        size_t total = loaded.size();
        loaded       = M3uParser::dedupe(std::move(loaded));
        if (loaded.size() < total) brls::Logger::info("live: {} duplicated channels merged", total - loaded.size());
        size_t unique = loaded.size();
        loaded        = M3uParser::filterByCountry(std::move(loaded), countries);
        if (loaded.size() < unique)
            brls::Logger::info("live: {} channels from other countries hidden ({} left)", unique - loaded.size(), loaded.size());
        brls::sync([loaded = std::move(loaded), errors]() mutable {
            listsLoading   = false;
            cachedChannels = loaded;
            cachedCategory = ALL_KEY;
            if (activeTab) activeTab->showChannels(std::move(loaded), errors);
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

    bool known = std::any_of(categories.begin(), categories.end(), [](auto& c) { return c.first == cachedCategory; });
    selectCategory(known ? cachedCategory : ALL_KEY);

    // Focus was parked on the status label during a reload: hand it back to the selected category
    if (brls::Application::getCurrentFocus() == status) {
        for (auto& [key, cell] : categoryCells)
            if (key == cachedCategory) brls::Application::giveFocus(cell);
    }
    status->setFocusable(false);
}

void LiveTvTab::selectCategory(const std::string& category) {
    visible.clear();
    for (const auto& channel : channels) {
        bool match = category == ALL_KEY || (category == OTHERS_KEY && channel.groups.empty()) ||
                     std::find(channel.groups.begin(), channel.groups.end(), category) != channel.groups.end();
        if (match) visible.push_back(&channel);
    }
    for (auto& [key, cell] : categoryCells) cell->setSelected(key == category);
    cachedCategory = category;

    // RecyclerFrame reads out of bounds when (re)loaded with zero rows: never hand it an empty list
    // (every category comes from at least one channel, so this only guards future changes)
    if (visible.empty()) return;
    if (channelList->getDataSource()) channelList->reloadData();
    else channelList->setDataSource(new ChannelDataSource(visible));
}
