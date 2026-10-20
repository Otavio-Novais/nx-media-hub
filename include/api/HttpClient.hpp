#pragma once

#include <map>
#include <string>

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

// "https://host/secret/path?x=1" -> "https://host/…": for logs, since addon URLs carry API keys in the path
std::string redact(const std::string& url);

}  // namespace HttpClient
