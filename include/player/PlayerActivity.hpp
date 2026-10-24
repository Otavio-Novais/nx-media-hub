#pragma once

#include <borealis.hpp>
#include <optional>

#include "models/MediaItem.hpp"

// What is being played, for "continue watching". No item (live TV, test URLs): nothing is recorded.
struct PlaybackContext {
    std::optional<MediaItem> item;
    int season  = 0;
    int episode = 0;
};

class PlayerActivity : public brls::Activity {
public:
    explicit PlayerActivity(const StreamSource& source, PlaybackContext context = {});
    // Versions of a live channel, best first (Channel::versions()): plays the first one that starts
    explicit PlayerActivity(std::vector<StreamSource> versions);
    ~PlayerActivity() override;

private:
    void recordProgress();

    StreamSource source;
    PlaybackContext context;
};
