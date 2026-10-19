#include "views/SourcePicker.hpp"

#include <borealis.hpp>

#include "player/PlayerActivity.hpp"

using namespace brls::literals;

namespace SourcePicker {

void show(const std::vector<StreamSource>& sources, const std::string& playerTitle, const PlaybackContext& context) {
    std::vector<std::string> labels;
    for (const auto& source : sources) labels.push_back(source.title);

    auto* dropdown = new brls::Dropdown(
        "sources/pick"_i18n, labels, [](int) {}, 0,
        // dismissCb runs after the dropdown is popped: pushing the player in the selection callback
        // would make the dropdown's own popActivity() close the player instead
        [sources, playerTitle, context](int index) {
            if (index < 0 || index >= (int)sources.size()) return;
            StreamSource stream = sources[index];
            stream.title        = playerTitle;
            brls::Application::pushActivity(new PlayerActivity(stream, context));
        });
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

}  // namespace SourcePicker
