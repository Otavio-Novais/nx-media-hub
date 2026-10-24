#pragma once

#include <functional>
#include <map>
#include <string>
#include <string_view>

struct HttpResponse {
    long status = 0;
    std::string body;
    std::string error;  // empty on transport success (check status for HTTP errors)

    bool ok() const { return error.empty() && status >= 200 && status < 300; }
};

namespace HttpClient {

// Requests run one at a time; a Background one (images) only starts when no Normal one is waiting
enum class Priority { Normal, Background };

// Call once at startup, before any request.
void init();

// Blocking GET: never call on the UI thread, wrap it in brls::async.
HttpResponse get(const std::string& url, const std::map<std::string, std::string>& headers = {},
                 long timeoutSeconds = 30, Priority priority = Priority::Normal);

// Blocking GET for big bodies (M3U lists): a 2xx body goes to onData chunk by chunk as it arrives, never into
// HttpResponse::body. onData returns false to stop the download early, which is not an error. It gives up when
// less than 1 KB/s arrives for stallSeconds, so a big list on a slow but steady connection still finishes, and
// after STREAM_TIMEOUT in any case: requests run one at a time, nothing else could load meanwhile.
HttpResponse getStream(const std::string& url, const std::function<bool(std::string_view)>& onData,
                       long stallSeconds = 20, Priority priority = Priority::Normal);

// "https://host/secret/path?x=1" -> "https://host/…": for logs, since addon URLs carry API keys in the path
std::string redact(const std::string& url);

}  // namespace HttpClient
