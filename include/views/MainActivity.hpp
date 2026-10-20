#pragma once

#include <borealis.hpp>

class MainActivity : public brls::Activity {
public:
    brls::View* createContentView() override;

    void onContentAvailable() override;
    // Back from the player/details/settings dialogs: refresh what depends on history and favorites
    void onResume() override;
};
