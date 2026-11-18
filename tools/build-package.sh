#!/bin/sh
# Build local Debian/Ubuntu or Fedora packages without modifying the host.
# Prerequisites must already be installed in the chosen build environment.
set -eu
cd "$(dirname "$0")/.."
kind="${1:-}"
case "$kind" in
    deb) command -v dpkg-buildpackage >/dev/null || { echo "Needs dpkg-buildpackage and debhelper" >&2; exit 1; } ;;
    rpm) command -v rpmbuild >/dev/null || { echo "Needs rpmbuild" >&2; exit 1; } ;;
    *) echo "Usage: sh tools/build-package.sh deb|rpm" >&2; exit 2 ;;
esac
sh tools/check-release-version.sh
version=$(sed -n 's/^#define APP_VERSION "\([^"]*\)".*/\1/p' src/app.c)
mkdir -p build/packaging dist
work=$(mktemp -d "$PWD/build/packaging/$kind.XXXXXX")
archive="$work/hermes-$version.tar.gz"
sh tools/source-archive.sh "$archive"
if [ "$kind" = deb ]; then
    tar -xzf "$archive" -C "$work"
    source="$work/hermes-$version"
    cp -R packaging/debian "$source/debian"
    chmod +x "$source/debian/rules"
    (cd "$source" && dpkg-buildpackage -us -uc -b)
    for artifact in "$work"/*.deb "$work"/*.buildinfo "$work"/*.changes; do
        [ ! -f "$artifact" ] || cp "$artifact" dist/
    done
else
    mkdir -p "$work/SOURCES" "$work/SPECS"
    cp "$archive" "$work/SOURCES/"
    cp packaging/rpm/hermes.spec "$work/SPECS/"
    rpmbuild --define "_topdir $work" -ba "$work/SPECS/hermes.spec"
    find "$work/RPMS" "$work/SRPMS" -type f -name '*.rpm' -exec cp {} dist/ \;
fi
echo "Package artifacts: $PWD/dist (build workspace retained at $work)"
