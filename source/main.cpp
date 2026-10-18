#include <borealis.hpp>
#include <cstdlib>
#include <cstring>

#include "player/PlayerActivity.hpp"
#include "views/HomeTab.hpp"
#include "views/MainActivity.hpp"

using namespace brls::literals;

int main(int argc, char* argv[]) {
    // -d: debug logs; -p <url>: open the player directly (desktop testing)
    std::string playUrl;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "-d") == 0) brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);
        else if (std::strcmp(argv[i], "-p") == 0 && i + 1 < argc) playUrl = argv[++i];
    }

    brls::Platform::APP_LOCALE_DEFAULT = brls::LOCALE_PT_BR;

    if (!brls::Application::init()) {
        brls::Logger::error("Unable to init Borealis application");
        return EXIT_FAILURE;
    }

    brls::Application::createWindow("main/title"_i18n);
    brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::DARK);
    brls::Application::setGlobalQuit(true);

    brls::Application::registerXMLView("HomeTab", HomeTab::create);

    brls::Application::pushActivity(new MainActivity());
    if (!playUrl.empty()) brls::Application::pushActivity(new PlayerActivity({.title = playUrl, .url = playUrl}));

    while (brls::Application::mainLoop())
        ;

    return EXIT_SUCCESS;
}
