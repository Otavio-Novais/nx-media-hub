#include "player/PlayerActivity.hpp"

#include "player/MpvCore.hpp"
#include "player/VideoView.hpp"

PlayerActivity::PlayerActivity(const StreamSource& source) : brls::Activity(new VideoView(source)) {
    MpvCore::instance().loadStream(source);
}

PlayerActivity::~PlayerActivity() { MpvCore::instance().stop(); }
