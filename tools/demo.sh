#!/bin/sh
# Record the intro video: builds build/demo/hermes-demo, makes the demo home (tools/demo-home.sh), records the
# scripted scenes in real time (about 2 minutes) and encodes OUT_DIR/hermes-demo.mp4 plus a
# poster frame. The app runs in its own user + mount namespace where the demo home is /home/alex, so nothing of
# this machine shows up. Usage: tools/demo.sh [OUT_DIR]   (default build/demo; needs ffmpeg, unshare)
# HERMES_DEMO_SCENES=2,5 records only those scenes (while working on them).
set -e
repo="$(cd "$(dirname "$0")/.." && pwd)"
out="$(mkdir -p "${1:-$repo/build/demo}" && cd "${1:-$repo/build/demo}" && pwd)"
B=/tmp/fe-demo
"$repo/build.sh" demo
sh "$repo/tools/demo-home.sh" "$B" # (fresh every time: the scenes copy files and type in the terminal)
rm -rf "$B/config" "$B/data" "$B/cache" "$B/gvfs" /tmp/fe-selftest-state
cp "$repo/build/demo/hermes-demo" "$B/bin/" # (/home is replaced in the namespace)
uid=$(id -u) gid=$(id -g)
# The shell's title says user@host: we're alex on "laptop" there.
awk -F: -v u="$uid" 'BEGIN { OFS = ":" } $3 == u { $1 = "alex"; $5 = "Alex"; $6 = "/home/alex" } { print }' /etc/passwd > "$B/passwd"
# Outer namespace (root there): a fresh /home with the demo home at /home/alex, our passwd, a hostname; inner one:
# back to our own uid.
unshare -Urmu sh -c "hostname laptop && mount --bind '$B/passwd' /etc/passwd &&
    mount -t tmpfs none /home && mkdir /home/alex && mount --bind '$B/home' /home/alex &&
    cd /home/alex && exec unshare -U --map-user=$uid --map-group=$gid \
    env USER=alex LOGNAME=alex HERMES_DEMO_HOME=/home/alex HERMES_DEMO_DIR='$B' '$B/bin/hermes-demo' --demo '$B/raw.mkv'"
# Final encode: H.264 for the GitHub player, and a poster frame.
ffmpeg -y -loglevel error -i "$B/raw.mkv" -c:v libx264 -preset slow -crf 20 -pix_fmt yuv420p -movflags +faststart "$out/hermes-demo.mp4"
ffmpeg -y -loglevel error -ss 8 -i "$B/raw.mkv" -frames:v 1 "$out/poster.png"
ls -lh "$out"
