#include "views/HomeTab.hpp"

#include "player/PlayerActivity.hpp"

HomeTab::HomeTab() {
    this->inflateFromXMLRes("xml/tabs/home.xml");

    // Temporary Phase 1 test streams
    auto bind = [this](const std::string& id, StreamSource source) {
        this->getView(id)->registerClickAction([source](brls::View*) {
            brls::Application::pushActivity(new PlayerActivity(source));
            return true;
        });
    };
    bind("test_bbb", {.title = "Big Buck Bunny", .url = "https://test-streams.mux.dev/x36xhzz/x36xhzz.m3u8"});
    bind("test_apple", {.title = "Apple BipBop",
                        .url   = "https://devstreaming-cdn.apple.com/videos/streaming/examples/"
                                 "img_bipbop_adv_example_ts/master.m3u8"});
}

brls::View* HomeTab::create() { return new HomeTab(); }
