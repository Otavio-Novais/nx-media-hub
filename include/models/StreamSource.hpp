#pragma once

#include <map>
#include <string>
#include <vector>

struct SubtitleTrack {
    std::string id;
    std::string language;  // ex: "pt-BR", "por"
    std::string url;       // URL direta para .srt ou .vtt
};

// Every provider (addon, M3U entry, ...) converts its result to this before opening the player.
struct StreamSource {
    std::string title;                           // Ex: "1080p - Dublado PT-BR"
    std::string url;                             // Link direto .m3u8 ou .mp4
    std::string userAgent;                       // Opcional: User-Agent exigido pelo CDN
    std::string referer;                         // Opcional: Referer exigido pelo CDN
    std::map<std::string, std::string> headers;  // Headers extras para o libmpv
    std::vector<SubtitleTrack> subtitles;        // Legendas externas anexadas
    bool isLive = false;                         // Define se esconde a barra de progresso
};
