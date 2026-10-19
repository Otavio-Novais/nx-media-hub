// App-wide instances of the SD card data files (kept out of Library.cpp so the tests do not need Config).

#include "core/Config.hpp"
#include "core/Library.hpp"

History& History::instance() {
    static History history = [] {
        History h(Config::dataDir() + "/history.json");
        h.load();
        return h;
    }();
    return history;
}

Favorites& Favorites::instance() {
    static Favorites favorites = [] {
        Favorites f(Config::dataDir() + "/favorites.json");
        f.load();
        return f;
    }();
    return favorites;
}
