#include "player/VideoView.hpp"

#include <algorithm>
#include <cmath>
#include <fmt/format.h>

#include "player/MpvCore.hpp"

using namespace brls::literals;

namespace {

const brls::Time OSD_DURATION  = 3 * 1000 * 1000;   // usec
const brls::Time SAVE_INTERVAL = 30 * 1000 * 1000;  // usec

// Live reconnection: a stream counts as stuck when its position has not moved for STALL_TIMEOUT (buffering
// that never ends, or a load that never starts) or when it ended (keep-open leaves the last frame up).
// Like closing and opening the channel, but with growing waits so a server that is down is not hammered.
const brls::Time STALL_TIMEOUT    = 15 * 1000 * 1000;
// A version that has not started after this goes to the next one (same wait as the lists' check)
const brls::Time STARTUP_TIMEOUT  = 10 * 1000 * 1000;
const brls::Time STABLE_PLAYBACK  = 30 * 1000 * 1000;  // playing this long after a reload resets the attempts
// Waits before each attempt; past the list the last one repeats (10 attempts take about 2 minutes)
const brls::Time RETRY_DELAYS[]   = {1 * 1000 * 1000, 2 * 1000 * 1000, 4 * 1000 * 1000, 8 * 1000 * 1000,
                                     15 * 1000 * 1000};
const int RETRY_STEPS             = sizeof(RETRY_DELAYS) / sizeof(RETRY_DELAYS[0]);
const int MAX_ATTEMPTS            = 10;

std::string formatTime(double seconds) {
    if (std::isnan(seconds) || seconds < 0) seconds = 0;
    int total = (int)seconds;
    int h = total / 3600, m = (total / 60) % 60, s = total % 60;
    return h > 0 ? fmt::format("{}:{:02}:{:02}", h, m, s) : fmt::format("{:02}:{:02}", m, s);
}

}  // namespace

VideoView::VideoView(std::vector<StreamSource> versions, std::function<void()> onSave)
    : sources(std::move(versions)), source(sources.at(0)), onPeriodicSave(std::move(onSave)) {
    this->setFocusable(true);
    this->setHideHighlight(true);
    this->setGrow(1);

    auto& mpv = MpvCore::instance();
    this->registerAction("player/pause"_i18n, brls::BUTTON_A, [this, &mpv](brls::View*) {
        if (gaveUp) {  // "connection lost, press A": start over
            attempts = 0;
            startAttempt();
            return true;
        }
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

    progressAt = brls::getCPUTimeUsec();
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

void VideoView::watchLive() {
    auto& mpv      = MpvCore::instance();
    brls::Time now = brls::getCPUTimeUsec();
    if (gaveUp) return;
    if (reconnectAt) {
        if (now >= reconnectAt) startAttempt();
        return;
    }

    bool ended = !mpv.error.empty() || mpv.eofReached;
    // A pause by the user is not a stall (keep-open also pauses at the end: that one is caught above)
    if (mpv.position != lastPosition || (mpv.paused && !ended)) {
        if (lastPosition >= 0 && mpv.position > lastPosition) playing = everPlayed = true;
        lastPosition = mpv.position;
        progressAt   = now;
    }

    if (!playing) {
        // This version has not started: failed or too slow, go down to the next one
        if (!ended && now - progressAt < STARTUP_TIMEOUT) return;
        if (current + 1 < sources.size()) {
            brls::Logger::warning("player: version {}/{} did not start ({}), trying the next one", current + 1,
                                  sources.size(), ended ? (mpv.error.empty() ? "ended" : mpv.error) : "timeout");
            playVersion(current + 1);
            return;
        }
        // None started. Never played: leave the error (or the loading) on screen, as before; otherwise this
        // reconnection failed: wait and start over from the best version
        if (everPlayed) scheduleRetry(now, "no version started");
        return;
    }

    bool stalled = now - progressAt > STALL_TIMEOUT;
    if (!ended && !stalled) {
        if (attempts > 0 && !mpv.loading && now - reloadedAt > STABLE_PLAYBACK) attempts = 0;
        return;
    }
    scheduleRetry(now, ended ? (mpv.error.empty() ? "ended" : "failed: " + mpv.error) : "stalled");
}

void VideoView::scheduleRetry(brls::Time now, const std::string& reason) {
    if (attempts >= MAX_ATTEMPTS) {
        brls::Logger::error("player: live stream lost, giving up after {} reconnections", attempts);
        gaveUp = true;
        return;
    }
    brls::Time delay = RETRY_DELAYS[std::min(attempts, RETRY_STEPS - 1)];
    brls::Logger::warning("player: live stream {}, reconnecting in {}s (attempt {}/{})", reason, delay / 1000000,
                          attempts + 1, MAX_ATTEMPTS);
    reconnectAt = now + delay;
}

// One reconnection: from the best version down, like opening the channel again
void VideoView::startAttempt() {
    reconnectAt = 0;
    gaveUp      = false;
    attempts++;
    reloadedAt = brls::getCPUTimeUsec();
    playVersion(0);
}

void VideoView::playVersion(size_t index) {
    current      = index;
    source       = sources[index];
    playing      = false;
    progressAt   = brls::getCPUTimeUsec();
    lastPosition = -1;
    MpvCore::instance().loadStream(source);
}

void VideoView::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
                     brls::FrameContext* ctx) {
    auto& mpv = MpvCore::instance();
    mpv.draw(brls::Rect(x, y, width, height), this->getAlpha());
    if (source.isLive) watchLive();
    drawOsd(vg, x, y, width, height);

    brls::Time now = brls::getCPUTimeUsec();
    if (onPeriodicSave && !mpv.paused && !mpv.idle && now - lastSave > SAVE_INTERVAL) {
        lastSave = now;
        onPeriodicSave();
    }
    brls::Box::draw(vg, x, y, width, height, style, ctx);
}

void VideoView::drawOsd(NVGcontext* vg, float x, float y, float width, float height) {
    auto& mpv = MpvCore::instance();
    nvgFontFaceId(vg, brls::Application::getDefaultFont());

    // Status in the middle of the screen
    std::string status;
    if (gaveUp) status = "player/connection_lost"_i18n;
    else if (reconnectAt || (attempts > 0 && !playing))
        status = fmt::format(fmt::runtime("player/reconnecting"_i18n), attempts + (reconnectAt ? 1 : 0), MAX_ATTEMPTS);
    else if (!playing && current > 0)
        status = fmt::format(fmt::runtime("player/trying_version"_i18n), current + 1, sources.size());
    else if (!mpv.error.empty()) status = "player/error"_i18n + ": " + mpv.error;
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
