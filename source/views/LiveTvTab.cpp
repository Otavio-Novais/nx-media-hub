#include "views/LiveTvTab.hpp"

#include <algorithm>
#include <fstream>
#include <set>
#include <string_view>
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

// Session cache: the tab is recreated on every tab switch, the lists only reload on demand (X)
// or when Settings changed them (cachedVersion != Config::m3uVersion()).
// A load in progress is shared: its result goes to whichever tab instance is alive when it finishes.
ChannelList cachedChannels;
int cachedVersion          = -1;
std::string cachedCategory = ALL_KEY;
bool listsLoading          = false;
LiveTvTab* activeTab       = nullptr;

// All lists together: measured ~650 bytes per channel, so ~35 MB at most, far from the Switch's memory limit
// (iptv-org's list of every country has ~10k channels). Past it the rest of the list is ignored.
const size_t MAX_CHANNELS = 50000;

// Reads an M3U source into the parser while it downloads (the whole file is never in memory):
// http(s) URL or local file (ex: sdmc:/switch/nx-media-hub/lista.m3u). Stops once the parser is full.
bool readSource(const std::string& source, M3uParser::Parser& parser, std::string& error) {
    if (source.find("://") != std::string::npos) {
        HttpResponse response = HttpClient::getStream(source, [&parser](std::string_view chunk) {
            parser.feed(chunk);
            return !parser.full();
        });
        if (!response.ok()) {
            error = response.error.empty() ? "HTTP " + std::to_string(response.status) : response.error;
            return false;
        }
        return true;
    }
    std::ifstream file(source, std::ios::binary);
    if (!file) {
        error = "arquivo não encontrado";
        return false;
    }
    std::vector<char> buffer(64 * 1024);
    while (file && !parser.full()) {
        file.read(buffer.data(), (std::streamsize)buffer.size());  // the last read is short and ends the loop
        parser.feed(std::string_view(buffer.data(), (size_t)file.gcount()));
    }
    return true;
}

// How a source shows up in error messages: host only for URLs (they may carry credentials), full path for files
std::string sourceLabel(const std::string& source) {
    return source.find("://") != std::string::npos ? HttpClient::redact(source) : source;
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
        brls::Application::pushActivity(new PlayerActivity(channels[index.row]->versions()));
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
    if (cachedChannels && !cachedChannels->empty() && cachedVersion == Config::instance().m3uVersion()) showChannels(cachedChannels, "");
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
    int version                        = Config::instance().m3uVersion();
    runInBackground([sources, countries, version] {
        std::vector<Channel> loaded;
        std::string errors, truncated;
        for (const auto& source : sources) {
            if (loaded.size() >= MAX_CHANNELS) {
                truncated += sourceLabel(source) + "\n";
                continue;
            }
            M3uParser::Parser parser(MAX_CHANNELS - loaded.size());
            std::string error;
            if (!readSource(source, parser, error)) {
                brls::Logger::error("live: cannot load {}: {}", sourceLabel(source), error);
                errors += sourceLabel(source) + ": " + error + "\n";
                continue;
            }
            bool full   = parser.full();
            auto parsed = parser.finish();
            brls::Logger::info("live: {} channels from {}{}", parsed.size(), sourceLabel(source),
                               full ? " (channel limit reached, rest ignored)" : "");
            if (full) truncated += sourceLabel(source) + "\n";
            std::move(parsed.begin(), parsed.end(), std::back_inserter(loaded));
        }
        size_t total = loaded.size();
        loaded       = M3uParser::dedupe(std::move(loaded));
        if (loaded.size() < total) brls::Logger::info("live: {} duplicated channels merged", total - loaded.size());
        size_t unique = loaded.size();
        loaded        = M3uParser::filterByCountry(std::move(loaded), countries);
        if (loaded.size() < unique)
            brls::Logger::info("live: {} channels from other countries hidden ({} left)", unique - loaded.size(), loaded.size());
        size_t versions = loaded.size();
        loaded          = M3uParser::groupVersions(std::move(loaded));
        if (loaded.size() < versions)
            brls::Logger::info("live: {} versions of the same channels grouped ({} left)", versions - loaded.size(), loaded.size());
        auto shared = std::make_shared<const std::vector<Channel>>(std::move(loaded));
        brls::sync([loaded = std::move(shared), errors, truncated, version]() mutable {
            listsLoading   = false;
            cachedChannels = loaded;
            cachedVersion  = version;
            cachedCategory = ALL_KEY;
            // Settings changed the lists while these were loading: load the new ones instead
            if (version != Config::instance().m3uVersion()) {
                if (activeTab) activeTab->loadLists();
                return;
            }
            // Some lists failed but others loaded: the channels hide the error, so say it here
            if (!loaded->empty() && !errors.empty()) {
                errors.pop_back();  // trailing newline
                brls::Application::notify("live/partial_error"_i18n + "\n" + errors);
            }
            if (!truncated.empty()) {
                truncated.pop_back();
                brls::Application::notify(fmt::format(fmt::runtime("live/truncated"_i18n), MAX_CHANNELS) + "\n" + truncated);
            }
            if (activeTab) activeTab->showChannels(std::move(loaded), errors);
        });
    });
}

void LiveTvTab::showChannels(ChannelList loaded, const std::string& error) {
    channels = std::move(loaded);
    if (channels->empty()) {
        status->setText(error.empty() ? "live/empty"_i18n : "live/error"_i18n + "\n" + error);
        return;
    }
    status->setVisibility(brls::Visibility::GONE);
    channelList->setVisibility(brls::Visibility::VISIBLE);

    // Category list: All, playlist groups in alphabetical order, Others (channels without group)
    std::set<std::string> groups;
    bool hasUngrouped = false;
    for (const auto& channel : *channels) {
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
    for (const auto& channel : *channels) {
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
