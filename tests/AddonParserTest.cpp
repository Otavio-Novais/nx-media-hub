// Host-only unit tests for the Stremio addon protocol parsing.

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "api/AddonClient.hpp"

static int failures = 0;

#define CHECK(expr)                                                                        \
    do {                                                                                   \
        if (!(expr)) {                                                                     \
            std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #expr); \
            failures++;                                                                    \
        }                                                                                  \
    } while (0)

static std::string readFixture(const std::string& name) {
    std::ifstream file(std::string(FIXTURES_DIR) + "/" + name, std::ios::binary);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

static void testUrls() {
    using AddonClient::normalizeBaseUrl;
    CHECK(normalizeBaseUrl("https://addon.example.com/manifest.json") == "https://addon.example.com");
    CHECK(normalizeBaseUrl("https://addon.example.com/cfg/abc/manifest.json") == "https://addon.example.com/cfg/abc");
    CHECK(normalizeBaseUrl("stremio://addon.example.com/manifest.json") == "https://addon.example.com");
    CHECK(normalizeBaseUrl("http://192.168.0.10:7000/") == "http://192.168.0.10:7000");

    CHECK(AddonClient::streamPath(MediaType::Movie, "tt0133093") == "movie/tt0133093");
    CHECK(AddonClient::streamPath(MediaType::Series, "tt0903747", 1, 2) == "series/tt0903747:1:2");
}

static void testStreams() {
    std::vector<StreamSource> streams;
    CHECK(AddonClient::parseStreams(readFixture("addon_streams.json"), streams));
    // torrent, YouTube and external-link entries are skipped
    CHECK(streams.size() == 3);

    const auto& first = streams[0];
    CHECK(first.url == "https://cdn.example.com/movie.m3u8");
    CHECK(first.title == "Addon 1080p - Dublado PT-BR 2.1 GB");
    CHECK(first.userAgent == "Stremio");
    CHECK(first.referer == "https://ref.example.com/");
    CHECK(first.headers.count("Origin") == 1);
    CHECK(first.subtitles.size() == 2);
    CHECK(first.subtitles[0].language == "por" && first.subtitles[0].url == "https://subs.example.com/pt.srt");
    CHECK(!first.isLive);

    CHECK(streams[1].title == "Legado: só title");
    CHECK(streams[2].title == "https://cdn.example.com/no-name.mp4");
}

static void testInvalid() {
    std::vector<StreamSource> streams;
    CHECK(!AddonClient::parseStreams("not json", streams));
    CHECK(!AddonClient::parseStreams("{\"err\": \"not found\"}", streams));
    CHECK(AddonClient::parseStreams("{\"streams\": []}", streams) && streams.empty());
}

int main() {
    testUrls();
    testStreams();
    testInvalid();
    if (failures) std::fprintf(stderr, "%d check(s) failed\n", failures);
    else std::printf("all addon parser tests passed\n");
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
