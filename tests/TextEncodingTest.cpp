// Host-only unit tests for the subtitle charset conversion.

#include <cstdio>
#include <cstdlib>
#include <string>

#include "core/TextEncoding.hpp"

static int failures = 0;

#define CHECK(expr)                                                                        \
    do {                                                                                   \
        if (!(expr)) {                                                                     \
            std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #expr); \
            failures++;                                                                    \
        }                                                                                  \
    } while (0)

int main() {
    using namespace TextEncoding;

    // Already UTF-8: unchanged (BOM removed)
    CHECK(toUtf8("Você tem sorte") == "Você tem sorte");
    CHECK(toUtf8("\xEF\xBB\xBFOlá") == "Olá");
    CHECK(isValidUtf8("ação"));

    // ISO-8859-1 / Windows-1252 bytes (as in the Sintel pt .srt): "Você", "ação", curly quotes
    CHECK(!isValidUtf8("Voc\xEA"));
    CHECK(toUtf8("Voc\xEA tem sorte") == "Você tem sorte");
    CHECK(toUtf8("a\xE7\xE3o") == "ação");
    CHECK(toUtf8("\x93" "aspas" "\x94") == "“aspas”");
    CHECK(toUtf8("\x80") == "€");

    // Truncated / invalid sequences are treated as Windows-1252
    CHECK(!isValidUtf8("\xC3"));
    CHECK(!isValidUtf8("\xC0\x80"));
    CHECK(toUtf8("plain ascii") == "plain ascii");

    if (failures) std::fprintf(stderr, "%d check(s) failed\n", failures);
    else std::printf("all text encoding tests passed\n");
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
