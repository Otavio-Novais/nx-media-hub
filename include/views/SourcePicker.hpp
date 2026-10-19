#pragma once

#include <string>
#include <vector>

#include "player/PlayerActivity.hpp"

namespace SourcePicker {

// Lists the streams in a dropdown and opens the player with the chosen one (titled `playerTitle`)
void show(const std::vector<StreamSource>& sources, const std::string& playerTitle, const PlaybackContext& context);

}  // namespace SourcePicker
