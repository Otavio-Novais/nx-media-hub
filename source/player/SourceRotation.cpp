#include "player/SourceRotation.hpp"

SourceRotation::SourceRotation(size_t count, size_t start, int64_t now)
    : total(count == 0 ? 1 : count), index(start < count ? start : 0) {
    restart(now);
}

bool SourceRotation::update(int64_t now, bool error, bool started, bool buffering) {
    if (gaveUp) return false;

    bool failed = error;
    if (!failed && !played) {
        if (started && !buffering) {
            played   = true;
            failures = 0;
        } else {
            failed = now - startedAt > STARTUP_LIMIT;
        }
    } else if (!failed) {
        if (!buffering) stallSince = 0;
        else if (stallSince == 0) stallSince = now;
        failed = stallSince != 0 && now - stallSince > STALL_LIMIT;
    }
    if (!failed) return false;

    if (++failures >= total) {
        gaveUp = true;
        return false;
    }
    index = (index + 1) % total;
    restart(now);
    return true;
}

void SourceRotation::step(int delta, int64_t now) {
    int n    = (int)total;
    index    = (size_t)(((int)index + delta % n + n) % n);
    failures = 0;
    gaveUp   = false;
    restart(now);
}

void SourceRotation::restart(int64_t now) {
    startedAt  = now;
    stallSince = 0;
    played     = false;
}
