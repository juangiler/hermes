#!/bin/sh
# Sync the checked-in package metadata after bumping APP_VERSION and CHANGELOG.md.
set -eu
cd "$(dirname "$0")/.."

version=$(sed -n 's/^#define APP_VERSION "\([^"]*\)".*/\1/p' src/app.c)
if [ -z "$version" ] || ! grep -Fxq "## $version" CHANGELOG.md; then
    echo "Set APP_VERSION and add a matching CHANGELOG.md section first" >&2
    exit 1
fi

sed -i "s/^Version:[[:space:]]*.*/Version:        $version/" packaging/rpm/hermes.spec
debian_version=$(sed -n '1s/^hermes (\([^)]*\)).*/\1/p' packaging/debian/changelog)
case "$debian_version" in
    "$version"-[1-9]*) ;;
    *) debian_version="$version-1" ;;
esac
if ! head -n 1 packaging/debian/changelog | grep -Fxq "hermes ($debian_version) unstable; urgency=medium"; then
    tmp=$(mktemp)
    trap 'rm -f "$tmp"' EXIT HUP INT TERM
    {
        printf 'hermes (%s) unstable; urgency=medium\n\n' "$debian_version"
        printf '  * Release %s.\n\n' "$version"
        printf ' -- juangiler <juangiler@users.noreply.github.com>  %s\n\n' "$(date -R)"
        cat packaging/debian/changelog
    } > "$tmp"
    cat "$tmp" > packaging/debian/changelog
fi

sh tools/check-release-version.sh
