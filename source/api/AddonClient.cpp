#include "api/AddonClient.hpp"

#include <borealis/core/logger.hpp>

#include "api/HttpClient.hpp"

namespace AddonClient {

std::vector<StreamSource> fetchStreams(const std::vector<std::string>& addons, MediaType type,
                                       const std::string& imdbId, int season, int episode, std::string& errors) {
    std::vector<StreamSource> all;
    std::string path = "/stream/" + streamPath(type, imdbId, season, episode) + ".json";
    for (const auto& addon : addons) {
        std::string base      = normalizeBaseUrl(addon);
        HttpResponse response = HttpClient::get(base + path);
        std::vector<StreamSource> streams;
        if (!response.ok() || !parseStreams(response.body, streams)) {
            std::string error = !response.error.empty() ? response.error
                                : !response.ok()        ? "HTTP " + std::to_string(response.status)
                                                        : "resposta inválida";
            brls::Logger::warning("addon {}: {}", base, error);
            errors += base + ": " + error + "\n";
            continue;
        }
        brls::Logger::info("addon {}: {} streams for {}", base, streams.size(), path);
        std::move(streams.begin(), streams.end(), std::back_inserter(all));
    }
    return all;
}

}  // namespace AddonClient
