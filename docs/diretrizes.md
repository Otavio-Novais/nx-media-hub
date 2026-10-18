# Project Guidelines: NX Media Hub (Nintendo Switch Homebrew)

## 1. Visão Geral do Projeto
O **NX Media Hub** é um aplicativo homebrew nativo (`.nro`) de uso estritamente pessoal para o Nintendo Switch (Atmosphere/CNX). O objetivo é unificar em uma única interface otimizada para controle (Joy-Con / Pro Controller) e tela touch:
- **VOD (Filmes e Séries):** Catálogo integrado via API do TMDB/Cinemeta com suporte ao protocolo aberto de Addons (padrão Stremio REST JSON).
- **Canais Ao Vivo & Esportes (IPTV/HLS):** Leitor e organizador nativo de listas `.m3u` / `.m3u8` com categorias e guia de eventos.
- **Reprodução Acelerada por Hardware:** Player de vídeo integrado via `libmpv` com decodificação pela GPU Nvidia Tegra X1 (`nvtegra`), seleção de faixas de áudio (Dublado/ Original) e legendas externas (`.srt` / `.vtt`).

---

## 2. Stack Tecnológica & Dependências (`devkitPro`)

O projeto utiliza a cadeia de compilação oficial `devkitA64` e bibliotecas mantidas via `dkp-pacman`:

| Camada | Tecnologia / Biblioteca | Pacote `dkp-pacman` | Função no Projeto |
| :--- | :--- | :--- | :--- |
| **Linguagem / Build** | C++20 + CMake | `switch-dev`, `devkitA64` | Compilação cruzada para ARM64 e empacotamento `.nro`. |
| **Interface (UI/UX)** | **Borealis** (`brls`) | Submódulo Git | Framework de UI inspirado no Horizon OS com navegação nativa por controle e suporte a tabviews/grids. |
| **Player de Vídeo** | **libmpv** + **FFmpeg** | `switch-libmpv`, `switch-ffmpeg` | Renderização de vídeo OpenGL/deko3d com suporte a HLS (`.m3u8`), MP4, MKV e legendas. |
| **Rede / HTTP** | **libcurl** | `switch-curl`, `switch-mbedtls` | Requisições assíncronas REST, download de listas M3U e customização de headers HTTP. |
| **Parsing JSON** | **nlohmann/json** | Header-only / `switch-nlohmann-json` | Deserialização de respostas do TMDB, protocolo Stremio e configurações locais. |

---

## 3. Arquitetura de Diretórios Sugerida

```text
nx-media-hub/
├── CMakeLists.txt               # Configuração de build para Nintendo Switch
├── resources/                   # Ícones, fontes, temas e traduções (i18n PT-BR)
├── include/
│   ├── api/
│   │   ├── TmdbClient.hpp       # Busca de pôsteres, sinopses e IDs (TMDB/IMDb)
│   │   ├── AddonClient.hpp      # Cliente REST compatível com protocolo Stremio
│   │   └── M3uParser.hpp        # Parser de listas .m3u / .m3u8 para TV e Esportes
│   ├── models/
│   │   ├── MediaItem.hpp        # Structs para Filmes, Séries, Episódios e Canais
│   │   └── StreamSource.hpp     # URL do vídeo, headers HTTP (Referer/UA) e legendas
│   ├── player/
│   │   ├── MpvCore.hpp          # Singleton de controle do libmpv + renderização
│   │   └── PlayerOverlay.hpp    # OSD de controles (Play/Pause, Áudio, Legenda)
│   └── views/
│       ├── HomeTab.hpp          # Destaques e "Continuar Assistindo"
│       ├── CatalogTab.hpp       # Grid de Filmes e Séries com busca
│       ├── LiveTvTab.hpp        # Canais ao vivo e agenda esportiva
│       └── SettingsTab.hpp      # Gerenciamento de URLs de Addons, M3U e API Keys
└── source/                      # Implementações (.cpp) correspondentes
```

---

## 4. Módulos Principais e Contratos de Dados

### 4.1. Estrutura Unificada de Stream (`StreamSource`)
Todo provedor (seja um Addon REST, um servidor local ou um item de lista M3U) deve converter seu resultado para esta estrutura antes de acionar o player:

```cpp
#pragma once
#include <string>
#include <vector>
#include <map>

struct SubtitleTrack {
    std::string id;
    std::string language; // ex: "pt-BR", "por"
    std::string url;      // URL direta para .srt ou .vtt
};

struct StreamSource {
    std::string title;                        // Ex: "1080p - Dublado PT-BR"
    std::string url;                          // Link direto .m3u8 ou .mp4
    std::string userAgent;                    // Opcional: User-Agent exigido pelo CDN
    std::string referer;                      // Opcional: Referer exigido pelo CDN
    std::map<std::string, std::string> headers; // Headers extras para o libmpv
    std::vector<SubtitleTrack> subtitles;     // Legendas externas anexadas
    bool isLive = false;                      // Define se esconde a barra de progresso
};
```

### 4.2. Módulo 1: Integração VOD (TMDB + Protocolo Stremio)
1. **Catálogo e Metadados:**
   - Consome `https://api.themoviedb.org/3` com `language=pt-BR` para exibir títulos traduzidos, capas (`poster_path`), fundos (`backdrop_path`) e obter o `imdb_id` (ex: `tt1234567`).
2. **Resolução de Streams (Padrão Stremio Addon):**
   - Filmes: `GET {addon_base_url}/stream/movie/{imdb_id}.json`
   - Séries: `GET {addon_base_url}/stream/series/{imdb_id}:{season}:{episode}.json`
   - O parser lê o array `streams[]`, extrai a chave `url`, `title`/`description` e eventuais `behaviorHints.proxyHeaders.request` (para preencher `Referer` e `User-Agent` automaticamente no `StreamSource`).

### 4.3. Módulo 2: Canais Ao Vivo e Esportes (`M3uParser`)
1. Faz download de uma ou mais listas `.m3u` / `.m3u8` configuradas em `/switch/nx-media-hub/config.json`.
2. Faz o parse linha a linha extraindo os atributos da diretiva `#EXTINF`:
   - `tvg-name` ou título após a vírgula $\rightarrow$ Nome do canal/evento.
   - `tvg-logo` $\rightarrow$ Ícone do canal (carregado via cache assíncrono).
   - `group-title` $\rightarrow$ Categoria (ex: `"Esportes"`, `"Canais Abertos"`, `"Notícias"`).
   - `#EXTVLCOPT:http-user-agent=...` e `#EXTVLCOPT:http-referrer=...` $\rightarrow$ Mapeados automaticamente para o `StreamSource` caso a lista utilize cabeçalhos customizados.

### 4.4. Módulo 3: Configuração do `libmpv` no Switch
Para garantir reprodução fluida em 1080p60 sem travar a thread principal da interface gráfica:
- **Hardware Decoding:** Usar `hwdec=nvtegra-copy` (NVDEC do Tegra). O libmpv OpenGL não tem interop `nvtegra` (só o build deko3d); com `hwdec=nvtegra` o mpv cai silenciosamente para decodificação por software.
- **Cache de Rede:** Configurar `demuxer-max-bytes` (ex: `64MiB`) e `cache=yes` para evitar engasgos no Wi-Fi do Switch.
- **Injeção de Headers:** Antes do comando `loadfile`, aplicar os cabeçalhos presentes no `StreamSource`:
  ```cpp
  if (!source.referer.empty()) {
      mpv_set_property_string(mpv, "referrer", source.referer.c_str());
  }
  if (!source.userAgent.empty()) {
      mpv_set_property_string(mpv, "user-agent", source.userAgent.c_str());
  }
  ```
- **Legendas Externas:** Após o evento `MPV_EVENT_FILE_LOADED`, iterar sobre `source.subtitles` executando o comando `sub-add <url> select`.

---

## 5. Persistência no Cartão SD
Todos os dados de usuário ficam salvos localmente em `/switch/nx-media-hub/` para facilitar backup e edição via USB (DBI MTP):
- `config.json`: Chave da API do TMDB, lista de URLs de Addons HTTP e URLs de listas `.m3u`.
- `history.json`: Salva o `imdb_id`, episódio atual e o `time-pos` (segundo exato em que o vídeo parou no `libmpv`) para retomar de onde parou.
- `favorites.json`: Lista rápida de filmes, séries e canais favoritados.

---

## 6. Fluxo de Desenvolvimento e Testes (Linux + Wi-Fi)

### 6.1. Compilação via Docker (Opcional — Sem instalar pacotes no host)
```bash
docker run --rm -v "$(pwd):/project" -w /project devkitpro/devkita64:latest bash -c "
  cmake -B build -DCMAKE_BUILD_TYPE=Release &&
  cmake --build build -j\$(nproc)
"
```

### 6.2. Compilação Nativa e Envio Direto via `nxlink`
1. No Switch, abra o **Homebrew Menu** (Modo Applet Override — segurando **R** ao abrir um jogo) e pressione **Y** para ativar o *NetLoader*.
2. No terminal Linux:
   ```bash
   cmake -B build && cmake --build build -j$(nproc)
   nxlink -s -a <IP_DO_SWITCH> build/nx-media-hub.nro
   ```

---

## 7. Regras de Ouro para o Assistente de IA (Prompt Guidelines)
1. **Gerenciamento de Memória e Modo Applet:** Sempre verifique se o usuário abriu o app com acesso total à RAM (`appletGetAppletType() == AppletType_Application`). Caso esteja em modo álbum (Applet), exiba um aviso na UI sugerindo abrir segurando o botão `R` em um jogo.
2. **Threads e UI:** Nunca execute chamadas `curl_easy_perform()` na thread principal do Borealis (`brls::Application`). Utilize `brls::async` para chamadas de rede e `brls::sync` para atualizar a interface com os dados recebidos.
3. **Prevenção de Sleep Mode:** Enquanto o `libmpv` estiver reproduzindo vídeo (`core-idle == false`), chame `appletSetMediaPlaybackState(true)` da `libnx` para impedir que a tela do Switch escureça ou entre em modo de descanso durante o filme.
4. **Compatibilidade HOS Moderna:** Utilize sempre as assinaturas atuais da `libnx` (compatíveis com Atmosphere 1.7+ / Firmware 18.0+ e 19.0+), especialmente nas chamadas de `padConfigureInput` e `PadState`.

---

## 8. Roadmap de Implementação (Fases)
- [x] **Fase 1 (Base & Player):** Configurar template CMake com Borealis + `libmpv` e reproduzir um vídeo `.m3u8` de teste com controles de Play/Pause, Seek e troca de áudio/legenda.
- [ ] **Fase 2 (TV Ao Vivo & Esportes):** Implementar `M3uParser`, carregar lista do cartão SD/URL e criar a aba `LiveTvTab` dividida por categorias.
- [ ] **Fase 3 (Catálogo VOD):** Integrar `TmdbClient` com busca em português, telas de detalhes de filmes e seletor de temporadas/episódios para séries.
- [ ] **Fase 4 (Provedores / Addons):** Implementar `AddonClient` para buscar as streams de um filme/episódio e abrir o modal de seleção de fonte.
- [ ] **Fase 5 (Polimento):** Implementar histórico ("Continuar Assistindo"), favoritos e arquivo `config.json` editável.