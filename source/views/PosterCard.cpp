#include "views/PosterCard.hpp"

#include "api/TmdbClient.hpp"
#include "views/DetailView.hpp"

using namespace brls::literals;

PosterCard::PosterCard(const MediaItem& item, const std::string& subtitle) {
    this->inflateFromXMLRes("xml/cells/poster.xml");
    title->setText(item.title);
    if (!subtitle.empty()) {
        subtitleLabel->setText(subtitle);
    } else {
        std::string kind = item.type == MediaType::Movie ? "catalog/movie"_i18n : "catalog/series"_i18n;
        subtitleLabel->setText(item.year.empty() ? kind : item.year + " · " + kind);
    }
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
