#!/usr/bin/env bash
# The browser simulator's WebAssembly build (6b): firmware/lib/core, lib/hal_wasm and src/main_wasm.cpp with
# emscripten (em++ on PATH, 7.0.0), into <out-dir>/contrail.js and contrail.wasm:
#   tools/build_wasm.sh <out-dir>
# ArduinoJson is PlatformIO's copy for the sim env (installed with pio when missing). Objects go to
# firmware/.pio/build/wasm.
set -euo pipefail
if [ $# -ne 1 ]; then
    echo "usage: $0 <out-dir>" >&2
    exit 2
fi
here="$(cd "$(dirname "$0")" && pwd)"
fw="$here/../firmware"
out="$1"
mkdir -p "$out"
json="$fw/.pio/libdeps/sim/ArduinoJson/src"
[ -d "$json" ] || (cd "$fw" && pio pkg install -e sim >/dev/null)

# The version as firmware/scripts/version.py makes it: the release tag, else `git describe` with a dot, else dev.
version=dev
if [ "${GITHUB_REF_TYPE:-}" = tag ] && [[ "${GITHUB_REF_NAME:-}" == v* ]]; then
    version="${GITHUB_REF_NAME#v}"
elif tag="$(git -C "$fw" describe --tags --dirty --always 2>/dev/null)" && [[ "$tag" == *.* ]]; then
    version="${tag#v}"
fi

# -I lib/core only, as PlatformIO does: a -I per folder would let core's headers shadow system ones on a
# case-insensitive file system. -ffp-contract=off as the sim and test envs, so the pixels match theirs.
flags=(-std=gnu++17 -O2 -ffp-contract=off -Wall -Wno-unused-function -DCONTRAIL_WASM
       "-DCONTRAIL_VERSION=\"$version\"" -I "$fw/lib/hal" -I "$fw/lib/core" -I "$fw/lib/hal_wasm" -I "$json")
obj="$fw/.pio/build/wasm"
rm -rf "$obj"
mkdir -p "$obj"
export obj
sources=$(cd "$fw" && find lib/core lib/hal_wasm -name '*.cpp' | sort; echo src/main_wasm.cpp)
# One em++ per source, in parallel; each object named after its path.
(cd "$fw" && printf '%s\n' $sources | xargs -P "$(getconf _NPROCESSORS_ONLN)" -I{} \
    sh -c 'em++ "$@" -c "{}" -o "$obj/$(echo "{}" | tr / _).o"' sh "${flags[@]}")
em++ "${flags[@]}" "$obj"/*.o -o "$out/contrail.js" \
    --no-entry -sMODULARIZE=1 -sEXPORT_NAME=createContrail -sENVIRONMENT=worker,node \
    -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576 \
    -sEXPORTED_FUNCTIONS=_sim_start,_sim_step,_sim_touch,_sim_show,_sim_theme,_sim_alert,_sim_render,_malloc,_free \
    -sEXPORTED_RUNTIME_METHODS=ccall,HEAPU16 \
    --js-library "$fw/lib/hal_wasm/localtime.js"
size=$(wc -c <"$out/contrail.wasm" | tr -d ' ')
echo "contrail.wasm: $size bytes ($version)"
# The spec's budget (§2): 2 MB before compression.
[ "$size" -le 2000000 ] || { echo "contrail.wasm is over 2 MB" >&2; exit 1; }
