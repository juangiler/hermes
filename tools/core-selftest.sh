#!/bin/sh
# Standalone engine self-test: compiles tools/core-selftest.c without CMake.
# Uses the same command as the source header so local runs and CI match.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
out=${HERMES_CORE_TEST_BIN:-/tmp/hermes-core-test}

cd "$root"
cc -std=gnu11 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter \
    -Wno-format-truncation tools/core-selftest.c -o "$out" \
    -lm -ldl -pthread $(pkg-config --cflags --libs libarchive fuse3 openssl)
"$out"
