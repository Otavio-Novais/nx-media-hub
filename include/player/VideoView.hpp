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

    StreamSource source;
    brls::Time osdUntil = 0;
    std::function<void()> onPeriodicSave;
    brls::Time lastSave = 0;
};
