# NX Media Hub

Homebrew `.nro` pessoal para Nintendo Switch (Atmosphere): VOD (TMDB + addons Stremio), TV ao vivo (M3U) e player libmpv com `hwdec=nvtegra`.
Diretrizes e roadmap: [`docs/diretrizes.md`](docs/diretrizes.md).

## Clonar
```bash
git clone --recursive <repo>      # ou: git submodule update --init --recursive
```

## Build — Nintendo Switch (Docker)
```bash
docker run --rm -v "$(pwd):/data" devkitpro/devkita64:20251117 bash /data/scripts/build_switch.sh
# saída: cmake-build-switch/nx-media-hub.nro
```

## Build — Desktop (Linux, para desenvolvimento)
Dependências: `pkg-config libwayland-dev wayland-protocols libxkbcommon-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libdbus-1-dev libmpv-dev libcurl4-openssl-dev`
```bash
cmake -B build-desktop -DPLATFORM_DESKTOP=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-desktop -j$(nproc)
./build-desktop/nx-media-hub -d            # -p <url> abre o player direto
ctest --test-dir build-desktop               # testes do parser M3U
```

## Enviar ao Switch
No Switch: abra o Homebrew Menu em modo título (segure **R** ao abrir um jogo) e pressione **Y** (NetLoader).
```bash
docker run --rm --network host -v "$(pwd):/data" devkitpro/devkita64:20251117 \
  nxlink -s -a <IP_DO_SWITCH> /data/cmake-build-switch/nx-media-hub.nro
```
Ou copie o `.nro` para `/switch/nx-media-hub/` no cartão SD.

## Configuração
Na primeira execução o app cria `config.json` em `sdmc:/switch/nx-media-hub/` (desktop: `~/.config/nx-media-hub/`):
```json
{ "tmdbApiKey": "", "addons": [], "m3uUrls": ["https://iptv-org.github.io/iptv/countries/br.m3u"] }
```
`m3uUrls` aceita URLs ou caminhos locais (ex.: `sdmc:/switch/nx-media-hub/lista.m3u`).
