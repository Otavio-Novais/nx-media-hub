#pragma once

#include <borealis.hpp>

#include "models/StreamSource.hpp"

// Full-screen video surface with an on-screen display (title, progress, hints) drawn over it.
class VideoView : public brls::Box {
public:
    explicit VideoView(StreamSource source);

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
              brls::FrameContext* ctx) override;

private:
    void showOsd();
    void openTrackMenu(const std::string& type);
    void drawOsd(NVGcontext* vg, float x, float y, float width, float height);

    StreamSource source;
    brls::Time osdUntil = 0;
};
