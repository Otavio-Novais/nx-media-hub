#pragma once

#include <cstddef>
#include <cstdint>

// Which source of a channel is playing, and when to give up on it for the next one.
// Pure (times are passed in, in microseconds): safe to unit test on the host.
class SourceRotation {
public:
    static constexpr int64_t STARTUP_LIMIT = 10'000'000;  // no picture yet after this: next source
    static constexpr int64_t STALL_LIMIT   = 8'000'000;   // buffering this long after playing: next source

    SourceRotation(size_t count, size_t start, int64_t now);

    // Called every frame with the player state (started: the current source is showing video, see
    // MpvCore::started()). True when it moved to the next source (the caller loads current()). Gives up
    // (exhausted()) after every source failed in a row since one last played.
    bool update(int64_t now, bool error, bool started, bool buffering);

    // Manual switch (wraps around); also clears exhausted()
    void step(int delta, int64_t now);

    size_t current() const { return index; }
    size_t count() const { return total; }
    bool exhausted() const { return gaveUp; }
    bool playing() const { return played; }

private:
    void restart(int64_t now);

    size_t total;
    size_t index;
    int64_t startedAt = 0;
    int64_t stallSince = 0;  // 0: not buffering
    bool played        = false;
    size_t failures   = 0;  // in a row, since a source last played
    bool gaveUp       = false;
};
