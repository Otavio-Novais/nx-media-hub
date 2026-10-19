// Host-only unit tests for M3uParser (run with ctest from the desktop build).

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "api/M3uParser.hpp"

static int failures = 0;

#define CHECK(expr)                                                         \
    do {                                                                    \
        if (!(expr)) {                                                      \
            std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #expr); \
            failures++;                                                     \
        }                                                                   \
    } while (0)

static std::string readFixture(const std::string& name) {
    std::ifstream file(std::string(FIXTURES_DIR) + "/" + name, std::ios::binary);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

static void testBasicAttributes() {
    auto channels = M3uParser::parse(readFixture("basic.m3u"));
    CHECK(channels.size() == 3);

    const auto& globo = channels[0];
    CHECK(globo.name == "Globo SP");
    CHECK(globo.logo == "https://example.com/globo.png");
    CHECK(globo.groups.size() == 1 && globo.groups[0] == "Canais Abertos");
    CHECK(globo.stream.url == "https://example.com/globo/index.m3u8");
    CHECK(globo.stream.title == "Globo SP");
    CHECK(globo.stream.isLive);

    // Several categories separated by ";" and a comma inside a quoted attribute
    const auto& multi = channels[1];
    CHECK(multi.name == "Canal Esportes 1");
    CHECK(multi.groups.size() == 2 && multi.groups[0] == "Esportes" && multi.groups[1] == "Notícias");

    // No title after the comma: falls back to tvg-name
    CHECK(channels[2].name == "Fallback Name");
    CHECK(channels[2].groups.empty());
}

static void testHeaders() {
    auto channels = M3uParser::parse(readFixture("headers.m3u"));
    CHECK(channels.size() == 3);

    // #EXTVLCOPT lines apply to the next URL only
    CHECK(channels[0].stream.userAgent == "Mozilla/5.0 Test");
    CHECK(channels[0].stream.referer == "https://ref.example.com/");
    CHECK(channels[0].stream.url == "https://cdn.example.com/a.m3u8");
    CHECK(channels[1].stream.userAgent.empty());
    CHECK(channels[1].stream.referer.empty());

    // Kodi-style suffix: stripped from the URL and turned into headers
    const auto& kodi = channels[2];
    CHECK(kodi.stream.url == "https://cdn.example.com/c.m3u8");
    CHECK(kodi.stream.userAgent == "KodiUA");
    CHECK(kodi.stream.referer == "https://kodi.example.com/");
    CHECK(kodi.stream.headers.count("Origin") == 1 && kodi.stream.headers.at("Origin") == "https://kodi.example.com");
    // #EXTGRP is used when there is no group-title
    CHECK(kodi.groups.size() == 1 && kodi.groups[0] == "Filmes");
}

static void testRobustness() {
    // CRLF line endings, UTF-8 BOM, blank lines and a URL without #EXTINF
    std::string content = "\xEF\xBB\xBF#EXTM3U\r\n\r\n#EXTINF:-1,Canal CRLF\r\nhttp://a/b.m3u8\r\nhttp://orphan/c.m3u8\r\n";
    auto channels = M3uParser::parse(content);
    CHECK(channels.size() == 2);
    CHECK(channels[0].name == "Canal CRLF");
    CHECK(channels[0].stream.url == "http://a/b.m3u8");
    CHECK(channels[1].name == "http://orphan/c.m3u8");

    CHECK(M3uParser::parse("").empty());
    CHECK(M3uParser::parse("not a playlist").empty());
}

static void testDedupe() {
    auto channel = [](const std::string& name, const std::string& url, std::vector<std::string> groups,
                      const std::string& logo = "") {
        Channel c;
        c.name       = name;
        c.logo       = logo;
        c.groups     = std::move(groups);
        c.stream.url = url;
        return c;
    };
    // Same channel in the Portuguese list (General) and in the News list (News)
    auto merged = M3uParser::dedupe({
        channel("Canal A", "https://a/x.m3u8", {"General"}),
        channel("Canal B", "https://b/x.m3u8", {"Sports"}, "https://b/logo.png"),
        channel("Canal A (news)", "https://a/x.m3u8", {"News", "General"}, "https://a/logo.png"),
        channel("Canal C", "https://c/x.m3u8", {}),
    });
    CHECK(merged.size() == 3);
    CHECK(merged[0].name == "Canal A");  // first occurrence wins, order kept
    CHECK(merged[0].groups.size() == 2 && merged[0].groups[0] == "General" && merged[0].groups[1] == "News");
    CHECK(merged[0].logo == "https://a/logo.png");  // missing logo filled from the duplicate
    CHECK(merged[1].name == "Canal B" && merged[2].name == "Canal C");
}

int main() {
    testDedupe();
    testBasicAttributes();
    testHeaders();
    testRobustness();
    if (failures) std::fprintf(stderr, "%d check(s) failed\n", failures);
    else std::printf("all M3uParser tests passed\n");
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
