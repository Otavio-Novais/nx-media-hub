#include "player/VideoView.hpp"

#include <algorithm>
#include <cmath>
#include <fmt/format.h>

#include "player/MpvCore.hpp"

using namespace brls::literals;

namespace {

const brls::Time OSD_DURATION = 3 * 1000 * 1000;  // usec

std::string formatTime(double seconds) {
    if (std::isnan(seconds) || seconds < 0) seconds = 0;
    int total = (int)seconds;
    int h = total / 3600, m = (total / 60) % 60, s = total % 60;
    return h > 0 ? fmt::format("{}:{:02}:{:02}", h, m, s) : fmt::format("{:02}:{:02}", m, s);
}

}  // namespace

VideoView::VideoView(StreamSource src) : source(std::move(src)) {
    this->setFocusable(true);
    this->setHideHighlight(true);
    this->setGrow(1);

    auto& mpv = MpvCore::instance();
    this->registerAction("player/pause"_i18n, brls::BUTTON_A, [this, &mpv](brls::View*) {
        mpv.togglePause();
        showOsd();
        return true;
    });
    this->registerAction("hints/back"_i18n, brls::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity();
        return true;
    });
    this->registerAction("player/audio"_i18n, brls::BUTTON_X, [this](brls::View*) {
        openTrackMenu("audio");
        return true;
    });
    this->registerAction("player/subtitles"_i18n, brls::BUTTON_Y, [this](brls::View*) {
        openTrackMenu("sub");
        return true;
    });

    if (!source.isLive) {
        auto seek = [this, &mpv](double seconds) {
            return [this, &mpv, seconds](brls::View*) {
                mpv.seekRelative(seconds);
                showOsd();
                return true;
            };
        };
        for (auto button : {brls::BUTTON_LEFT, brls::BUTTON_LB})
            this->registerAction("-10s", button, seek(-10), true, true);
        for (auto button : {brls::BUTTON_RIGHT, brls::BUTTON_RB})
            this->registerAction("+10s", button, seek(10), true, true);
    }

    showOsd();
}

void VideoView::showOsd() { osdUntil = brls::getCPUTimeUsec() + OSD_DURATION; }

void VideoView::openTrackMenu(const std::string& type) {
    auto& mpv     = MpvCore::instance();
    auto tracks   = mpv.getTracks(type);
    bool isSub    = type == "sub";
    if (tracks.empty() && !isSub) {
        brls::Application::notify("player/no_tracks"_i18n);
        return;
    }

    // Subtitles get an extra "off" entry (track id 0)
    std::vector<std::string> labels;
    std::vector<int64_t> ids;
    int selected = 0;
    if (isSub) {
        labels.push_back("player/off"_i18n);
        ids.push_back(0);
    }
    for (const auto& track : tracks) {
        if (track.selected) selected = (int)labels.size();
        labels.push_back(track.label);
        ids.push_back(track.id);
    }

    std::string property = isSub ? "sid" : "aid";
    auto* dropdown       = new brls::Dropdown(
        isSub ? "player/subtitles"_i18n : "player/audio"_i18n, labels,
        [property, ids](int index) {
            if (index >= 0 && index < (int)ids.size()) MpvCore::instance().selectTrack(property, ids[index]);
        },
        selected);
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void VideoView::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
                     brls::FrameContext* ctx) {
    MpvCore::instance().draw(brls::Rect(x, y, width, height), this->getAlpha());
    drawOsd(vg, x, y, width, height);
    brls::Box::draw(vg, x, y, width, height, style, ctx);
}

void VideoView::drawOsd(NVGcontext* vg, float x, float y, float width, float height) {
    auto& mpv = MpvCore::instance();
    nvgFontFaceId(vg, brls::Application::getDefaultFont());

    // Status in the middle of the screen
    std::string status;
    if (!mpv.error.empty()) status = "player/error"_i18n + ": " + mpv.error;
    else if (mpv.loading || mpv.buffering) status = "player/loading"_i18n;
    if (!status.empty()) {
        nvgFontSize(vg, 28);
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(vg, nvgRGB(255, 255, 255));
        nvgText(vg, x + width / 2, y + height / 2, status.c_str(), nullptr);
    }

    if (!mpv.paused && brls::getCPUTimeUsec() > osdUntil) return;

    // Top: title
    nvgBeginPath(vg);
    nvgRect(vg, x, y, width, 90);
    nvgFillPaint(vg, nvgLinearGradient(vg, x, y, x, y + 90, nvgRGBA(0, 0, 0, 200), nvgRGBA(0, 0, 0, 0)));
    nvgFill(vg);
    nvgFontSize(vg, 26);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgFillColor(vg, nvgRGB(255, 255, 255));
    nvgText(vg, x + 40, y + 28, source.title.c_str(), nullptr);

    // Diagnostics: decoder in use and buffered seconds
    std::string stats = fmt::format("{} · buffer {:.0f}s", mpv.hwdec.empty() ? "?" : mpv.hwdec, mpv.cached);
    nvgFontSize(vg, 18);
    nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 160));
    nvgText(vg, x + width - 40, y + 32, stats.c_str(), nullptr);

    // Bottom: progress, time and button hints
    float barY = y + height - 110;
    nvgBeginPath(vg);
    nvgRect(vg, x, barY - 40, width, 150);
    nvgFillPaint(vg, nvgLinearGradient(vg, x, barY - 40, x, y + height, nvgRGBA(0, 0, 0, 0), nvgRGBA(0, 0, 0, 220)));
    nvgFill(vg);

    nvgFontSize(vg, 22);
    if (source.isLive) {
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFillColor(vg, nvgRGB(230, 60, 60));
        nvgText(vg, x + 40, barY, "player/live"_i18n.c_str(), nullptr);
    } else {
        float barX = x + 40, barW = width - 80;
        float progress = mpv.duration > 0 ? (float)std::clamp(mpv.position / mpv.duration, 0.0, 1.0) : 0;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, barX, barY - 3, barW, 6, 3);
        nvgFillColor(vg, nvgRGBA(255, 255, 255, 70));
        nvgFill(vg);
        nvgBeginPath(vg);
        nvgRoundedRect(vg, barX, barY - 3, barW * progress, 6, 3);
        nvgFillColor(vg, nvgRGB(0, 190, 220));
        nvgFill(vg);

        std::string time = formatTime(mpv.position) + " / " + formatTime(mpv.duration);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        nvgFillColor(vg, nvgRGB(255, 255, 255));
        nvgText(vg, barX, barY + 16, time.c_str(), nullptr);
    }

    std::string hints = mpv.paused ? "player/hints_paused"_i18n : "player/hints"_i18n;
    if (source.isLive) hints = "player/hints_live"_i18n;
    nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 200));
    nvgText(vg, x + width - 40, barY + 16, hints.c_str(), nullptr);
}
