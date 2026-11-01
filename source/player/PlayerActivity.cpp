#include "player/PlayerActivity.hpp"

#include <fmt/format.h>

#include "core/Library.hpp"
#include "player/MpvCore.hpp"
#include "player/VideoView.hpp"

using namespace brls::literals;

namespace {

const double MIN_RECORD = 30;  // seconds watched before a title shows up in "continue watching"

}  // namespace

PlayerActivity::PlayerActivity(const StreamSource& source, PlaybackContext context)
    : PlayerActivity(std::vector<StreamSource>{source}, std::move(context)) {}

PlayerActivity::PlayerActivity(std::vector<StreamSource> sources, PlaybackContext context)
    : PlayerActivity(new VideoView(std::move(sources), [this] { recordProgress(); }), std::move(context)) {}

// VideoView picks the first source to try (the one that last worked this session)
PlayerActivity::PlayerActivity(VideoView* view, PlaybackContext context)
    : brls::Activity(view), source(view->currentSource()), context(std::move(context)) {
    double start = 0;
    if (this->context.item && !source.isLive)
        start = History::instance().resumePosition(*this->context.item, this->context.season, this->context.episode);
    MpvCore::instance().loadStream(source, start);
    if (start > 0) {
        int s = (int)start;
        brls::Application::notify(fmt::format(fmt::runtime("player/resuming"_i18n),
                                              fmt::format("{:02}:{:02}:{:02}", s / 3600, (s / 60) % 60, s % 60)));
    }
}

PlayerActivity::~PlayerActivity() {
    recordProgress();  // before stop(): the position is still the one the user left at
    MpvCore::instance().stop();
}

void PlayerActivity::recordProgress() {
    if (!context.item || source.isLive) return;
    auto& mpv = MpvCore::instance();
    bool finished = History::isFinished(mpv.position, mpv.duration);
    if (mpv.position < MIN_RECORD && !finished) return;
    History::instance().record({*context.item, context.season, context.episode, mpv.position, mpv.duration});
}
