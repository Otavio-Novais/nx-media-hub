#!/bin/bash
# Builds nx-media-hub.nro inside the devkitPro container:
#   docker run --rm -v "$(pwd):/data" devkitpro/devkita64:20251117 bash /data/scripts/build_switch.sh [cmake options]
# Release (movies hidden): ... build_switch.sh -DENABLE_MOVIES=OFF
set -e

BUILD_DIR=cmake-build-switch

cd "$(dirname "$0")/.."
git config --global --add safe.directory "$(pwd)"

# ffmpeg (with nvtegra hwdec) and libmpv prebuilt by the wiliwili project
BASE_URL="https://github.com/xfangfang/wiliwili/releases/download/v0.1.0/"
PKGS=(
    "switch-ffmpeg-7.1-1-any.pkg.tar.zst"
    "switch-libmpv-0.36.0-3-any.pkg.tar.zst"
)
mkdir -p .pkg-cache
for PKG in "${PKGS[@]}"; do
    [ -f ".pkg-cache/${PKG}" ] || curl -L -o ".pkg-cache/${PKG}" "${BASE_URL}${PKG}"
    dkp-pacman -U --noconfirm --needed ".pkg-cache/${PKG}"
done

cmake -B ${BUILD_DIR} -DPLATFORM_SWITCH=ON -DCMAKE_BUILD_TYPE=Release "$@"
make -C ${BUILD_DIR} nx-media-hub.nro -j"$(nproc)"

# the container runs as root: hand the outputs back to the repo owner
chown -R "$(stat -c %u:%g .)" ${BUILD_DIR} .pkg-cache
