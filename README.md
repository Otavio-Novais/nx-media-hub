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
./build-desktop/nx-media-hub -d
```

## Enviar ao Switch
No Switch: abra o Homebrew Menu em modo título (segure **R** ao abrir um jogo) e pressione **Y** (NetLoader).
```bash
docker run --rm --network host -v "$(pwd):/data" devkitpro/devkita64:20251117 \
  nxlink -s -a <IP_DO_SWITCH> /data/cmake-build-switch/nx-media-hub.nro
```
Ou copie o `.nro` para `/switch/nx-media-hub/` no cartão SD.
