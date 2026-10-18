#pragma once

#include <string>
#include <vector>

#include "models/StreamSource.hpp"

// A live TV channel or event from an M3U list.
struct Channel {
    std::string name;
    std::string logo;
    std::vector<std::string> groups;  // from group-title (";"-separated) or #EXTGRP
    StreamSource stream;              // isLive = true
};
