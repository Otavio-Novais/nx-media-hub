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

// Call once at startup, before any request.
void init();

// Blocking GET: never call on the UI thread, wrap it in brls::async.
HttpResponse get(const std::string& url, const std::map<std::string, std::string>& headers = {},
                 long timeoutSeconds = 30);

}  // namespace HttpClient
