#include "views/MainActivity.hpp"

#include "views/HomeTab.hpp"

using namespace brls::literals;

brls::View* MainActivity::createContentView() {
#ifdef ENABLE_MOVIES
    return brls::View::createFromXMLResource("activity/main.xml");
#else
    return brls::View::createFromXMLResource("activity/main_live.xml");  // without the catalog tab
#endif
}

void MainActivity::onContentAvailable() {
    // Applet (album) mode only gets a small memory pool: not enough for 1080p playback
    if (!brls::Application::getPlatform()->isApplicationMode()) {
        auto* dialog = new brls::Dialog("main/applet_warning"_i18n);
        dialog->addButton("hints/ok"_i18n, [] {});
        dialog->open();
    }
}

void MainActivity::onResume() { HomeTab::refreshActive(); }
