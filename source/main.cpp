#include <borealis.hpp>
#include <cstdlib>
#include <cstring>

#include "views/MainActivity.hpp"

using namespace brls::literals;

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "-d") == 0) brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);
    }

    brls::Platform::APP_LOCALE_DEFAULT = brls::LOCALE_PT_BR;

    if (!brls::Application::init()) {
        brls::Logger::error("Unable to init Borealis application");
        return EXIT_FAILURE;
    }

    brls::Application::createWindow("main/title"_i18n);
    brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::DARK);
    brls::Application::setGlobalQuit(true);

    brls::Application::pushActivity(new MainActivity());

    while (brls::Application::mainLoop())
        ;

    return EXIT_SUCCESS;
}
