// Host-only unit tests for M3uParser (run with ctest from the desktop build).

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

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
    CHECK(globo.tvgId == "globo.br");

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

static void testFilterByCountry() {
    auto channel = [](const std::string& name, const std::string& tvgId) {
        Channel c;
        c.name       = name;
        c.tvgId      = tvgId;
        c.stream.url = "https://x/" + name;
        return c;
    };
    std::vector<std::string> countries = {"br", "pt", "us", "uk", "ca"};
    auto kept = M3uParser::filterByCountry(
        {
            channel("Globo", "Globo.br@SD"),
            channel("CNN", "CNN.us@East"),
            channel("BBC News", "BBCNews.uk@HD"),
            channel("Auction Hunters (Sweden)", "AuctionHunters.se@SD"),  // Swedish channel
            channel("Auction Hunters NO", "AuctionHunters.us@NO"),          // US channel, Norwegian feed
            channel("AMC Europe Romania", "AMCEurope.us@Romania"),           // regional feed by name
            channel("Animal Planet (Poland)", "AnimalPlanet.pl@HD"),
            channel("TV Canada", "TVCanada.ca@CA"),                          // feed of an allowed country
            channel("Lista pessoal", ""),                                    // no tvg-id: kept
        },
        countries);
    std::vector<std::string> names;
    for (const auto& c : kept) names.push_back(c.name);
    CHECK((names == std::vector<std::string>{"Globo", "CNN", "BBC News", "TV Canada", "Lista pessoal"}));

    // Empty list: no filtering
    CHECK(M3uParser::filterByCountry({channel("A", "A.se@SD")}, {}).size() == 1);
}

static bool sameChannels(const std::vector<Channel>& a, const std::vector<Channel>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) {
        if (a[i].name != b[i].name || a[i].logo != b[i].logo || a[i].tvgId != b[i].tvgId ||
            a[i].groups != b[i].groups || a[i].stream.url != b[i].stream.url ||
            a[i].stream.userAgent != b[i].stream.userAgent || a[i].stream.referer != b[i].stream.referer ||
            a[i].stream.headers != b[i].stream.headers)
            return false;
    }
    return true;
}

static void testIncremental() {
    // Any chunk size (lines, the BOM and CRLF cut anywhere) gives the same channels as the whole file
    std::vector<std::pair<std::string, std::string>> playlists = {
        {"basic.m3u", readFixture("basic.m3u")},
        {"headers.m3u", readFixture("headers.m3u")},
        {"BOM + CRLF", "\xEF\xBB\xBF#EXTM3U\r\n\r\n#EXTINF:-1,Canal CRLF\r\nhttp://a/b.m3u8\r\nhttp://orphan/c.m3u8"},
    };
    for (const auto& [fixture, content] : playlists) {
        auto whole = M3uParser::parse(content);
        CHECK(!whole.empty());
        for (size_t size = 1; size <= content.size(); size++) {
            M3uParser::Parser parser;
            for (size_t pos = 0; pos < content.size(); pos += size)
                parser.feed(std::string_view(content).substr(pos, size));
            if (!sameChannels(parser.finish(), whole)) {
                std::fprintf(stderr, "%s: chunks of %zu bytes differ\n", fixture.c_str(), size);
                failures++;
                break;
            }
        }
    }

    // Channel limit: stops at maxChannels and full() tells the caller to stop reading
    M3uParser::Parser limited(2);
    limited.feed(readFixture("basic.m3u"));
    CHECK(limited.full());
    auto two = limited.finish();
    CHECK(two.size() == 2 && two[0].name == "Globo SP");

    // A line longer than any playlist line is dropped, the next ones still parse
    M3uParser::Parser parser;
    parser.feed("#EXTM3U\n" + std::string(100 * 1024, 'x'));
    parser.feed(std::string(100 * 1024, 'x') + "\n#EXTINF:-1,Canal\nhttps://example.com/a.m3u8");
    auto after = parser.finish();
    CHECK(after.size() == 1 && after[0].name == "Canal");
}

static void testSplitQuality() {
    auto check = [](const char* name, const char* base, int height) {
        M3uParser::NameQuality q = M3uParser::splitQuality(name);
        if (q.base != base || q.height != height) {
            std::fprintf(stderr, "splitQuality(\"%s\") = \"%s\" %d, expected \"%s\" %d\n", name, q.base.c_str(),
                         q.height, base, height);
            failures++;
        }
    };
    check("Love Nature (1080p)", "Love Nature", 1080);
    check("Love Nature 4K (2160p) [Geo-blocked]", "Love Nature", 2160);
    check("RTP Acores SD (504p) [Not 24/7]", "RTP Acores", 504);
    check("Al Jazeera English HD (576p)", "Al Jazeera English", 576);  // the explicit height wins over "HD"
    check("A&E FHD H.265", "A&E", 1080);
    check("Canal Full HD", "Canal", 1080);
    check("Canal (HD)", "Canal", 720);
    check("iCarly (United States) SD", "iCarly (United States)", 480);  // "(United States)" is part of the name
    check("Adrenalina Pura TV Brazil", "Adrenalina Pura TV Brazil", 0);
    check("HD", "HD", 0);  // never strips the whole name
    check("TV 4K", "TV", 2160);

    // Other list formats: separators, marks in brackets, combined marks, backup markers
    check("Canal | HD", "Canal", 720);
    check("Canal - FHD", "Canal", 1080);
    check("Canal-HD", "Canal", 720);
    check("Canal [FHD]", "Canal", 1080);
    check("Canal (FHD/H265)", "Canal", 1080);
    check("Canal [HD H.264]", "Canal", 720);
    check("Canal HD+", "Canal", 720);
    check("Canal 1080i", "Canal", 1080);
    check("Canal FHD 60FPS", "Canal", 1080);
    check("Canal HD \xC2\xB9", "Canal", 720);         // "Canal HD ¹"
    check("Canal\xC2\xB2", "Canal", 0);               // "Canal²"
    check("Canal FHD \xE2\x81\xB4", "Canal", 1080);  // "Canal FHD ⁴"
    check("Canal ALT", "Canal", 0);
    check("Canal HD ALT2", "Canal", 720);
    check("Canal SD Backup", "Canal", 480);
    check("Canal \xE2\x80\x93 UHD", "Canal", 2160);  // "Canal – UHD"
    check("Canal UHD (2160p) [Geo-blocked]", "Canal", 2160);
    check("[HD]", "[HD]", 0);
    check("Rede-TV", "Rede-TV", 0);
    check("SporTV 2 HD", "SporTV 2", 720);  // a channel number is part of the name
    check("FunBox UHD SD", "FunBox UHD", 480);  // a second resolution word is part of the name
    check("Love Nature 4K (2160p)", "Love Nature", 2160);
}

static void testGroupVersions() {
    auto channel = [](const std::string& name, const std::string& url, std::vector<std::string> groups = {},
                      const std::string& tvgId = "") {
        Channel c;
        c.name         = name;
        c.tvgId        = tvgId;
        c.groups       = std::move(groups);
        c.stream.url   = url;
        c.stream.title = name;
        return c;
    };
    auto grouped = M3uParser::groupVersions({
        channel("Canal SD", "sd", {"Abertos"}),
        channel("Outro", "outro"),
        channel("Canal 4K (2160p)", "4k"),
        channel("CANAL", "plain"),
        channel("Canal FHD", "fhd", {"Esportes"}),
        channel("Canal HD", "hd"),
        channel("Record (1080p)", "record-sp", {}, "RecordTV.br@SP"),
        channel("Record (720p)", "record-sp-720", {}, "RecordTV.br@RJ"),  // same channel, other feed: merged
        channel("Record (1080p)", "record-pt", {}, "RecordTVPortugal.pt"),  // other channel: kept apart
    });
    CHECK(grouped.size() == 4);

    const Channel& canal = grouped[0];
    CHECK(canal.name == "Canal");
    CHECK((canal.groups == std::vector<std::string>{"Abertos", "Esportes"}));
    std::vector<std::string> urls;
    for (const auto& v : canal.versions()) urls.push_back(v.url);
    // 1080p, 4K, 720p, unknown, SD
    CHECK((urls == std::vector<std::string>{"fhd", "4k", "hd", "plain", "sd"}));
    CHECK(canal.versions()[0].title == "Canal FHD");  // each version keeps its own name for the player

    CHECK(grouped[1].name == "Outro" && grouped[1].alternatives.empty());
    CHECK(grouped[2].name == "Record" && grouped[2].stream.url == "record-sp" && grouped[2].alternatives.size() == 1);
    CHECK(grouped[3].stream.url == "record-pt" && grouped[3].alternatives.empty());
}

int main() {
    testSplitQuality();
    testGroupVersions();
    testIncremental();
    testFilterByCountry();
    testDedupe();
    testBasicAttributes();
    testHeaders();
    testRobustness();
    if (failures) std::fprintf(stderr, "%d check(s) failed\n", failures);
    else std::printf("all M3uParser tests passed\n");
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
