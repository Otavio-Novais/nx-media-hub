// Host-only unit tests for history/favorites persistence and the JSON model serialization.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "core/Library.hpp"
#include "core/Storage.hpp"

static int failures = 0;

#define CHECK(expr)                                                                        \
    do {                                                                                   \
        if (!(expr)) {                                                                     \
            std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #expr); \
            failures++;                                                                    \
        }                                                                                  \
    } while (0)

static std::string tempDir() {
    auto dir = std::filesystem::temp_directory_path() / ("nxmh-test-" + std::to_string(std::rand()));
    std::filesystem::remove_all(dir);
    return dir.string();
}

static MediaItem movie(int id, const std::string& title) {
    MediaItem item;
    item.tmdbId = id;
    item.type   = MediaType::Movie;
    item.title  = title;
    return item;
}

static MediaItem series(int id, const std::string& title) {
    MediaItem item = movie(id, title);
    item.type      = MediaType::Series;
    return item;
}

static void testSerialization() {
    MediaItem item = series(1396, "Breaking Bad");
    item.posterPath = "/p.jpg";
    item.year       = "2008";
    item.rating     = 8.9;
    MediaItem back  = nlohmann::json(item).get<MediaItem>();
    CHECK(back.tmdbId == 1396 && back.type == MediaType::Series && back.title == "Breaking Bad");
    CHECK(back.posterPath == "/p.jpg" && back.year == "2008" && back.rating > 8.8);

    Channel channel;
    channel.name             = "Canal";
    channel.logo             = "https://logo/x.png";
    channel.groups           = {"Esportes"};
    channel.stream.url       = "https://cdn/x.m3u8";
    channel.stream.referer   = "https://ref/";
    channel.stream.headers   = {{"Origin", "https://o"}};
    channel.stream.isLive    = true;
    channel.alternatives     = {{.title = "Canal SD", .url = "https://cdn/sd.m3u8", .isLive = true}};
    Channel c2               = nlohmann::json(channel).get<Channel>();
    CHECK(c2.name == "Canal" && c2.groups.size() == 1 && c2.stream.url == "https://cdn/x.m3u8");
    CHECK(c2.stream.referer == "https://ref/" && c2.stream.headers.at("Origin") == "https://o" && c2.stream.isLive);
    CHECK(c2.alternatives.size() == 1 && c2.alternatives[0].url == "https://cdn/sd.m3u8");
    // Favorites saved before versions existed have no "alternatives"
    nlohmann::json old = nlohmann::json(channel);
    old.erase("alternatives");
    CHECK(old.get<Channel>().alternatives.empty());

    // Missing fields fall back to defaults instead of throwing
    MediaItem empty = nlohmann::json::object().get<MediaItem>();
    CHECK(empty.tmdbId == 0 && empty.type == MediaType::Movie);
}

static void testHistory() {
    std::string dir  = tempDir();
    std::string path = dir + "/history.json";
    History history(path);
    history.load();  // missing file: empty history
    CHECK(history.entries().empty());

    history.record({movie(603, "Matrix"), 0, 0, 1200, 8160, 100});
    history.record({series(1396, "Breaking Bad"), 1, 2, 600, 2900, 200});
    auto entries = history.entries();
    CHECK(entries.size() == 2);
    CHECK(entries[0].item.tmdbId == 1396);  // most recent first

    // Resume only the same movie / the same episode, and only after 30 s
    CHECK(history.resumePosition(movie(603, "Matrix"), 0, 0) == 1200);
    CHECK(history.resumePosition(series(1396, "Breaking Bad"), 1, 2) == 600);
    CHECK(history.resumePosition(series(1396, "Breaking Bad"), 1, 3) == 0);
    history.record({movie(550, "Clube da Luta"), 0, 0, 10, 8000, 300});
    CHECK(history.resumePosition(movie(550, "Clube da Luta"), 0, 0) == 0);

    // Updating an entry moves it to the top instead of duplicating it
    history.record({movie(603, "Matrix"), 0, 0, 1500, 8160, 400});
    entries = history.entries();
    CHECK(entries.size() == 3 && entries[0].item.tmdbId == 603 && entries[0].position == 1500);

    // Finished titles leave the list
    history.record({movie(603, "Matrix"), 0, 0, 8100, 8160, 500});
    CHECK(history.resumePosition(movie(603, "Matrix"), 0, 0) == 0);
    CHECK(history.entries().size() == 2);

    // Persisted: a new instance reads the same entries
    History reloaded(path);
    reloaded.load();
    CHECK(reloaded.entries().size() == 2);
    CHECK(reloaded.entries()[0].item.tmdbId == 550);
    CHECK(reloaded.resumePosition(series(1396, "Breaking Bad"), 1, 2) == 600);

    // Bounded size
    for (int i = 0; i < 80; i++) reloaded.record({movie(10000 + i, "x"), 0, 0, 100, 1000, 1000 + i});
    CHECK(reloaded.entries().size() == 50);

    std::filesystem::remove_all(dir);
}

static void testFavorites() {
    std::string dir  = tempDir();
    std::string path = dir + "/favorites.json";
    Favorites favorites(path);
    favorites.load();
    CHECK(favorites.media().empty() && favorites.channels().empty());

    CHECK(favorites.toggle(movie(603, "Matrix")));
    CHECK(favorites.toggle(series(1396, "Breaking Bad")));
    CHECK(favorites.contains(movie(603, "Matrix")));
    CHECK(!favorites.contains(series(603, "same id, other type")));
    CHECK(favorites.media()[0].tmdbId == 1396);  // newest first

    Channel channel;
    channel.name       = "Canal";
    channel.stream.url = "https://cdn/x.m3u8";
    CHECK(favorites.toggle(channel));
    CHECK(favorites.contains(channel));

    Favorites reloaded(path);
    reloaded.load();
    CHECK(reloaded.media().size() == 2 && reloaded.channels().size() == 1);
    CHECK(!reloaded.toggle(movie(603, "Matrix")));  // second toggle removes
    CHECK(reloaded.media().size() == 1);

    std::filesystem::remove_all(dir);
}

static void testCorruptFileIsKept() {
    std::string dir  = tempDir();
    std::string path = dir + "/history.json";
    std::filesystem::create_directories(dir);
    std::ofstream(path) << "{ not json";
    History history(path);
    history.load();
    CHECK(history.entries().empty());
    // Loading a broken file must not overwrite it (the user may want to fix it by hand)
    std::ifstream in(path);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(content == "{ not json");
    std::filesystem::remove_all(dir);
}

static void testRecoverTemp() {
    // Simulates a crash between remove() and rename(): only the complete .tmp is left
    std::string dir  = tempDir();
    std::string path = dir + "/favorites.json";
    std::filesystem::create_directories(dir);
    std::ofstream(path + ".tmp") << R"({"media":[{"tmdbId":603,"type":"movie","title":"Matrix"}],"channels":[]})";
    Favorites favorites(path);
    favorites.load();
    CHECK(favorites.media().size() == 1);
    CHECK(std::filesystem::exists(path) && !std::filesystem::exists(path + ".tmp"));
    std::filesystem::remove_all(dir);
}

int main() {
    testRecoverTemp();
    testSerialization();
    testHistory();
    testFavorites();
    testCorruptFileIsKept();
    if (failures) std::fprintf(stderr, "%d check(s) failed\n", failures);
    else std::printf("all library tests passed\n");
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
