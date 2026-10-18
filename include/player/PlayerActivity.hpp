#pragma once

#include <borealis.hpp>

#include "models/StreamSource.hpp"

class PlayerActivity : public brls::Activity {
public:
    explicit PlayerActivity(const StreamSource& source);
    ~PlayerActivity() override;
};
