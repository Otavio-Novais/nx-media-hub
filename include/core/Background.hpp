#pragma once

#include <functional>

// Runs `task` on a new detached thread. Used for list/details requests so they do not wait behind the
// image downloads on the shared brls::async queue. pthread instead of std::thread: devkitA64's libstdc++
// throws "Function not implemented" for std::thread on the Switch.
void runInBackground(std::function<void()> task);
