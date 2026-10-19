#pragma once

#include <borealis.hpp>

class MainActivity : public brls::Activity {
public:
    CONTENT_FROM_XML_RES("activity/main.xml");

    void onContentAvailable() override;
    // Back from the player/details/settings dialogs: refresh what depends on history and favorites
    void onResume() override;
};
