#include "views/MainActivity.hpp"

using namespace brls::literals;

void MainActivity::onContentAvailable() {
    // Applet (album) mode only gets a small memory pool: not enough for 1080p playback
    if (!brls::Application::getPlatform()->isApplicationMode()) {
        auto* dialog = new brls::Dialog("main/applet_warning"_i18n);
        dialog->addButton("hints/ok"_i18n, [] {});
        dialog->open();
    }
}
