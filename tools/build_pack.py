#!/usr/bin/env python3
"""Build logos.pak (5a spec §2-3) from a folder of airline logos named <ICAO>.png.

Each logo is scaled to 40, 30 and 25 px (Lanczos), given rounded corners as alpha, and quantised: 40 and 30
px to <= 128 colours at 8 bpp, 25 px to 16 colours at 4 bpp. The pack layout (header, index, logo item and
its per-size blocks with their CRCs) is documented in the header comment of firmware/lib/core/assets/Pack.h.
Needs Pillow (tools/requirements.txt).

  tools/.venv/bin/python -I tools/build_pack.py --logos assets/logos --out logos.pak [--only UAL,DAL]
  tools/.venv/bin/python -I tools/build_pack.py --selftest

Copy logos.pak to /contrail/ on a FAT32 card. Exit codes: 0 written, 2 no logos found (or an --only code was not found), 3 two files share a code (UAL.png, ual.png).
"""
import argparse
import os
import struct
import sys
import time

from PIL import Image, ImageDraw

MAGIC, VERSION, KIND_LOGOS = b"CPAK", 1, 1
SIZES = [(40, 8, 128), (30, 8, 128), (25, 4, 16)]  # side, bpp, colours


def crc16(data, crc=0xFFFF):
    """CRC-16/CCITT-FALSE, as assets::crc16."""
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else crc << 1
            crc &= 0xFFFF
    return crc


def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def write_pack(path, kind, items, built=None):
    """items: {key: bytes}. Keys are 1-4 ASCII characters."""
    keys = sorted(items)
    count = len(keys)
    index_off = 32
    data_off = index_off + 12 * count
    index, blob, off = [], [], data_off
    for k in keys:
        kb = k.encode("ascii")
        assert 1 <= len(kb) <= 4, k
        index.append(kb.ljust(4, b"\0") + struct.pack("<II", off, len(items[k])))
        blob.append(items[k])
        off += len(items[k])
    header = MAGIC + struct.pack("<HHIIII", VERSION, kind, count, index_off, data_off,
                                 int(time.time()) if built is None else built) + b"\0" * 8
    assert len(header) == 32
    with open(path, "wb") as f:
        f.write(header + b"".join(index) + b"".join(blob))


def rounded(img, side):
    """img (RGBA, side x side) with corners of radius side/8 made transparent, anti-aliased by 4x supersampling."""
    r = side / 8
    big = Image.new("L", (side * 4, side * 4), 0)
    ImageDraw.Draw(big).rounded_rectangle([0, 0, side * 4 - 1, side * 4 - 1], radius=r * 4, fill=255)
    mask = big.resize((side, side), Image.Resampling.LANCZOS)
    out = img.copy()
    a = out.getchannel("A")
    out.putalpha(Image.composite(a, Image.new("L", a.size, 0), mask))
    return out


def squared(src):
    """src as RGBA, padded to a square with transparent margins (centred), so scaling never stretches it."""
    im = src.convert("RGBA")
    w, h = im.size
    if w == h:
        return im
    side = max(w, h)
    out = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    out.paste(im, ((side - w) // 2, (side - h) // 2))
    return out


def quantise(src, side, colours):
    """(RGBA palette, one index per pixel) for src scaled to side x side with rounded corners."""
    im = squared(src).resize((side, side), Image.Resampling.LANCZOS)
    im = rounded(im, side)
    q = im.quantize(colors=colours, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.NONE)
    return q.getpalette("RGBA"), list(q.tobytes())  # tobytes, as getdata is deprecated in Pillow 13


def encode_size(src, side, bpp, colours):
    pal, px = quantise(src, side, colours)
    used = max(px) + 1
    assert used <= colours, (used, colours)
    block = b""
    for i in range(used):
        r, g, b, a = pal[4 * i:4 * i + 4]
        # The quantiser averages alpha across merged colours, so "opaque" comes out as 253 and "clear" as 1. Snap the
        # near-ends so solid areas are exactly opaque and the corners exactly clear (drawBadge can then skip blending).
        a = 255 if a >= 250 else 0 if a <= 5 else a
        block += struct.pack("<HB", rgb565(r, g, b), a)
    if bpp == 8:
        block += bytes(px)
    else:
        for y in range(side):
            row = px[y * side:(y + 1) * side] + [0]
            block += bytes((row[x] << 4) | row[x + 1] for x in range(0, side, 2))
    block += struct.pack("<H", crc16(block))
    return used, block


def encode_logo(path):
    src = Image.open(path)
    head = struct.pack("<B", len(SIZES))
    blocks, off = b"", 1 + 8 * len(SIZES)
    for side, bpp, colours in SIZES:
        used, block = encode_size(src, side, bpp, colours)
        head += struct.pack("<BBBBI", side, side, bpp, used, off)
        blocks += block
        off += len(block)
    return head + blocks


def selftest():
    assert crc16(b"123456789") == 0x29B1  # the CRC-16/CCITT-FALSE check value
    assert rgb565(255, 255, 255) == 0xFFFF
    # A synthetic 25 px logo through the 4 bpp encoder: the nibbles decode back to the quantised indices.
    src = Image.new("RGBA", (90, 60), (0, 0, 0, 0))  # not square: exercises the padding too
    d = ImageDraw.Draw(src)
    for i, c in enumerate([(255, 0, 0), (0, 160, 0), (0, 0, 255), (240, 200, 0), (255, 255, 255)]):
        d.rectangle([i * 18, 0, i * 18 + 17, 59], fill=c + (255,))
    side, bpp, colours = 25, 4, 16
    _, want = quantise(src, side, colours)
    used, block = encode_size(src, side, bpp, colours)
    row = (side * bpp + 7) // 8
    assert len(block) == 3 * used + row * side + 2, len(block)
    assert struct.unpack("<H", block[-2:])[0] == crc16(block[:-2])
    px = block[3 * used:-2]
    got = [(px[y * row + x // 2] >> (0 if x & 1 else 4)) & 0x0F for y in range(side) for x in range(side)]
    assert got == want, "4 bpp nibbles do not match the quantised indices"
    assert all(px[y * row + row - 1] & 0x0F == 0 for y in range(side))  # the odd row's pad nibble
    print("selftest ok")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--logos")
    ap.add_argument("--out")
    ap.add_argument("--only", help="comma-separated ICAO codes (test fixtures)")
    ap.add_argument("--built", type=int, help="fixed build time, for reproducible fixtures")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        selftest()
        return 0
    if not a.logos or not a.out:
        ap.error("--logos and --out are required (unless --selftest)")
    only = {c.strip().upper() for c in a.only.split(",")} if a.only else None
    items, source = {}, {}
    for name in sorted(os.listdir(a.logos)):
        stem, ext = os.path.splitext(name)
        code = stem.upper()
        if ext.lower() != ".png" or not 3 <= len(code) <= 4 or not (code.isascii() and code.isalnum()):
            continue
        if only and code not in only:
            continue
        if code in source:
            print(f"duplicate code {code}: {source[code]} and {name}")
            return 3
        source[code] = name
        items[code] = encode_logo(os.path.join(a.logos, name))
    missing = sorted(only - set(items)) if only else []
    if missing:
        print("not found: " + ", ".join(missing))
        return 2
    if not items:
        print("no logos found")
        return 2
    write_pack(a.out, KIND_LOGOS, items, a.built)
    size = os.path.getsize(a.out)
    print(f"wrote {a.out}: {len(items)} logos, {size / 1e6:.1f} MB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
