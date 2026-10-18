#include <borealis.hpp>
#include <cstdlib>
#include <cstring>
#include <exception>

#include "api/HttpClient.hpp"
#include "core/Config.hpp"
#include "player/PlayerActivity.hpp"
#include "views/CatalogTab.hpp"
#include "views/HomeTab.hpp"
#include "views/LiveTvTab.hpp"
#include "views/MainActivity.hpp"
#include "views/RemoteImage.hpp"

using namespace brls::literals;

int main(int argc, char* argv[]) {
    // Diagnostics: tell an unhandled exception apart from a normal quit in the nxlink logs
    std::set_terminate([] {
        try {
            if (auto e = std::current_exception()) std::rethrow_exception(e);
        } catch (const std::exception& e) {
            brls::Logger::error("terminate: unhandled exception: {}", e.what());
        } catch (...) {
            brls::Logger::error("terminate: unknown exception");
        }
        std::abort();
    });

    // -d: debug logs; -p <url>: open the player directly (desktop testing)
    std::string playUrl;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "-d") == 0) brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);
        else if (std::strcmp(argv[i], "-p") == 0 && i + 1 < argc) playUrl = argv[++i];
    }

    brls::Platform::APP_LOCALE_DEFAULT = brls::LOCALE_PT_BR;
    HttpClient::init();
    Config::instance().load();

    if (!brls::Application::init()) {
        brls::Logger::error("Unable to init Borealis application");
        return EXIT_FAILURE;
    }

    brls::Application::createWindow("main/title"_i18n);
    brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::DARK);
    brls::Application::setGlobalQuit(true);
    brls::Application::getExitEvent()->subscribe([] { brls::Logger::info("application exit requested"); });

    brls::Application::registerXMLView("RemoteImage", RemoteImage::create);
    brls::Application::registerXMLView("HomeTab", HomeTab::create);
    brls::Application::registerXMLView("CatalogTab", CatalogTab::create);
    brls::Application::registerXMLView("LiveTvTab", LiveTvTab::create);

    brls::Application::pushActivity(new MainActivity());
    if (!playUrl.empty()) brls::Application::pushActivity(new PlayerActivity({.title = playUrl, .url = playUrl}));

    while (brls::Application::mainLoop())
        ;

    brls::Logger::info("main loop ended: normal quit");
    return EXIT_SUCCESS;
}
