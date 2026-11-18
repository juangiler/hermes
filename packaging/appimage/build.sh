#!/bin/sh
# Opt-in AppImage recipe for a disposable Ubuntu 22.04 build container.
# Not invoked by ordinary builds. Never run this on the host.
# HERMES_APPIMAGE_PROVISION=1 permits dependency installation in that container.
# LINUXDEPLOY_SHA256 must be supplied from a separately verified tool checksum.
set -eu
cd "$(dirname "$0")/../.."
: "${LINUXDEPLOY_SHA256:?Supply a verified linuxdeploy checksum for this architecture}"
[ "${HERMES_APPIMAGE_PROVISION:-0}" = 1 ] || {
    echo "Run only in a disposable Ubuntu build container; set HERMES_APPIMAGE_PROVISION=1 to provision it" >&2
    exit 1
}
[ "$(id -u)" = 0 ] || { echo "Provisioning requires root inside the disposable container" >&2; exit 1; }
[ -e /.dockerenv ] || [ -e /run/.containerenv ] || {
    echo "No container marker found; refusing to provision the host" >&2
    exit 1
}
os=$(. /etc/os-release && printf '%s:%s' "$ID" "$VERSION_ID")
[ "$os" = ubuntu:22.04 ] || { echo "This recipe requires an Ubuntu 22.04 build container" >&2; exit 1; }
ARCH=$(uname -m)
case "$ARCH" in x86_64|aarch64) ;; *) echo "Unsupported architecture: $ARCH" >&2; exit 1 ;; esac
OUT=${OUT:-$PWD/dist}
mkdir -p "$OUT" build/appimage
OUT=$(cd "$OUT" && pwd)
work=$(mktemp -d "$PWD/build/appimage/work.XXXXXX")
DEPS="$work/deps"
appdir="$work/AppDir"
VERSION=${VERSION:-$(sed -n 's/^#define APP_VERSION "\(.*\)".*/\1/p' src/app.c)}
SDL_TAG=${SDL_TAG:-release-3.4.20}
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq --no-install-recommends build-essential cmake ninja-build pkg-config git curl ca-certificates file \
  libtool-bin desktop-file-utils libfreetype-dev libharfbuzz-dev libfontconfig1-dev libarchive-dev libfuse3-dev libssl-dev \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxfixes-dev libxtst-dev libxkbcommon-dev \
  libwayland-dev wayland-protocols libegl1-mesa-dev libgl1-mesa-dev libgles2-mesa-dev libvulkan-dev libdrm-dev libgbm-dev \
  libdecor-0-dev libdbus-1-dev libudev-dev
git clone -q --depth 1 -b "$SDL_TAG" https://github.com/libsdl-org/SDL "$work/sdl"
cmake -S "$work/sdl" -B "$work/sdl/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$DEPS" -DCMAKE_INSTALL_LIBDIR=lib -DSDL_SHARED=ON -DSDL_STATIC=OFF \
  -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_AUDIO=OFF -DSDL_CAMERA=OFF -DSDL_JOYSTICK=OFF \
  -DSDL_HAPTIC=OFF -DSDL_HIDAPI=OFF -DSDL_SENSOR=OFF
cmake --build "$work/sdl/build"
cmake --install "$work/sdl/build"
mkdir "$work/vterm"
curl -fsSLo "$work/vterm.tar.gz" https://launchpad.net/libvterm/trunk/v0.3/+download/libvterm-0.3.3.tar.gz
tar xzf "$work/vterm.tar.gz" -C "$work/vterm" --strip-components=1
make -s -C "$work/vterm" PREFIX="$DEPS"
make -s -C "$work/vterm" PREFIX="$DEPS" install
export PKG_CONFIG_PATH="$DEPS/lib/pkgconfig"
HERMES_OUT="$work/hermes" CFLAGS_PKG="-O2 -g0" LDFLAGS_PKG="-Wl,-rpath,\$ORIGIN/../lib" ./build.sh package
HERMES_BINARY="$work/hermes" DESTDIR="$appdir" PREFIX=/usr ./install.sh
id=io.github.juangiler.hermes
# linuxdeploy must pick the PNG, not an SVG, for the AppImage root icon.
rm "$appdir/usr/share/icons/hicolor/scalable/apps/$id.svg"
curl -fsSLo "$work/linuxdeploy" "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-$ARCH.AppImage"
printf '%s  %s\n' "$LINUXDEPLOY_SHA256" "$work/linuxdeploy" | sha256sum -c -
chmod +x "$work/linuxdeploy"
export APPIMAGE_EXTRACT_AND_RUN=1 LD_LIBRARY_PATH="$DEPS/lib"
export OUTPUT="$OUT/hermes-$VERSION-$ARCH.AppImage"
unset LDAI_UPDATE_INFORMATION
(cd "$OUT" && "$work/linuxdeploy" --appdir "$appdir" -e "$appdir/usr/bin/hermes" \
  -d "$appdir/usr/share/applications/$id.desktop" \
  -i "$appdir/usr/share/icons/hicolor/256x256/apps/$id.png" --output appimage)
(cd "$OUT" && sha256sum "$(basename "$OUTPUT")" > "$(basename "$OUTPUT").sha256")
echo "AppImage: $OUTPUT"
