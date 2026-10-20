#include "views/HomeTab.hpp"

#include <fmt/format.h>

#include "core/Library.hpp"
#include "player/PlayerActivity.hpp"
#include "views/PosterCard.hpp"
#include "views/RemoteImage.hpp"

using namespace brls::literals;

namespace {

HomeTab* activeHome = nullptr;

#ifdef ENABLE_MOVIES
std::string remaining(const WatchEntry& entry) {
    int minutes = (int)((entry.duration - entry.position) / 60);
    std::string left = minutes >= 60 ? fmt::format("{}h {:02}min", minutes / 60, minutes % 60) : fmt::format("{} min", minutes);
    std::string text = fmt::format(fmt::runtime("home/remaining"_i18n), left);
    if (entry.item.type == MediaType::Series) text = fmt::format("T{}E{} · {}", entry.season, entry.episode, text);
    return text;
}
#endif

// Channel logo + name; plays the channel on click
class ChannelCard : public brls::Box {
public:
    explicit ChannelCard(const Channel& channel) {
        this->inflateFromXMLRes("xml/cells/channel_card.xml");
        ((brls::Label*)getView("name"))->setText(channel.name);
        ((RemoteImage*)getView("logo"))->setImageFromUrl(channel.logo);
        this->registerClickAction([channel](brls::View*) {
            brls::Application::pushActivity(new PlayerActivity(channel.stream));
            return true;
        });
        this->registerAction("home/remove"_i18n, brls::BUTTON_Y, [channel](brls::View*) {
            Favorites::instance().toggle(channel);
            HomeTab::refreshActive();
            return true;
        });
        this->addGestureRecognizer(new brls::TapGestureRecognizer(this));
    }
};

}  // namespace

HomeTab::HomeTab() {
    this->inflateFromXMLRes("xml/tabs/home.xml");
    activeHome = this;
    rebuild();
}

HomeTab::~HomeTab() {
    if (activeHome == this) activeHome = nullptr;
}

brls::View* HomeTab::create() { return new HomeTab(); }

void HomeTab::refreshActive() {
    // Deferred: on resume Borealis restores the focus first, and the rebuild must see that focus
    brls::sync([] {
        if (activeHome &&
            (activeHome->historyRevision != History::instance().revision() ||
             activeHome->favoritesRevision != Favorites::instance().revision()))
            activeHome->rebuild();
    });
}

void HomeTab::rebuild() {
    historyRevision   = History::instance().revision();
    favoritesRevision = Favorites::instance().revision();

    // clearViews() deletes the cards, possibly the focused one: park the focus on the empty label
    brls::View* focus = brls::Application::getCurrentFocus();
    bool focusInside  = false;
    for (brls::View* v = focus; v; v = v->hasParent() ? v->getParent() : nullptr)
        if (v == sections) focusInside = true;
    empty->setFocusable(true);
    empty->setHideHighlight(true);
    if (focusInside) brls::Application::giveFocus(empty);
    sections->clearViews();

    std::vector<brls::View*> continueCards, favoriteCards, channelCards;
#ifdef ENABLE_MOVIES
    for (const auto& entry : History::instance().entries()) {
        auto* card = new PosterCard(entry.item, remaining(entry));
        std::string key = entry.key();
        card->registerAction("home/remove"_i18n, brls::BUTTON_Y, [key](brls::View*) {
            History::instance().remove(key);
            HomeTab::refreshActive();
            return true;
        });
        card->loadPoster();
        continueCards.push_back(card);
    }
    for (const auto& item : Favorites::instance().media()) {
        auto* card = new PosterCard(item);
        card->registerAction("home/remove"_i18n, brls::BUTTON_Y, [item](brls::View*) {
            Favorites::instance().toggle(item);
            HomeTab::refreshActive();
            return true;
        });
        card->loadPoster();
        favoriteCards.push_back(card);
    }
#endif
    for (const auto& channel : Favorites::instance().channels()) channelCards.push_back(new ChannelCard(channel));

    addSection("home/continue"_i18n, continueCards);
    addSection("home/favorites"_i18n, favoriteCards);
    addSection("home/channels"_i18n, channelCards);

    bool nothing = continueCards.empty() && favoriteCards.empty() && channelCards.empty();
#ifndef ENABLE_MOVIES
    empty->setText("home/empty_live"_i18n);
#endif
    empty->setVisibility(nothing ? brls::Visibility::VISIBLE : brls::Visibility::GONE);

    if (brls::Application::getCurrentFocus() == empty && !nothing) {
        // Focus parked above: move it to the first card of the first section
        auto* section = (brls::Box*)sections->getChildren()[0];
        auto* scroll  = (brls::HScrollingFrame*)section->getChildren()[1];
        brls::Application::giveFocus(scroll->getDefaultFocus());
    }
    if (!nothing) empty->setFocusable(false);
}

void HomeTab::addSection(const std::string& title, const std::vector<brls::View*>& cards) {
    if (cards.empty()) return;
    auto* section = new brls::Box(brls::Axis::COLUMN);
    section->setMarginBottom(24);

    auto* header = new brls::Header();
    header->setTitle(title);
    section->addView(header);

    auto* row = new brls::Box(brls::Axis::ROW);
    for (brls::View* card : cards) row->addView(card);
    auto* scroll = new brls::HScrollingFrame();
    scroll->setHeight(310);
    scroll->setContentView(row);
    section->addView(scroll);

    sections->addView(section);
}
