#include "views/DetailView.hpp"

#include "core/Background.hpp"

#include "api/AddonClient.hpp"
#include "api/TmdbClient.hpp"
#include "core/Config.hpp"
#include "core/Library.hpp"
#include "views/SourcePicker.hpp"

using namespace brls::literals;

namespace {

std::string formatRuntime(int minutes) {
    if (minutes <= 0) return "";
    return minutes >= 60 ? fmt::format("{}h {:02}min", minutes / 60, minutes % 60) : fmt::format("{} min", minutes);
}

}  // namespace

void DetailView::open(const MediaItem& item) {
    auto* frame = new brls::AppletFrame(new DetailView(item));
    frame->setTitle(item.title);
    brls::Application::pushActivity(new brls::Activity(frame));
}

DetailView::DetailView(MediaItem mediaItem) : item(std::move(mediaItem)) {
    this->inflateFromXMLRes("xml/views/detail.xml");

    title->setText(item.title);
    overview->setText(item.overview.empty() ? "detail/no_overview"_i18n : item.overview);
    poster->setImageFromUrl(TmdbClient::imageUrl(item.posterPath, TmdbClient::ImageSize::Poster));
    playButton->setVisibility(item.type == MediaType::Movie ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    playButton->registerClickAction([this](brls::View*) {
        play();
        return true;
    });
    this->registerAction("detail/favorite"_i18n, brls::BUTTON_X, [this](brls::View*) {
        // Prefer the loaded details (full metadata) over the list entry
        const MediaItem& target = details.item.tmdbId != 0 ? details.item : item;
        bool added              = Favorites::instance().toggle(target);
        brls::Application::notify(added ? "detail/favorite_added"_i18n : "detail/favorite_removed"_i18n);
        return true;
    });

    MediaItem request = item;
    ASYNC_RETAIN
    runInBackground([ASYNC_TOKEN, request] {
        MediaDetails loaded;
        std::string error;
        TmdbClient::details(request.type, request.tmdbId, loaded, error);
        brls::sync([ASYNC_TOKEN, loaded, error] {
            ASYNC_RELEASE
            this->showDetails(loaded, error);
        });
    });
}

void DetailView::showDetails(const MediaDetails& loaded, const std::string& error) {
    if (!error.empty()) {
        meta->setText("detail/error"_i18n + ": " + error);
        return;
    }
    details = loaded;

    std::vector<std::string> parts;
    if (!details.item.year.empty()) parts.push_back(details.item.year);
    if (details.item.rating > 0) parts.push_back(fmt::format("★ {:.1f}", details.item.rating));
    if (!details.genres.empty()) {
        std::string genres;
        for (size_t i = 0; i < details.genres.size() && i < 3; i++) genres += (i ? ", " : "") + details.genres[i];
        parts.push_back(genres);
    }
    if (details.runtime > 0) parts.push_back(formatRuntime(details.runtime));
    std::string text;
    for (size_t i = 0; i < parts.size(); i++) text += (i ? "  ·  " : "") + parts[i];
    meta->setText(text);

    if (item.type != MediaType::Series || details.seasons.empty()) return;

    // Season selector: starts on the first regular season (0 is "Specials")
    std::vector<std::string> names;
    int selected = 0;
    for (size_t i = 0; i < details.seasons.size(); i++) {
        names.push_back(details.seasons[i].name);
        if (details.seasons[i].number == 1) selected = (int)i;
    }
    seasonSelector->init("detail/season"_i18n, names, selected,
                         [this](int index) { loadSeason(details.seasons[index].number); });
    series->setVisibility(brls::Visibility::VISIBLE);
    loadSeason(details.seasons[selected].number);
}

void DetailView::loadSeason(int seasonNumber) {
    int request = ++seasonRequest;
    episodeList->clearViews();
    auto* loading = new brls::Label();
    loading->setText("catalog/loading"_i18n);
    episodeList->addView(loading);

    int tmdbId = item.tmdbId;
    ASYNC_RETAIN
    runInBackground([ASYNC_TOKEN, tmdbId, seasonNumber, request] {
        std::vector<Episode> episodes;
        std::string error;
        TmdbClient::season(tmdbId, seasonNumber, episodes, error);
        brls::sync([ASYNC_TOKEN, episodes, error, request] {
            ASYNC_RELEASE
            if (request == this->seasonRequest) this->showEpisodes(episodes, error);
        });
    });
}

void DetailView::showEpisodes(const std::vector<Episode>& episodes, const std::string& error) {
    episodeList->clearViews();
    if (!error.empty() || episodes.empty()) {
        auto* label = new brls::Label();
        label->setText(error.empty() ? "detail/no_episodes"_i18n : "detail/error"_i18n + ": " + error);
        episodeList->addView(label);
        return;
    }
    for (const auto& episode : episodes) {
        auto* cell = new brls::DetailCell();
        cell->setText(fmt::format("{}. {}", episode.number, episode.name));
        cell->setDetailText(formatRuntime(episode.runtime));
        int season = episode.season, number = episode.number;
        cell->registerClickAction([this, season, number](brls::View*) {
            play(season, number);
            return true;
        });
        episodeList->addView(cell);
    }
}

void DetailView::play(int season, int episode) {
    if (details.imdbId.empty()) {
        // Details not loaded yet, or TMDB has no IMDb id for this title
        brls::Application::notify(details.item.tmdbId == 0 ? "catalog/loading"_i18n : "sources/no_imdb"_i18n);
        return;
    }
    std::vector<std::string> addons = Config::instance().addons;
    if (addons.empty()) {
        brls::Application::notify("sources/no_addons"_i18n);
        return;
    }

    std::string playerTitle = item.title;
    if (item.type == MediaType::Series) playerTitle += fmt::format(" · T{}E{}", season, episode);
    brls::Application::notify("sources/searching"_i18n);

    MediaType type     = item.type;
    std::string imdbId = details.imdbId;
    PlaybackContext context{details.item, season, episode};
    ASYNC_RETAIN
    runInBackground([ASYNC_TOKEN, addons, type, imdbId, season, episode, playerTitle, context] {
        std::string errors;
        auto streams = AddonClient::fetchStreams(addons, type, imdbId, season, episode, errors);
        brls::sync([ASYNC_TOKEN, streams, errors, playerTitle, context] {
            ASYNC_RELEASE
            if (streams.empty()) {
                brls::Application::notify(errors.empty() ? "sources/none"_i18n : "sources/addon_error"_i18n);
                return;
            }
            SourcePicker::show(streams, playerTitle, context);
        });
    });
}
