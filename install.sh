#!/bin/sh
# Install Hermes. Usage: [DESTDIR=...] [PREFIX=/usr/local] ./install.sh
# Build ./build.sh package first. Override with HERMES_BUILD_MODE or HERMES_BINARY.
set -e
cd "$(dirname "$0")"
PREFIX="${PREFIX:-/usr/local}"
DEST="${DESTDIR:-}"
case "$PREFIX" in
  /*) ;;
  *) echo "PREFIX must be absolute" >&2; exit 1 ;;
esac
# These cannot be represented safely in both Desktop Exec and policy XML.
case "$PREFIX" in
  *'"'*|*'\'*|*'&'*|*'<'*|*'>'*|*'|'*|*'%'*|*'$'*|*'`'*|*'
'*) echo "PREFIX contains unsupported characters" >&2; exit 1 ;;
esac
mode="${HERMES_BUILD_MODE:-package}"
case "$mode" in
  debug|release|package) ;;
  *) echo "invalid HERMES_BUILD_MODE: $mode" >&2; exit 1 ;;
esac
binary="${HERMES_BINARY:-build/$mode/hermes}"
id=io.github.juangiler.hermes
[ -x "$binary" ] || { echo "$binary missing: run ./build.sh $mode first" >&2; exit 1; }
install -Dm755 "$binary" "$DEST$PREFIX/bin/hermes"
install -d "$DEST$PREFIX/share/applications"
# Bind this launcher to this installation; another copy earlier in PATH must
# not replace the binary selected by PREFIX (including Distrobox exports).
HERMES_INSTALL_BINARY="$PREFIX/bin/hermes" awk '
    /^Exec=hermes %F$/ {
        printf "Exec=\"%s\" %%F\n", ENVIRON["HERMES_INSTALL_BINARY"]
        next
    }
    { print }
' assets/desktop/$id.desktop > "$DEST$PREFIX/share/applications/$id.desktop"
policy=$(mktemp)
trap 'rm -f "$policy"' EXIT HUP INT TERM
sed "s|@BINDIR@|$PREFIX/bin|" assets/desktop/$id.policy > "$policy"
install -Dm644 "$policy" "$DEST$PREFIX/share/polkit-1/actions/$id.policy"
# phones and cameras over USB for the user at the seat (udev only reads /usr/lib and /etc)
UDEV_DIR="$PREFIX/lib/udev/rules.d"; [ "$PREFIX" = /usr ] || UDEV_DIR=/etc/udev/rules.d
install -Dm644 assets/udev/70-hermes-phones.rules "$DEST$UDEV_DIR/70-hermes-phones.rules"
install -Dm644 assets/branding/hermes.svg "$DEST$PREFIX/share/icons/hicolor/scalable/apps/$id.svg"
for size in 16 24 32 48 64 128 256; do
    install -Dm644 "assets/branding/hermes-$size.png" "$DEST$PREFIX/share/icons/hicolor/${size}x${size}/apps/$id.png"
done
install -Dm644 README.md "$DEST$PREFIX/share/doc/hermes/README.md"
for d in docs/*.md; do
  # local gitignored notes never ship (same rule as tools/source-archive.sh)
  case "$d" in docs/checkpoint.md|docs/PR-details.md) continue ;; esac
  install -Dm644 "$d" "$DEST$PREFIX/share/doc/hermes/$d"
done
install -Dm644 LICENSE "$DEST$PREFIX/share/licenses/hermes/LICENSE"
install -Dm644 third_party/stb_image_LICENSE.txt "$DEST$PREFIX/share/licenses/hermes/stb_image_LICENSE.txt"
install -Dm644 third_party/nanosvg_LICENSE.txt "$DEST$PREFIX/share/licenses/hermes/nanosvg_LICENSE.txt"
install -Dm644 third_party/material-icons/LICENSE "$DEST$PREFIX/share/licenses/hermes/material-icon-theme_LICENSE.txt"
install -Dm644 third_party/icons-catppuccin/LICENSE "$DEST$PREFIX/share/licenses/hermes/catppuccin-icons_LICENSE.txt"
install -Dm644 third_party/icons-symbols/LICENSE "$DEST$PREFIX/share/licenses/hermes/symbols-icons_LICENSE.txt"
echo "installed to $DEST$PREFIX"
if [ -z "$DEST" ]; then
  command -v update-desktop-database >/dev/null && update-desktop-database -q "$PREFIX/share/applications" || true
  command -v gtk-update-icon-cache >/dev/null && gtk-update-icon-cache -q -t "$PREFIX/share/icons/hicolor" || true
  command -v udevadm >/dev/null && udevadm control --reload 2>/dev/null || true
  echo "To make it the default folder handler: xdg-mime default $id.desktop inode/directory"
fi
