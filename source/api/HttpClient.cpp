#include "api/HttpClient.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <condition_variable>
#include <functional>
#include <mutex>

namespace {

// One request at a time: concurrent TLS handshakes from several threads are the suspected cause of
// intermittent native crashes on the Switch (curl's mbedtls backend may lack thread support there).
// A plain mutex is not fair: the image worker re-locked it right away and lists waited behind dozens of
// images, so Normal requests go first.
std::mutex turnMutex;
std::condition_variable turnChanged;
bool busy          = false;
int normalsWaiting = 0;

const long STREAM_TIMEOUT = 300;  // seconds, see getStream()

class Turn {
public:
    explicit Turn(HttpClient::Priority priority) {
        std::unique_lock<std::mutex> lock(turnMutex);
        bool normal = priority == HttpClient::Priority::Normal;
        if (normal) normalsWaiting++;
        turnChanged.wait(lock, [normal] { return !busy && (normal || normalsWaiting == 0); });
        if (normal) normalsWaiting--;
        busy = true;
    }
    ~Turn() {
        {
            std::lock_guard<std::mutex> lock(turnMutex);
            busy = false;
        }
        turnChanged.notify_all();
    }
};

size_t writeBody(char* data, size_t size, size_t count, void* userdata) {
    static_cast<std::string*>(userdata)->append(data, size * count);
    return size * count;
}

struct StreamTarget {
    CURL* curl;
    const std::function<bool(std::string_view)>* onData;
    bool stopped = false;  // onData asked to stop: curl reports a write error, but nothing failed
};

size_t writeStream(char* data, size_t size, size_t count, void* userdata) {
    auto* target = static_cast<StreamTarget*>(userdata);
    long status  = 0;
    curl_easy_getinfo(target->curl, CURLINFO_RESPONSE_CODE, &status);
    // An error page (404 HTML...) is not a playlist: drop it, the caller sees the status
    if (status < 200 || status >= 300) return size * count;
    if (!(*target->onData)(std::string_view(data, size * count))) {
        target->stopped = true;
        return 0;  // aborts the transfer
    }
    return size * count;
}

void setCommonOptions(CURL* curl, const std::string& url) {
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "nx-media-hub/0.1");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");  // any encoding curl supports (gzip...)
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
#ifdef __SWITCH__
    // The Switch has no CA store reachable by curl/mbedtls (same default as wiliwili):
    // HTTPS is encrypted but the server certificate is not verified
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
#endif
}

}  // namespace

namespace HttpClient {

void init() { curl_global_init(CURL_GLOBAL_DEFAULT); }

std::string redact(const std::string& url) {
    size_t scheme = url.find("://");
    if (scheme == std::string::npos) return url.substr(0, url.find('/'));
    size_t pathStart = url.find_first_of("/?#", scheme + 3);
    if (pathStart == std::string::npos) return url;
    return url.substr(0, pathStart) + "/…";
}

HttpResponse get(const std::string& url, const std::map<std::string, std::string>& headers, long timeoutSeconds,
                 Priority priority) {
    Turn turn(priority);
    HttpResponse response;
    CURL* curl = curl_easy_init();
    if (!curl) {
        response.error = "curl_easy_init failed";
        return response;
    }

    curl_slist* headerList = nullptr;
    for (const auto& [key, value] : headers) headerList = curl_slist_append(headerList, (key + ": " + value).c_str());

    setCommonOptions(curl, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, std::min(timeoutSeconds, 10L));
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSeconds);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);

    CURLcode code = curl_easy_perform(curl);
    if (code != CURLE_OK) response.error = curl_easy_strerror(code);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);

    curl_slist_free_all(headerList);
    curl_easy_cleanup(curl);
    return response;
}

HttpResponse getStream(const std::string& url, const std::function<bool(std::string_view)>& onData,
                       long stallSeconds, Priority priority) {
    Turn turn(priority);
    HttpResponse response;
    CURL* curl = curl_easy_init();
    if (!curl) {
        response.error = "curl_easy_init failed";
        return response;
    }

    StreamTarget target{curl, &onData};
    setCommonOptions(curl, url);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, STREAM_TIMEOUT);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, stallSeconds);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeStream);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &target);

    CURLcode code = curl_easy_perform(curl);
    if (code != CURLE_OK && !target.stopped) response.error = curl_easy_strerror(code);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);

    curl_easy_cleanup(curl);
    return response;
}

}  // namespace HttpClient
