#!/usr/bin/env python3
"""Renders a map.pak as the Live Map would show it: map_preview.py map.pak --radius 25 --theme cinematic -o out.png"""
import argparse
import math
import os
import struct
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from build_map import K_AIRPORT, K_MAJOR, K_RING, K_RIVER, K_ROAD, K_RUNWAY, K_TOWN, UNITS, crc16  # noqa: E402

FONTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fonts")
THEMES = {  # the 5b spec's colours; the rest from firmware/lib/core/screens/Theme.cpp
    "cinematic": dict(water=(8, 16, 38), land=(24, 36, 62), coast=(96, 140, 200), ring=(70, 100, 160),
                      text=(200, 214, 255), airport=(255, 210, 154), runway=(150, 170, 220), road=(58, 74, 112),
                      road2=(42, 56, 90), river=(44, 72, 128), town=(150, 162, 200), caps=False),
    "editorial": dict(water=(16, 22, 30), land=(44, 48, 54), coast=(150, 156, 166), ring=(110, 110, 118),
                      text=(220, 220, 224), airport=(200, 200, 206), runway=(160, 160, 168), road=(84, 88, 96),
                      road2=(64, 68, 74), river=(52, 66, 84), town=(170, 170, 176), caps=True),
}
TOWN_CAP = {0: 10, 1: 7, 2: 5}


def steps(d, p, n, cuts):
    """n - 1 steps from d[p:], as RingReader: [(dx, dy, cut)], end offset."""
    out = []
    for _ in range(n - 1):
        cut = cuts and d[p] == 0x81
        if cut:
            p += 1
        if d[p] == 0x80:
            dx, dy = struct.unpack_from("<hh", d, p + 1)
            p += 5
        elif d[p] == 0x82:
            v = int.from_bytes(d[p + 1:p + 4], "little")
            dx, dy = ((v & 0xFFF) ^ 0x800) - 0x800, ((v >> 12) ^ 0x800) - 0x800
            p += 4
        else:
            dx, dy = struct.unpack_from("<bb", d, p)
            p += 2
        out.append((dx, dy, cut))
    return out, p


def read_pack(path):
    d = open(path, "rb").read()
    magic, ver, hsize, lat, lon, cover, nlev = struct.unpack_from("<4sHHiiHB", d, 0)
    assert magic == b"CMAP" and ver == 1, "not a version 1 map pack"
    assert struct.unpack_from("<H", d, 62)[0] == crc16(d[:62]), "header CRC"
    assert struct.unpack_from("<H", d, 32)[0] == crc16(d[64:64 + 24 * nlev]), "level table CRC"
    levels = []
    for i in range(nlev):
        max_r, milli, lat0, lon0, rows, cols, idx, icrc, _ = struct.unpack_from("<HHiiHHIHH", d, 64 + 24 * i)
        assert crc16(d[idx:idx + 8 * rows * cols]) == icrc, f"level {i} index CRC"
        levels.append(dict(max_r=max_r, deg=milli / 1000, lat0=lat0 / 1e5, lon0=lon0 / 1e5, rows=rows, cols=cols, idx=idx))
    return d, (lat / 1e5, lon / 1e5), cover, levels


def tile(d, lv, row, col):
    off, size, flags, _ = struct.unpack_from("<IHBB", d, lv["idx"] + 8 * (row * lv["cols"] + col))
    if not off:
        return flags & 1, []
    t = d[off:off + size]
    assert struct.unpack_from("<H", t, 0)[0] == crc16(t[2:]), f"tile {row},{col} CRC"
    (n,) = struct.unpack_from("<H", t, 2)
    feats = []
    for i in range(n):
        kind, rank, at, ymin, ymax = struct.unpack_from("<BBHhh", t, 4 + 8 * i)
        if kind == K_RING:
            head, x, y = struct.unpack_from("<HHH", t, at)
            st, _ = steps(t, at + 6, head & 0x7FFF, True)
            pts, cuts = [(x, y)], [bool(head & 0x8000)]  # cuts[i]: the edge into point i; point 0 carries the closing edge
            for dx, dy, cut in st:
                pts.append((pts[-1][0] + dx, pts[-1][1] + dy))
                cuts.append(cut)
            feats.append((kind, rank, (pts, cuts)))
        elif kind in (K_MAJOR, K_ROAD, K_RIVER, K_RUNWAY):
            cnt, x, y = struct.unpack_from("<HHH", t, at)
            st, _ = steps(t, at + 6, cnt, False)
            pts = [(x, y)]
            for dx, dy, _ in st:
                pts.append((pts[-1][0] + dx, pts[-1][1] + dy))
            feats.append((kind, rank, pts))
        else:
            x, y, ln = struct.unpack_from("<HHB", t, at)
            feats.append((kind, rank, (x, y, t[at + 5:at + 5 + ln].decode("utf-8"))))
    return flags & 1, feats


def render(path, radius, theme, out):
    d, home, cover, levels = read_pack(path)
    lvl = next(i for i, lv in enumerate(levels) if lv["max_r"] >= radius)
    lv, T, SS = levels[lvl], THEMES[theme], 4
    ppn, cos = 120 / radius, math.cos(math.radians(home[0]))

    def P(row, col, x, y, s=SS):
        lat = lv["lat0"] + (row + y / UNITS) * lv["deg"]
        lon = lv["lon0"] + (col + x / UNITS) * lv["deg"]
        return ((120 + (lon - home[1]) * 60 * cos * ppn) * s, (135 - (lat - home[0]) * 60 * ppn) * s)

    half_h, half_w = 135 / ppn / 60, 120 / ppn / 60 / cos
    r0 = max(0, int((home[0] - half_h - lv["lat0"]) // lv["deg"]))
    r1 = min(lv["rows"] - 1, int((home[0] + half_h - lv["lat0"]) // lv["deg"]))
    c0 = max(0, int((home[1] - half_w - lv["lon0"]) // lv["deg"]))
    c1 = min(lv["cols"] - 1, int((home[1] + half_w - lv["lon0"]) // lv["deg"]))
    tiles = {(r, c): tile(d, lv, r, c) for r in range(r0, r1 + 1) for c in range(c0, c1 + 1)}

    big = Image.new("RGB", (240 * SS, 270 * SS), T["water"])
    mask = Image.new("1", big.size, 0)
    for (r, c), (full, feats) in tiles.items():
        if full:
            a, b = P(r, c, 0, UNITS), P(r, c, UNITS, 0)
            # a pixel of overlap so adjacent all-land tiles leave no seam of water
            ImageDraw.Draw(mask).rectangle([a[0], a[1] - 1, b[0] + 1, b[1] + 1], fill=1)
        # even-odd within the tile, then OR into the chart: two tiles that share an edge must not cancel on it
        t_mask = None
        for kind, _, (pts, _cuts) in (f for f in feats if f[0] == K_RING):
            ring = Image.new("1", big.size, 0)
            ImageDraw.Draw(ring).polygon([P(r, c, x, y) for x, y in pts], fill=1, outline=1)
            t_mask = ring if t_mask is None else ImageChops.logical_xor(t_mask, ring)
        if t_mask is not None:
            mask = ImageChops.logical_or(mask, t_mask)
    big.paste(Image.new("RGB", big.size, T["land"]), mask=mask)
    dr = ImageDraw.Draw(big)
    style = {K_RIVER: ("river", 1.2), K_ROAD: ("road2", 1.0), K_MAJOR: ("road", 1.5), K_RUNWAY: ("runway", 1.5)}
    for (r, c), (_, feats) in tiles.items():
        for kind, _, pts in feats:
            if kind == K_RING:  # coastline: the edges that are not tile cuts, as the firmware strokes them
                ring, cuts = pts
                for i in range(len(ring)):
                    if not cuts[i]:
                        dr.line([P(r, c, *ring[i - 1]), P(r, c, *ring[i])], fill=T["coast"], width=SS)
            elif kind in style:
                col, w = style[kind]
                dr.line([P(r, c, x, y) for x, y in pts], fill=T[col], width=int(SS * w))
    for rr in (60, 120):
        dr.ellipse([(120 - rr) * SS, (135 - rr) * SS, (120 + rr) * SS, (135 + rr) * SS], outline=T["ring"], width=SS)
    img = Image.new("RGB", (240, 320), T["water"])
    img.paste(big.resize((240, 270), Image.LANCZOS), (0, 0))
    dr = ImageDraw.Draw(img)
    font = ImageFont.truetype(os.path.join(FONTS, "Inter-Medium.ttf"), 9)
    taken = [(0, 0, 240, 24), (110, 126, 130, 144)]
    towns = 0
    points = sorted(((k, rank, r, c, p) for (r, c), (_, f) in tiles.items() for k, rank, p in f if k >= K_TOWN),
                    key=lambda t: (t[0] == K_TOWN, t[1]))  # airports by rank, then towns by rank
    for kind, rank, r, c, (x, y, name) in points:
        sx, sy = P(r, c, x, y, 1)
        if not (4 < sx < 236 and 18 < sy < 266):
            continue
        if kind == K_TOWN:
            if towns >= TOWN_CAP[lvl]:
                continue
            name = name.upper() if T["caps"] else name
        w = dr.textlength(name, font=font)
        for bx, by in ((sx + 4, sy - 5), (sx - 4 - w, sy - 5), (sx - w / 2, sy + 3), (sx - w / 2, sy - 13)):
            b = (bx - 1, by, bx + w + 1, by + 10)
            if b[0] >= 2 and b[2] <= 238 and b[1] >= 2 and b[3] <= 268 and all(
                    b[2] < t[0] or b[0] > t[2] or b[3] < t[1] or b[1] > t[3] for t in taken):
                col = T["airport"] if kind == K_AIRPORT else T["town"]
                if kind == K_AIRPORT:
                    rad = 2.6 if rank == 0 else 1.8 if rank == 1 else 1.4
                    dr.ellipse([sx - rad, sy - rad, sx + rad, sy + rad], fill=col)
                else:
                    dr.rectangle([sx - 1, sy - 1, sx + 1, sy + 1], fill=col)
                    towns += 1
                dr.text((bx, by - 1), name, font=font, fill=col)
                taken.append(b)
                break
    img.save(out)
    print(f"{out}: level {lvl}, {len(tiles)} tiles")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("pack")
    ap.add_argument("--radius", type=int, default=25)
    ap.add_argument("--theme", choices=THEMES, default="cinematic")
    ap.add_argument("-o", "--out", default="map_preview.png")
    a = ap.parse_args()
    render(a.pack, a.radius, a.theme, a.out)


if __name__ == "__main__":
    main()
