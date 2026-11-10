#!/usr/bin/env bash
# The showcase page's animated previews (6d), from the simulator's --clip and --tour:
#   tools/previews.sh <sim-binary> <out-dir>
# Files: <screen>-<theme>.mp4 and tour.mp4, 480x640 (2x). Pixels are doubled first, so that 4:2:0 chroma lands on whole
# firmware pixels and small coloured text does not smear. Fails if a file is over its size limit (spec 6d).
set -euo pipefail
if [ $# -ne 2 ]; then
    echo "usage: $0 <sim-binary> <out-dir>" >&2
    exit 2
fi
sim="$1"
out="$2"
mkdir -p "$out"

max_clip=$((600 * 1024))
max_tour=$((2048 * 1024))
max_all=$((5120 * 1024))

encode() {  # <output>; frames on stdin
    ffmpeg -nostdin -v error -y -f rawvideo -pix_fmt rgb24 -s 240x320 -r 30 -i - \
        -vf scale=480:640:flags=neighbor,format=yuv420p -c:v libx264 -preset veryslow -crf 23 \
        -movflags +faststart -an "$1"
}

total=0
check() {  # <file> <limit>
    local size
    size=$(wc -c <"$1" | tr -d ' ')
    echo "$((size / 1024)) KB  $(basename "$1")"
    total=$((total + size))
    if [ "$size" -gt "$2" ]; then
        echo "$1 is over its limit of $(($2 / 1024)) KB" >&2
        exit 1
    fi
}

for theme in cinematic editorial; do
    for screen in closest radar sky alert map board; do
        "$sim" --clip "$screen" "$theme" | encode "$out/$screen-$theme.mp4"
        check "$out/$screen-$theme.mp4" "$max_clip"
    done
done
"$sim" --tour | encode "$out/tour.mp4"
check "$out/tour.mp4" "$max_tour"

echo "$((total / 1024)) KB in all"
if [ "$total" -gt "$max_all" ]; then
    echo "the previews are over their limit of $((max_all / 1024)) KB" >&2
    exit 1
fi
