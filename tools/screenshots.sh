#!/usr/bin/env bash
# Every screen in both themes, from the simulator's --render mode, as PNGs for the site:
#   tools/screenshots.sh <sim-binary> <out-dir>      (PYTHON: an interpreter with Pillow, default python3)
# Files: <screen>-<theme>.png, 480x640 (2x).
set -euo pipefail
if [ $# -ne 2 ]; then
    echo "usage: $0 <sim-binary> <out-dir>" >&2
    exit 2
fi
sim="$1"
out="$2"
here="$(cd "$(dirname "$0")" && pwd)"
py="${PYTHON:-python3}"
mkdir -p "$out"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

# screen:scene
pairs="closest:busy radar:busy board:busy sky:night map:busy stats:stats alert:busy"
for theme in cinematic editorial; do
    for pair in $pairs; do
        screen="${pair%%:*}"
        scene="${pair##*:}"
        "$sim" --render "$scene" "$screen" "$theme" "$tmp/$screen-$theme.ppm"
        "$py" "$here/ppm2png.py" "$tmp/$screen-$theme.ppm" "$out/$screen-$theme.png" 2 >/dev/null
    done
done
ls "$out"/*.png | wc -l | tr -d ' '
