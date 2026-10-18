#include "core/TextEncoding.hpp"

#include <cstdint>

namespace {

// Windows-1252 code points for bytes 0x80-0x9F (0 = undefined, kept as U+FFFD)
const uint16_t CP1252_HIGH[32] = {0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
                                  0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0,      0x017D, 0,
                                  0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
                                  0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178};

void appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += (char)cp;
    } else if (cp < 0x800) {
        out += (char)(0xC0 | (cp >> 6));
        out += (char)(0x80 | (cp & 0x3F));
    } else {
        out += (char)(0xE0 | (cp >> 12));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    }
}

}  // namespace

namespace TextEncoding {

bool isValidUtf8(const std::string& text) {
    size_t i = 0, n = text.size();
    while (i < n) {
        auto c   = (unsigned char)text[i];
        int len  = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        if (len == 0 || i + len > n) return false;
        if (len == 2 && c < 0xC2) return false;  // overlong
        for (int k = 1; k < len; k++)
            if (((unsigned char)text[i + k] >> 6) != 0x2) return false;
        i += len;
    }
    return true;
}

std::string toUtf8(const std::string& text) {
    if (isValidUtf8(text)) return text.rfind("\xEF\xBB\xBF", 0) == 0 ? text.substr(3) : text;
    std::string out;
    out.reserve(text.size() + text.size() / 8);
    for (unsigned char c : text) {
        if (c < 0x80) out += (char)c;
        else if (c < 0xA0) appendUtf8(out, CP1252_HIGH[c - 0x80] ? CP1252_HIGH[c - 0x80] : 0xFFFD);
        else appendUtf8(out, c);  // 0xA0-0xFF map to the same code points (Latin-1)
    }
    return out;
}

}  // namespace TextEncoding
