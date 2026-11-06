#!/usr/bin/env bash
# Rebuilds the FAT images the FatReader tests read (firmware/test/test_fat_reader):
#   firmware/test/fixtures/fat32_mbr.img.gz   40 MB, MBR partition (type 0x0B) at LBA 1, FAT32, 512 B clusters
#   firmware/test/fixtures/fat16_flat.img.gz  16 MB, no partition table ("superfloppy"), FAT16
# macOS only: hdiutil makes and mounts the images (no root). File contents are a pattern the tests recompute:
# the little-endian 32-bit word at offset 4k is (k * 2654435761 + seed) mod 2^32.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
out="$root/firmware/test/fixtures"
tiny="$out/logos_tiny.pak"  # the FAT16 image's /contrail/logos.pak
if [[ ! -f "$tiny" ]]; then
    echo "missing $tiny: it is committed with the tests; run this from a full checkout" >&2
    exit 1
fi
work="$(mktemp -d)"
mnt="$work/mnt"
cleanup() {
    hdiutil detach -quiet "$mnt" 2>/dev/null || true
    rm -rf "$work"
}
trap cleanup EXIT

# pattern FILE SIZE SEED
pattern() {
    python3 - "$1" "$2" "$3" <<'EOF'
import struct, sys
path, size, seed = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
words = (size + 3) // 4
data = b"".join(struct.pack("<I", (k * 2654435761 + seed) & 0xFFFFFFFF) for k in range(words))
open(path, "wb").write(data[:size])
EOF
}

attach() { hdiutil attach -quiet -nobrowse -noautoopen -mountpoint "$mnt" "$1"; }
detach() {
    sync
    hdiutil detach -quiet "$mnt"
}

# put PATH SIZE SEED: writes a pattern file into the mounted image. cp -X copies no extended attributes, but
# macOS still adds ._ AppleDouble files for new files and directories (the tests expect them). put does not
# remount: the steps that depend on the order clusters are allocated (the fragmentation, the deleted entry)
# call detach and attach themselves, so a delete has reached the image before the next write.
put() {
    pattern "$work/src" "$2" "$3"
    cp -X "$work/src" "$mnt/$1"
}

# --- FAT32 behind an MBR -------------------------------------------------------------------------------
img="$work/fat32.dmg"
hdiutil create -quiet -size 40m -fs "MS-DOS FAT32" -layout MBRSPUD -volname CTEST32 "$img"
attach "$img"
touch "$mnt/.metadata_never_index"
mkdir -p "$mnt/CONTRAIL/NESTED/DEEP" "$mnt/FRAG"
# A hole in the middle of the free space, so LOGOS.PAK starts in it and continues past SPACER.BIN.
put CONTRAIL/PAD.BIN 20480 9
put CONTRAIL/SPACER.BIN 1536 8
detach; attach "$img"
rm "$mnt/CONTRAIL/PAD.BIN"  # also leaves a deleted entry ahead of LOGOS.PAK
detach; attach "$img"
put CONTRAIL/LOGOS.PAK 200000 1
put CONTRAIL/NESTED/DEEP/INNER.BIN 3000 2
printf 'long name\n' >"$mnt/Long Name File.txt"
printf 'lower case\n' >"$mnt/readme.txt"
: >"$mnt/EMPTY.TXT"
# Fragmentation the plain way: write A, write B, delete A, write C bigger than A.
put FRAG/A.BIN 8192 3
put FRAG/B.BIN 4096 4
detach; attach "$img"
rm "$mnt/FRAG/A.BIN"
detach; attach "$img"
put FRAG/C.BIN 30000 5
# A deleted entry that stays deleted (the driver reuses free slots, so delete after the last write).
put CONTRAIL/OLD.PAK 1000 7
detach; attach "$img"
rm "$mnt/CONTRAIL/OLD.PAK"
rm -rf "$mnt/.fseventsd" "$mnt/.Spotlight-V100" 2>/dev/null || true
detach
gzip -9 -n -c "$img" >"$out/fat32_mbr.img.gz"

# --- FAT16 with no partition table ----------------------------------------------------------------------
img="$work/fat16.dmg"
hdiutil create -quiet -size 16m -fs "MS-DOS FAT16" -layout NONE -volname CTEST16 "$img"
attach "$img"
touch "$mnt/.metadata_never_index"
mkdir -p "$mnt/contrail" "$mnt/DATA"
cp -X "$tiny" "$mnt/contrail/logos.pak"
put DATA/PATTERN.BIN 65536 6
rm -rf "$mnt/.fseventsd" "$mnt/.Spotlight-V100" 2>/dev/null || true
detach
gzip -9 -n -c "$img" >"$out/fat16_flat.img.gz"

ls -l "$out"/fat*.img.gz
