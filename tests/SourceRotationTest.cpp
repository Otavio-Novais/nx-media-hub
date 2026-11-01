// Host-only unit tests for SourceRotation (run with ctest from the desktop build).

#include <cstdio>
#include <cstdlib>

#include "player/SourceRotation.hpp"

static int failures = 0;

#define CHECK(expr)                                                                       \
    do {                                                                                  \
        if (!(expr)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #expr); \
            failures++;                                                                   \
        }                                                                                 \
    } while (0)

const int64_t S = 1'000'000;  // one second in microseconds

static void testError() {
    SourceRotation r(3, 0, 0);
    CHECK(!r.update(1 * S, false, false, false));
    CHECK(r.update(2 * S, true, false, false));  // the source failed to open: next one right away
    CHECK(r.current() == 1);
}

static void testStartupTimeout() {
    SourceRotation r(3, 0, 0);
    CHECK(!r.update(0, false, false, false));
    CHECK(!r.update(1 * S, false, true, true));  // file loaded but still buffering: not playing yet
    CHECK(!r.update(9 * S, false, false, false));
    CHECK(r.update(11 * S, false, false, false));  // no picture in 10 s
    CHECK(r.current() == 1);
    CHECK(!r.update(12 * S, false, false, false));  // the timer restarted with the new source
}

static void testStall() {
    SourceRotation r(2, 0, 0);
    r.update(1 * S, false, false, false);
    r.update(2 * S, false, true, false);  // playing
    CHECK(r.playing());
    CHECK(!r.update(60 * S, false, true, false));  // playing for a long time is fine
    CHECK(!r.update(61 * S, false, true, true));   // short buffering is fine
    CHECK(!r.update(62 * S, false, true, false));
    CHECK(!r.update(70 * S, false, true, true));
    CHECK(!r.update(77 * S, false, true, true));
    CHECK(r.update(79 * S, false, true, true));  // stuck buffering for 9 s
    CHECK(r.current() == 1);
}

static void testGivesUp() {
    SourceRotation r(3, 1, 0);  // starts at the remembered source
    CHECK(r.update(1 * S, true, false, false) && r.current() == 2);
    CHECK(r.update(2 * S, true, false, false) && r.current() == 0);  // wraps around
    CHECK(!r.update(3 * S, true, false, false));                     // all 3 failed in a row: stop
    CHECK(r.exhausted() && r.current() == 0);
    CHECK(!r.update(30 * S, true, false, false));

    // A single source never rotates
    SourceRotation one(1, 0, 0);
    CHECK(!one.update(1 * S, true, false, false) && one.exhausted());
}

static void testPlayingResetsFailures() {
    SourceRotation r(2, 0, 0);
    CHECK(r.update(1 * S, true, false, false) && r.current() == 1);
    r.update(2 * S, false, false, false);
    r.update(3 * S, false, true, false);  // source 1 plays
    CHECK(r.update(50 * S, true, false, false) && r.current() == 0);  // later it drops: try again
    CHECK(!r.exhausted());
}

static void testManual() {
    SourceRotation r(3, 0, 0);
    r.step(-1, 1 * S);
    CHECK(r.current() == 2);
    r.step(1, 2 * S);
    CHECK(r.current() == 0);

    SourceRotation dead(2, 0, 0);
    dead.update(1 * S, true, false, false);
    dead.update(2 * S, true, false, false);
    CHECK(dead.exhausted() && dead.current() == 1);
    dead.step(1, 3 * S);  // the user tries again by hand
    CHECK(!dead.exhausted() && dead.current() == 0);
    CHECK(!dead.update(4 * S, false, false, false));
}

int main() {
    testError();
    testStartupTimeout();
    testStall();
    testGivesUp();
    testPlayingResetsFailures();
    testManual();
    if (failures) std::fprintf(stderr, "%d check(s) failed\n", failures);
    else std::printf("all SourceRotation tests passed\n");
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
