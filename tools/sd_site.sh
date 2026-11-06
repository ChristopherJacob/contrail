#!/usr/bin/env bash
# Puts the SD card page's data on the site (6c spec §2): the map regions and search index in <site>/mapdata/ and
# the map builder's Python in <site>/py/. The map sources are downloaded into tools/.cache/map/ if missing.
#   tools/sd_site.sh site
set -euo pipefail
site="${1:?usage: tools/sd_site.sh <site folder>}"
here="$(cd "$(dirname "$0")" && pwd)"
python3 -I "$here/map_regions.py" --out "$site/mapdata"
mkdir -p "$site/py"
cp "$here/build_map.py" "$here/gen_map.py" "$here/sd_card.py" "$site/py/"
