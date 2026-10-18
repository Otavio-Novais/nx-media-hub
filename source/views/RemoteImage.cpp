#include "views/RemoteImage.hpp"

#include <algorithm>
#include <borealis/core/cache_helper.hpp>
#include <deque>
#include <map>
#include <mutex>

#include "api/HttpClient.hpp"
#include "core/Background.hpp"

namespace {

const long IMAGE_TIMEOUT = 8;  // seconds
const int MAX_WORKERS    = 1;  // HttpClient serializes requests anyway (see HttpClient.cpp)

// Main thread only: views waiting for each URL
std::map<std::string, std::vector<RemoteImage*>> waiters;

// Shared with the workers: URLs to download (front = newest request)
std::mutex queueMutex;
std::deque<std::string> queue;
int activeWorkers = 0;

void finishDownload(const std::string& url, const HttpResponse& response) {
    auto node = waiters.extract(url);
    if (!response.ok() || response.body.empty()) {
        brls::Logger::warning("image {} failed: {}", url,
                              response.error.empty() ? "HTTP " + std::to_string(response.status) : response.error);
        return;
    }
    // This function holds one temporary reference (getCache() on a hit, addCache() on a new entry)
    auto& cache = brls::TextureCache::instance();
    int texture = cache.getCache(url);
    if (texture <= 0) {
        // Mipmaps: big source images (ex: 960px logos) are drawn at ~100px and look broken without them
        texture = nvgCreateImageMem(brls::Application::getNVGContext(), NVG_IMAGE_GENERATE_MIPMAPS,
                                    (unsigned char*)response.body.data(), (int)response.body.size());
        if (texture == 0) {
            brls::Logger::warning("image {}: unsupported format", url);
            return;
        }
        cache.addCache(url, texture);
    }
    if (!node.empty())
        for (RemoteImage* image : node.mapped()) image->applyCached(url);  // each takes its own reference
    // Release the temporary reference: with no waiter left the texture stays cached until evicted
    cache.removeCache(texture);
}

void workerLoop() {
    while (true) {
        std::string url;
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            if (queue.empty()) {
                activeWorkers--;
                return;
            }
            url = queue.front();
            queue.pop_front();
        }
        HttpResponse response = HttpClient::get(url, {}, IMAGE_TIMEOUT);
        brls::sync([url, response] { finishDownload(url, response); });
    }
}

void enqueue(const std::string& url) {
    std::lock_guard<std::mutex> lock(queueMutex);
    queue.push_front(url);
    if (activeWorkers < MAX_WORKERS) {
        activeWorkers++;
        runInBackground(workerLoop);
    }
}

void unqueue(const std::string& url) {
    std::lock_guard<std::mutex> lock(queueMutex);
    for (auto it = queue.begin(); it != queue.end(); ++it) {
        if (*it == url) {
            queue.erase(it);
            return;
        }
    }
}

}  // namespace

RemoteImage::RemoteImage() {
    // Textures are shared through brls::TextureCache (keyed by URL, reference counted)
    this->setFreeTexture(false);
}

RemoteImage::~RemoteImage() { setImageFromUrl(""); }

void RemoteImage::setImageFromUrl(const std::string& newUrl) {
    // Stop waiting for the previous URL (and drop it from the queue if nobody else wants it)
    if (auto it = waiters.find(url); it != waiters.end()) {
        auto& list = it->second;
        list.erase(std::remove(list.begin(), list.end(), this), list.end());
        if (list.empty()) {
            unqueue(url);
            waiters.erase(it);
        }
    }
    url = newUrl;

    // checkCache() drops our reference to the current texture and reuses a cached texture for `url`
    if (!url.empty() && checkCache(url) > 0) {
        this->invalidateImageBounds();
        return;
    }
    if (this->getTexture() != 0 && url.empty()) brls::TextureCache::instance().removeCache(this->getTexture());
    this->clear();
    if (url.empty()) return;

    auto& list   = waiters[url];
    bool pending = !list.empty();
    list.push_back(this);
    if (!pending) enqueue(url);
}

void RemoteImage::applyCached(const std::string& cachedUrl) {
    if (cachedUrl != url || this->getTexture() != 0) return;
    int texture = brls::TextureCache::instance().getCache(url);  // takes our reference
    if (texture <= 0) return;
    this->innerSetImage(texture);
    // A fixed-size view does not relayout on a new texture: recompute the drawn bounds and paint,
    // otherwise the image is drawn with the previous image's ratio (and its freed texture)
    this->invalidateImageBounds();
}

brls::View* RemoteImage::create() { return new RemoteImage(); }
