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

static void testCleanLabel() {
    using AddonClient::cleanLabel;
    CHECK(cleanLabel("[RD+] Torrentio Sintel \U0001F464 3 \U0001F4BE 441.89 MB \u2699\uFE0F ThePirateBay") ==
          "[RD+] Torrentio Sintel Seeds 3 · 441.89 MB · ThePirateBay");
    // Flags (regional indicators) and other emojis disappear; accents stay
    CHECK(cleanLabel("Dublado \U0001F1E7\U0001F1F7 / \U0001F1EC\U0001F1E7 Temporada 1ª") == "Dublado / Temporada 1ª");
    CHECK(cleanLabel("  1080p   x264  ") == "1080p x264");
}

static void testRank() {
    auto stream = [](const std::string& title) {
        StreamSource s;
        s.title = title;
        return s;
    };
    std::vector<StreamSource> streams = {
        stream("[RD download] Torrentio 1080p - Filme DUAL"),
        stream("[RD+] Torrentio 4k - Filme 2160p"),
        stream("[RD+] Torrentio 720p - Filme"),
        stream("[RD+] Torrentio 1080p - Filme"),
        stream("[RD+] Torrentio 1080p - Filme DUAL Dublado"),
        stream("Addon sem tags"),
    };
    AddonClient::rankStreams(streams);
    CHECK(streams[0].title == "[RD+] Torrentio 1080p - Filme DUAL Dublado");  // cached + Portuguese + 1080p
    CHECK(streams[1].title == "[RD+] Torrentio 1080p - Filme");
    CHECK(streams[2].title == "[RD+] Torrentio 720p - Filme");
    CHECK(streams[3].title == "[RD+] Torrentio 4k - Filme 2160p");
    CHECK(streams[4].title == "Addon sem tags");                            // other addons: neutral
    CHECK(streams[5].title == "[RD download] Torrentio 1080p - Filme DUAL");  // uncached last
}

int main() {
    testCleanLabel();
    testRank();
    testUrls();
    testStreams();
    testInvalid();
    if (failures) std::fprintf(stderr, "%d check(s) failed\n", failures);
    else std::printf("all addon parser tests passed\n");
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
