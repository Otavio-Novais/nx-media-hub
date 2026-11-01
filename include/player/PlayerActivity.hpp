#pragma once

#include <borealis.hpp>
#include <optional>
#include <vector>

#include "models/MediaItem.hpp"

// What is being played, for "continue watching". No item (live TV, test URLs): nothing is recorded.
struct PlaybackContext {
    std::optional<MediaItem> item;
    int season  = 0;
    int episode = 0;
};

class VideoView;

class PlayerActivity : public brls::Activity {
public:
    explicit PlayerActivity(const StreamSource& source, PlaybackContext context = {});
    // A live channel with several sources: tried in order, switching when one fails (see SourceRotation)
    explicit PlayerActivity(std::vector<StreamSource> sources, PlaybackContext context = {});
    ~PlayerActivity() override;

private:
    // The content view is built first: brls::Activity sets getContentView() only later
    PlayerActivity(VideoView* view, PlaybackContext context);
    void recordProgress();

    StreamSource source;
    PlaybackContext context;
};
