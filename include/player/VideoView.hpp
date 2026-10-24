#pragma once

#include <borealis.hpp>
#include <functional>
#include <vector>

#include "models/StreamSource.hpp"

// Full-screen video surface with an on-screen display (title, progress, hints) drawn over it.
class VideoView : public brls::Box {
public:
    // sources: versions of the same stream, best first (live channels; usually just one). The first one must
    // already be loading. onPeriodicSave: called every 30 s of playback (progress for "continue watching")
    explicit VideoView(std::vector<StreamSource> sources, std::function<void()> onPeriodicSave = {});

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
              brls::FrameContext* ctx) override;

private:
    void showOsd();
    void openTrackMenu(const std::string& type);
    void drawOsd(NVGcontext* vg, float x, float y, float width, float height);
    void watchLive();
    void scheduleRetry(brls::Time now, const std::string& reason);
    void startAttempt();
    void playVersion(size_t index);

    std::vector<StreamSource> sources;
    size_t current = 0;   // version playing (or trying to start)
    StreamSource source;  // = sources[current]
    brls::Time osdUntil = 0;
    std::function<void()> onPeriodicSave;
    brls::Time lastSave = 0;

    // Live: next version when one does not start, reload from the best one when it stalls or ends (watchLive)
    double lastPosition    = -1;
    brls::Time progressAt  = 0;  // last time the position moved (or the user paused, or a version was loaded)
    brls::Time reconnectAt = 0;  // pending reload, 0 = none
    brls::Time reloadedAt  = 0;
    int attempts           = 0;      // reloads since the stream last played fine
    bool playing           = false;  // the current version has started
    bool everPlayed        = false;  // a channel that never started shows its error instead of reconnecting
    bool gaveUp            = false;
};
