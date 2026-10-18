#pragma once

#include <string>

namespace TextEncoding {

bool isValidUtf8(const std::string& text);

// Returns `text` as UTF-8: unchanged when it already is valid UTF-8, otherwise decoded as Windows-1252
// (superset of ISO-8859-1, the usual encoding of Brazilian .srt files). A UTF-8 BOM is removed.
// Needed because the Switch libmpv is built without iconv and cannot convert subtitle charsets.
std::string toUtf8(const std::string& text);

}  // namespace TextEncoding
