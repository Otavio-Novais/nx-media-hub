#include "api/HttpClient.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <mutex>

namespace {

// One request at a time: concurrent TLS handshakes from several threads are the suspected cause of
// intermittent native crashes on the Switch (curl's mbedtls backend may lack thread support there)
std::mutex requestMutex;

size_t writeBody(char* data, size_t size, size_t count, void* userdata) {
    static_cast<std::string*>(userdata)->append(data, size * count);
    return size * count;
}

}  // namespace

namespace HttpClient {

void init() { curl_global_init(CURL_GLOBAL_DEFAULT); }

HttpResponse get(const std::string& url, const std::map<std::string, std::string>& headers, long timeoutSeconds) {
    std::lock_guard<std::mutex> lock(requestMutex);
    HttpResponse response;
    CURL* curl = curl_easy_init();
    if (!curl) {
        response.error = "curl_easy_init failed";
        return response;
    }

    curl_slist* headerList = nullptr;
    for (const auto& [key, value] : headers) headerList = curl_slist_append(headerList, (key + ": " + value).c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "nx-media-hub/0.1");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");  // any encoding curl supports (gzip...)
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, std::min(timeoutSeconds, 10L));
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSeconds);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
#ifdef __SWITCH__
    // The Switch has no CA store reachable by curl/mbedtls (same default as wiliwili):
    // HTTPS is encrypted but the server certificate is not verified
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
#endif

    CURLcode code = curl_easy_perform(curl);
    if (code != CURLE_OK) response.error = curl_easy_strerror(code);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);

    curl_slist_free_all(headerList);
    curl_easy_cleanup(curl);
    return response;
}

}  // namespace HttpClient
