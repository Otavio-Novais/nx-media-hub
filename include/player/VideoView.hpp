#pragma once

#include <borealis.hpp>
#include <functional>

#include "models/StreamSource.hpp"

// Full-screen video surface with an on-screen display (title, progress, hints) drawn over it.
class VideoView : public brls::Box {
public:
    // onPeriodicSave: called every 30 s of playback (progress for "continue watching")
    explicit VideoView(StreamSource source, std::function<void()> onPeriodicSave = {});

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
              brls::FrameContext* ctx) override;

private:
    void showOsd();
    void openTrackMenu(const std::string& type);
    void drawOsd(NVGcontext* vg, float x, float y, float width, float height);
    void watchLive();
    void reconnect();


    StreamSource source;
    brls::Time osdUntil = 0;
    std::function<void()> onPeriodicSave;
    brls::Time lastSave = 0;

    // Live: reload the stream when it stalls or ends (see watchLive)
    double lastPosition    = -1;
    brls::Time progressAt  = 0;  // last time the position moved (or the user paused)
    brls::Time reconnectAt = 0;  // pending reload, 0 = none
    brls::Time reloadedAt  = 0;
    int attempts           = 0;  // reloads since the stream last played fine
    bool played            = false;  // a channel that never started shows its error right away instead
    bool gaveUp            = false;
};
