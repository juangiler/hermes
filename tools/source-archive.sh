#!/bin/sh
# Archive the current source (including uncommitted edits), never .git/home data.
set -eu
cd "$(dirname "$0")/.."
version=$(sed -n 's/^#define APP_VERSION "\([^"]*\)".*/\1/p' src/app.c)
[ -n "$version" ] || { echo "Missing APP_VERSION" >&2; exit 1; }
out="${1:-dist/hermes-$version.tar.gz}"
mkdir -p "$(dirname "$out")"
tar --exclude='__pycache__' --exclude='*.pyc' \
    --exclude='.git' --exclude='.env' --exclude='.env.*' --exclude='.superpowers' \
    --exclude='docs/checkpoint.md' --exclude='docs/PR-details.md' \
    --transform="s,^,hermes-$version/," -czf "$out" \
    build.sh install.sh README.md LICENSE CHANGELOG.md MAINTENANCE.md \
    src assets packaging third_party tools docs
echo "$out"
