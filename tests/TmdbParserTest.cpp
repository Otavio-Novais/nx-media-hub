// Host-only unit tests for the TMDB JSON parsing (fixtures are trimmed real pt-BR responses).

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "api/TmdbClient.hpp"

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

static void testSearchResults() {
    TmdbClient::ResultPage page;
    CHECK(TmdbClient::parseResults(readFixture("tmdb_search.json"), page));
    CHECK(page.page == 1 && page.totalPages > 1);
    const auto& items = page.items;
    // 3 movies/series + 1 person in the fixture: the person is skipped
    CHECK(items.size() == 3);
    CHECK(items[0].type == MediaType::Movie);
    CHECK(items[0].tmdbId == 603);
    CHECK(items[0].title == "Matrix");
    CHECK(items[0].year == "1999");
    CHECK(!items[0].posterPath.empty() && items[0].posterPath[0] == '/');
    CHECK(items[0].rating > 0);
}

static void testMovieDetails() {
    MediaDetails details;
    CHECK(TmdbClient::parseDetails(readFixture("tmdb_movie.json"), MediaType::Movie, details));
    CHECK(details.item.title == "Matrix");
    CHECK(details.imdbId == "tt0133093");
    CHECK(details.runtime == 136);
    CHECK(details.genres.size() == 2 && details.genres[0] == "Ação");
    CHECK(details.seasons.empty());
}

static void testSeriesDetails() {
    MediaDetails details;
    CHECK(TmdbClient::parseDetails(readFixture("tmdb_tv.json"), MediaType::Series, details));
    CHECK(details.item.title == "Breaking Bad");
    CHECK(details.item.year == "2008");
    CHECK(details.imdbId == "tt0903747");
    CHECK(details.seasons.size() == 6);
    CHECK(details.seasons[0].number == 0 && details.seasons[0].name == "Especiais");
    CHECK(details.seasons[1].number == 1 && details.seasons[1].episodeCount == 7);
}

static void testSeason() {
    std::vector<Episode> episodes;
    CHECK(TmdbClient::parseSeason(readFixture("tmdb_season.json"), episodes));
    CHECK(episodes.size() == 3);
    CHECK(episodes[0].season == 1 && episodes[0].number == 1);
    CHECK(episodes[0].name == "Piloto");
    CHECK(episodes[0].runtime == 59);
}

static void testInvalidInput() {
    TmdbClient::ResultPage items;
    MediaDetails details;
    std::vector<Episode> episodes;
    CHECK(!TmdbClient::parseResults("not json", items));
    CHECK(!TmdbClient::parseResults("{\"status_message\":\"Invalid API key\"}", items));
    CHECK(!TmdbClient::parseDetails("[]", MediaType::Movie, details));
    CHECK(!TmdbClient::parseSeason("{}", episodes));
    CHECK(TmdbClient::imageUrl("", TmdbClient::ImageSize::Poster).empty());
    CHECK(TmdbClient::imageUrl("/a.jpg", TmdbClient::ImageSize::Poster) == "https://image.tmdb.org/t/p/w342/a.jpg");
}

int main() {
    testSearchResults();
    testMovieDetails();
    testSeriesDetails();
    testSeason();
    testInvalidInput();
    if (failures) std::fprintf(stderr, "%d check(s) failed\n", failures);
    else std::printf("all TMDB parser tests passed\n");
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
