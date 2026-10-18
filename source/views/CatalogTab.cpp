#include "views/CatalogTab.hpp"

#include <algorithm>

#include "core/Background.hpp"

#include "api/TmdbClient.hpp"
#include "views/DetailView.hpp"

using namespace brls::literals;

namespace {

const int CARDS_PER_ROW = 4;
const int ROWS_AHEAD    = 2;  // posters loaded below the focused row
const int ROWS_BEHIND   = 1;  // and above it
const int PREFETCH_ROWS = 2;  // next page requested when the focus is this close to the end

// Session state: the tab is recreated on every tab switch and shows the same list again
struct CatalogState {
    std::string query;  // empty = trending
    std::string heading;
    std::vector<MediaItem> items;
    int page       = 0;
    int totalPages = 1;
};
CatalogState state;

}  // namespace

PosterCard::PosterCard(const MediaItem& item) {
    this->inflateFromXMLRes("xml/cells/poster.xml");
    title->setText(item.title);
    std::string kind = item.type == MediaType::Movie ? "catalog/movie"_i18n : "catalog/series"_i18n;
    subtitle->setText(item.year.empty() ? kind : item.year + " · " + kind);
    posterUrl = TmdbClient::imageUrl(item.posterPath, TmdbClient::ImageSize::Poster);
    this->registerClickAction([item](brls::View*) {
        DetailView::open(item);
        return true;
    });
    this->addGestureRecognizer(new brls::TapGestureRecognizer(this));
}

void PosterCard::loadPoster() {
    if (posterUrl.empty() || poster->getTexture() != 0) return;
    poster->setImageFromUrl(posterUrl);
}

CatalogTab::CatalogTab() {
    this->inflateFromXMLRes("xml/tabs/catalog.xml");

    this->registerAction("catalog/search"_i18n, brls::BUTTON_Y, [this](brls::View*) {
        brls::Application::getImeManager()->openForText(
            [this](std::string query) {
                if (!query.empty()) startList(query);
            },
            "catalog/search_header"_i18n, "", 64);
        return true;
    });
    this->registerAction("catalog/trending"_i18n, brls::BUTTON_X, [this](brls::View*) {
        startList("");
        return true;
    });

    if (state.page > 0) {
        heading->setText(state.heading);
        if (state.items.empty()) {
            status->setText("catalog/empty"_i18n);
        } else {
            status->setVisibility(brls::Visibility::GONE);
            appendCards(state.items, 0);
            loadPostersAround(0);
        }
    } else {
        startList("");
    }
}

brls::View* CatalogTab::create() { return new CatalogTab(); }

void CatalogTab::startList(const std::string& query) {
    listGeneration++;
    loadingPage = false;
    state       = CatalogState{query, query.empty() ? "catalog/trending"_i18n
                                                    : fmt::format(fmt::runtime("catalog/results_for"_i18n), query)};
    heading->setText(state.heading);
    status->setText("catalog/loading"_i18n);
    status->setVisibility(brls::Visibility::VISIBLE);

    // clearViews() deletes the cards, possibly the focused one: park the focus on the status label
    brls::View* focus = brls::Application::getCurrentFocus();
    status->setFocusable(true);
    status->setHideHighlight(true);
    if (focus && focus->getParent() && focus->getParent()->getParent() == grid) brls::Application::giveFocus(status);
    grid->clearViews();
    loadPage(1);
}

void CatalogTab::loadPage(int page) {
    if (loadingPage) return;
    loadingPage       = true;
    int generation    = listGeneration;
    std::string query = state.query;
    ASYNC_RETAIN
    runInBackground([ASYNC_TOKEN, query, page, generation] {
        TmdbClient::ResultPage result;
        std::string error;
        if (query.empty()) TmdbClient::trending(page, result, error);
        else TmdbClient::search(query, page, result, error);
        brls::sync([ASYNC_TOKEN, result, error, generation] {
            ASYNC_RELEASE
            if (generation != this->listGeneration) return;
            this->loadingPage = false;
            if (!error.empty()) {
                // A failed first page replaces the list; a failed later page keeps what is shown
                if (state.page == 0) this->status->setText("catalog/error"_i18n + "\n" + error);
                return;
            }
            brls::Logger::info("catalog: page {}/{} with {} items", result.page, result.totalPages, result.items.size());
            bool first       = state.page == 0;
            size_t start     = state.items.size();
            state.page       = result.page;
            state.totalPages = result.totalPages;
            state.items.insert(state.items.end(), result.items.begin(), result.items.end());
            if (first && state.items.empty()) {
                this->status->setText("catalog/empty"_i18n);
                return;
            }
            this->status->setVisibility(brls::Visibility::GONE);
            this->appendCards(result.items, start);
            if (first) {
                this->loadPostersAround(0);
                // Focus parked on the status label by startList(): move it to the first card
                if (brls::Application::getCurrentFocus() == this->status)
                    brls::Application::giveFocus(((brls::Box*)this->grid->getChildren()[0])->getChildren()[0]);
                this->status->setFocusable(false);
            }
        });
    });
}

void CatalogTab::appendCards(const std::vector<MediaItem>& items, size_t firstIndex) {
    // Rows of cards (not one wrapping box) so that up/down moves between rows
    for (size_t i = 0; i < items.size(); i++) {
        if ((firstIndex + i) % CARDS_PER_ROW == 0) {
            auto* row = new brls::Box(brls::Axis::ROW);
            row->setMarginBottom(20);
            grid->addView(row);
        }
        ((brls::Box*)grid->getChildren().back())->addView(new PosterCard(items[i]));
    }
}

void CatalogTab::loadPostersAround(int row) {
    const auto& rows = grid->getChildren();
    int last         = std::min((int)rows.size() - 1, row + ROWS_AHEAD);
    for (int r = std::max(0, row - ROWS_BEHIND); r <= last; r++)
        for (brls::View* card : ((brls::Box*)rows[r])->getChildren()) ((PosterCard*)card)->loadPoster();
}

void CatalogTab::onChildFocusGained(brls::View* directChild, brls::View* focusedView) {
    brls::Box::onChildFocusGained(directChild, focusedView);

    // focusedView is a PosterCard inside a row of the grid
    brls::View* rowView = focusedView->getParent();
    if (!rowView || rowView->getParent() != grid) return;
    const auto& rows = grid->getChildren();
    int row          = (int)(std::find(rows.begin(), rows.end(), rowView) - rows.begin());

    loadPostersAround(row);
    if (row >= (int)rows.size() - PREFETCH_ROWS && state.page < state.totalPages) loadPage(state.page + 1);
}
