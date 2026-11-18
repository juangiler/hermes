#!/bin/sh
# Build Hermes. Usage: ./build.sh [release|debug|package|demo]
# HERMES_ADDON_SRC=<dir>: optional add-on sources; HERMES_OUT=<file>: output override.
# Legacy FE_ADDON_SRC and FE_OUT remain accepted; HERMES_* takes precedence.
# Both absolute paths.
set -e
cd "$(dirname "$0")"
mode="${1:-release}"
ADDON=""
SRCS="src/*.c src/*.h"
addon_src="${HERMES_ADDON_SRC:-${FE_ADDON_SRC:-}}"
if [ -n "$addon_src" ]; then ADDON="-DFE_ADDON -I$addon_src"; SRCS="$SRCS $addon_src/*.c"; fi
case "$mode" in
  release) OPT="-O2 -march=native -DNDEBUG -flto=auto"; binary=hermes ;;
  package) OPT="${CFLAGS_PKG:--O2} -DNDEBUG"; binary=hermes ;;
  debug) OPT="-O0 -g3 -fsanitize=address,undefined -fno-omit-frame-pointer"; binary=hermes ;;
  demo) OPT="-O2 -DNDEBUG -DFE_DEMO"; binary=hermes-demo ;;
  *) echo "unknown mode: $mode" >&2; exit 1 ;;
esac
out="${HERMES_OUT:-${FE_OUT:-build/$mode/$binary}}"
mkdir -p "$(dirname "$out")"
PKGS="sdl3 freetype2 harfbuzz fontconfig libarchive vterm fuse3 openssl"
pkg-config --atleast-version=3.2 sdl3
pkg-config --atleast-version=0.3 vterm
pkg-config --atleast-version=3.0 openssl
CFLAGS="-std=gnu11 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Wno-format-truncation $(pkg-config --cflags $PKGS)"
LIBS="$(pkg-config --libs $PKGS) -lpthread -lm -ldl"
# Source must be pure ASCII: UTF-8 text goes in as \x escapes (catches mangled characters).
# shellcheck disable=SC2086
if LC_ALL=C grep -nP '[^\x00-\x7F]' $SRCS; then echo "error: non-ASCII characters in source (use \\x escapes)"; exit 1; fi
${CC:-cc} $CFLAGS $ADDON $OPT src/main.c -o "$out" $LIBS ${LDFLAGS_PKG:-}
echo "built $out ($mode${ADDON:+, with add-ons})"
