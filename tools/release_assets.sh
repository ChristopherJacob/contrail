#!/usr/bin/env bash
# Release files from the cyd2usb and cyd_microusb builds (run `pio run -e <env>` for both first):
#   tools/release_assets.sh <version> <out-dir>          (from the repo root)
# Per model: contrail-<model>-install.bin (bootloader, partition table, boot_app0 and app merged at offset 0,
# for ESP Web Tools and a blank board) and contrail-<model>-app.bin (the app alone, for the web update), each
# also under a versioned name; then logos.pak and SHA256SUMS. Needs pio, esptool 4.x and Pillow on PATH's
# python (PYTHON overrides it).
set -euo pipefail
if [ $# -ne 2 ]; then
    echo "usage: $0 <version> <out-dir>" >&2
    exit 2
fi
version="$1"
out="$2"
py="${PYTHON:-python}"
mkdir -p "$out"

for model in cyd2usb cyd_microusb; do
    build="firmware/.pio/build/$model"
    # The flash layout this script writes, checked against what PlatformIO uploads for this env.
    dump="$(pio run -d firmware -e "$model" -t envdump)"
    flat="$(printf '%s' "$dump" | tr -d ' \n')"
    for want in "'ESP32_APP_OFFSET':'0x10000'" "'BOARD_FLASH_MODE':'dio'" "'BOARD_F_FLASH':'40000000L'" \
                "('0x1000','[^']*/$model/bootloader.bin')" "('0x8000','[^']*/$model/partitions.bin')" \
                "('0xe000','[^']*/boot_app0.bin')"; do
        if ! printf '%s' "$flat" | grep -q "$want"; then
            echo "$model: the build's flash layout changed (no $want in envdump); update this script" >&2
            exit 1
        fi
    done
    boot_app0="$(printf '%s' "$flat" | grep -o "('0xe000','[^']*/boot_app0.bin')" | cut -d"'" -f4)"
    for f in "$build/bootloader.bin" "$build/partitions.bin" "$boot_app0" "$build/firmware.bin"; do
        [ -f "$f" ] || { echo "missing $f (build $model first)" >&2; exit 1; }
    done
    "$py" -m esptool --chip esp32 merge_bin -o "$out/contrail-$model-install.bin" \
        --flash_mode dio --flash_freq 40m --flash_size 4MB \
        0x1000 "$build/bootloader.bin" 0x8000 "$build/partitions.bin" 0xe000 "$boot_app0" \
        0x10000 "$build/firmware.bin"
    cp "$build/firmware.bin" "$out/contrail-$model-app.bin"
    cp "$out/contrail-$model-install.bin" "$out/contrail-$model-$version-install.bin"
    cp "$out/contrail-$model-app.bin" "$out/contrail-$model-$version-app.bin"
done

"$py" -I tools/build_pack.py --logos assets/logos --out "$out/logos.pak" --built "$(git log -1 --format=%ct)"
(cd "$out" && rm -f SHA256SUMS && sha256sum -- * > SHA256SUMS)
ls -l "$out"
