#pragma once

#include <borealis.hpp>
#include <functional>
#include <vector>

#include "models/StreamSource.hpp"
#include "player/SourceRotation.hpp"

// Full-screen video surface with an on-screen display (title, progress, hints) drawn over it.
class VideoView : public brls::Box {
public:
    // onPeriodicSave: called every 30 s of playback (progress for "continue watching")
    // sources: the same channel from several servers/qualities, tried in order (one source for movies)
    explicit VideoView(std::vector<StreamSource> sources, std::function<void()> onPeriodicSave = {});

    const StreamSource& currentSource() const { return source; }

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
              brls::FrameContext* ctx) override;

private:
    void showOsd();
    void openTrackMenu(const std::string& type);
    void drawOsd(NVGcontext* vg, float x, float y, float width, float height);
    void loadCurrentSource();

    std::vector<StreamSource> sources;
    SourceRotation rotation;
    StreamSource source;  // the one playing
    brls::Time osdUntil = 0;
    std::function<void()> onPeriodicSave;
    brls::Time lastSave = 0;
};
