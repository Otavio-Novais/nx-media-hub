#include "views/SettingsTab.hpp"

#include "core/Config.hpp"

using namespace brls::literals;

namespace {

const int URL_MAX_LENGTH = 512;

std::string maskKey(const std::string& key) {
    if (key.size() <= 4) return std::string(key.size(), '*');
    return std::string(8, '*') + key.substr(key.size() - 4);
}

}  // namespace

SettingsTab::SettingsTab() {
    this->inflateFromXMLRes("xml/tabs/settings.xml");
    rebuild();
}

brls::View* SettingsTab::create() { return new SettingsTab(); }

void SettingsTab::scheduleRebuild() {
    // Deferred: keyboards and dialogs give the focus back to the cell that opened them when they close,
    // and the rebuild deletes that cell. Park the focus first (see rebuild()).
    ASYNC_RETAIN
    brls::sync([ASYNC_TOKEN] {
        ASYNC_RELEASE
        this->rebuild();
    });
}

void SettingsTab::rebuild() {
    brls::View* focus = brls::Application::getCurrentFocus();
    bool focusInside  = false;
    for (brls::View* v = focus; v; v = v->hasParent() ? v->getParent() : nullptr)
        if (v == content) focusInside = true;
    parking->setFocusable(true);
    parking->setHideHighlight(true);
    if (focusInside) brls::Application::giveFocus(parking);
    content->clearViews();

    Config& config = Config::instance();

    addHeader("settings/tmdb"_i18n);
    std::string keyDetail = config.tmdbApiKey.empty()   ? "settings/not_set"_i18n
                            : config.isTmdbKeyDefault() ? "settings/from_build"_i18n
                                                        : maskKey(config.tmdbApiKey);
    addCell("settings/tmdb_key"_i18n, keyDetail)->registerClickAction([this](brls::View*) {
        Config& cfg      = Config::instance();
        std::string init = cfg.isTmdbKeyDefault() ? "" : cfg.tmdbApiKey;
        brls::Application::getImeManager()->openForText(
            [this](std::string key) {
                Config::instance().setTmdbApiKey(key);
                scheduleRebuild();
            },
            "settings/tmdb_key"_i18n, "settings/tmdb_hint"_i18n, 64, init);
        return true;
    });

    addHeader("settings/addons"_i18n);
    addUrlList(config.addons, "settings/add_addon"_i18n, "settings/addon_url"_i18n,
               [](std::vector<std::string> list) { Config::instance().setAddons(list); });

    addHeader("settings/lists"_i18n);
    addUrlList(config.m3uUrls, "settings/add_list"_i18n, "settings/list_url"_i18n,
               [](std::vector<std::string> list) { Config::instance().setM3uUrls(list); });
    auto* note = new brls::Label();
    note->setText("settings/lists_note"_i18n);
    note->setFontSize(15);
    note->setMarginTop(8);
    content->addView(note);

    addHeader("settings/about"_i18n);
    addCell("settings/version"_i18n, APP_VERSION);
    addCell("settings/data_dir"_i18n, Config::dataDir());

    // Focus parked above: hand it to the first cell
    if (brls::Application::getCurrentFocus() == parking) brls::Application::giveFocus(content->getDefaultFocus());
    parking->setFocusable(false);
}

void SettingsTab::addHeader(const std::string& title) {
    auto* header = new brls::Header();
    header->setTitle(title);
    content->addView(header);
}

brls::DetailCell* SettingsTab::addCell(const std::string& title, const std::string& detail) {
    auto* cell = new brls::DetailCell();
    cell->setText(title);
    cell->setDetailText(detail);
    content->addView(cell);
    return cell;
}

void SettingsTab::addUrlList(const std::vector<std::string>& urls, const std::string& addLabel,
                             const std::string& keyboardHeader, std::function<void(std::vector<std::string>)> save) {
    for (size_t i = 0; i < urls.size(); i++) {
        addCell(urls[i], "")->registerClickAction([this, urls, i, save](brls::View*) {
            auto* dialog = new brls::Dialog(fmt::format(fmt::runtime("settings/remove_confirm"_i18n), urls[i]));
            dialog->addButton("hints/cancel"_i18n, [] {});
            dialog->addButton("hints/delete"_i18n, [this, urls, i, save] {
                std::vector<std::string> list = urls;
                list.erase(list.begin() + (long)i);
                save(list);
                scheduleRebuild();
            });
            dialog->open();
            return true;
        });
    }
    addCell(addLabel, "")->registerClickAction([this, urls, keyboardHeader, save](brls::View*) {
        brls::Application::getImeManager()->openForText(
            [this, urls, save](std::string url) {
                if (url.empty()) return;
                std::vector<std::string> list = urls;
                list.push_back(url);
                save(list);
                scheduleRebuild();
            },
            keyboardHeader, "", URL_MAX_LENGTH, "https://");
        return true;
    });
}
