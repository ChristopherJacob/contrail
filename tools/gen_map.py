#!/usr/bin/env python3
"""Build firmware/lib/core/map/MapData.cpp: the Live Map's built-in base map.

Natural Earth 1:50m land and lakes are cut into 2x2-degree tiles (Sutherland-Hodgman, a latitude
band at a time, then a column at a time), quantised to 2048 units per tile side (~108 m) and
delta-coded in the ring format documented in firmware/lib/core/map/Tiles.h (int8 short steps, int12
medium steps, int16 long steps). Edges the clipper makes along a tile border are flagged as tile cuts
(filled, never stroked). Tiles entirely south of 60 S (Antarctica) are left out. OurAirports large
and medium airports follow as packed 10-byte records (see firmware/lib/core/map/MapData.h), with the
runways of the large ones as a flat table.

The inputs are local files (see the plan, Task 6); only the Python standard library is needed. The
output is committed, so firmware builds never need this script or the data.

  python3 -I tools/gen_map.py --land ne_50m_land.geojson --lakes ne_50m_lakes.geojson \\
      --airports airports.csv --runways runways.csv --out firmware/lib/core/map/MapData.cpp \\
      [--tolerance UNITS] [--budget BYTES] [--probe LAT,LON ...]
  python3 -I tools/gen_map.py --selftest

Exit codes: 0 written, 2 over the flash budget (nothing written), 3 a known point came out wrong.
"""
import argparse
import csv
import json
import math
import re
import struct
import sys

TILE_DEG = 2
ROWS, COLS = 90, 180
UNITS = 2048
MIN_ROW = 15  # rows 0..14 lie entirely south of 60 S (Antarctica): left out
BIT_BYTES = (ROWS * COLS + 7) // 8
DEFAULT_BUDGET = 360 * 1024
# Points the output must get right, checked on the encoded tiles: (name, lat, lon, land?).
KNOWN = [
    ("Kansas", 38.5, -98.5, True),
    ("Cambridge, MA", 42.373, -71.119, True),
    ("Lake Superior", 47.7, -87.5, False),
    ("Caspian Sea", 42.0, 50.5, False),
    ("North Pacific", 30.5, -140.5, False),
    ("Antarctica (left out)", -75.0, 0.0, False),
]


# ------------------------------------------------------------------ input

def polygons(path):
    """Each polygon in a GeoJSON file as a list of rings [(lon, lat), ...], outline first."""
    with open(path, encoding="utf-8") as f:
        doc = json.load(f)
    for feat in doc["features"]:
        g = feat.get("geometry") or {}
        if g.get("type") == "Polygon":
            polys = [g["coordinates"]]
        elif g.get("type") == "MultiPolygon":
            polys = g["coordinates"]
        else:
            continue
        for poly in polys:
            rings = []
            for ring in poly:
                pts = [(float(p[0]), float(p[1])) for p in ring]
                if len(pts) > 1 and pts[0] == pts[-1]:
                    pts.pop()
                if len(pts) >= 3:
                    rings.append(pts)
            if rings:
                yield rings


def bbox(ring):
    xs, ys = [p[0] for p in ring], [p[1] for p in ring]
    return min(xs), min(ys), max(xs), max(ys)


def load_geometry(land_path, lakes_path):
    """Every ring to fill even-odd: land outlines and holes, then the lakes that are not already
    holes in the land (a lake drawn both ways would cancel out and come back as land)."""
    land, holes = [], []
    for rings in polygons(land_path):
        land.extend(rings)
        holes.extend(bbox(r) for r in rings[1:])
    lakes, dropped = [], 0
    for rings in polygons(lakes_path):
        for r in rings:
            b = bbox(r)
            if any(all(abs(b[i] - h[i]) < 0.05 for i in range(4)) for h in holes):
                dropped += 1
                continue
            lakes.append(r)
    return land, lakes, dropped


CODE = re.compile(r"^[A-Z0-9]{3,4}$")
IDENT_CHARS = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"  # base 37: 0 pads a short code, then these
MAX_RUNWAYS = 31  # 5 bits in the record
RUNWAY_BLOCK = 32  # kRunwayStart holds the runways before every 32nd airport


def lat24(lat):
    return min((1 << 24) - 1, max(0, round((lat + 90) / 180 * (1 << 24))))


def lon24(lon):
    return round((lon + 180) / 360 * (1 << 24)) % (1 << 24)


def lat_e5(v):
    """A packed latitude back in 1e-5 degrees, exactly as MapData.h's Airport::latE5() does it."""
    return ((v * 18000000 + (1 << 23)) >> 24) - 9000000


def lon_e5(v):
    return ((v * 36000000 + (1 << 23)) >> 24) - 18000000


def wrap_e5(d):
    """A longitude difference in 1e-5 degrees, wrapped into [-180, 180) degrees."""
    return (d + 18000000) % 36000000 - 18000000


def ident21(code):
    v = 0
    for i, ch in enumerate(code):
        v += (1 + IDENT_CHARS.index(ch)) * 37 ** i
    return v


def unident21(v):
    out = ""
    while v:
        v, d = divmod(v, 37)
        out += IDENT_CHARS[d - 1] if d else ""
    return out


def pack_airport(a):
    """The 10-byte record: bits 0-23 latitude, 24-47 longitude (both unsigned fractions of the
    range), 48-68 ident (base 37), 69-70 size class (1 large, 0 medium), 71-75 runway count."""
    v = (a["lat24"] | a["lon24"] << 24 | ident21(a["code"]) << 48 | (1 if a["large"] else 0) << 69
         | len(a["rw"]) << 71)
    return v.to_bytes(10, "little")


def unpack_airport(b):
    v = int.from_bytes(b, "little")
    return {"latE5": lat_e5(v & 0xFFFFFF), "lonE5": lon_e5(v >> 24 & 0xFFFFFF), "code": unident21(v >> 48 & 0x1FFFFF),
            "large": (v >> 69 & 3) == 1, "runways": v >> 71 & 31}


def load_airports(path, runways_path):
    """Large and medium airports sorted by packed (lat, lon); large ones carry runway end offsets,
    in 1e-5 degrees from the airport's packed position (so the drawn runways stay exact)."""
    aps = []
    with open(path, newline="", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            if r["type"] not in ("large_airport", "medium_airport"):
                continue
            code = next((c for c in (r.get("icao_code") or "", r.get("gps_code") or "", r["ident"]) if CODE.match(c)),
                        None)
            if not code:
                continue
            lat, lon = float(r["latitude_deg"]), float(r["longitude_deg"])
            a = {"ident": r["ident"], "code": code, "large": r["type"] == "large_airport",
                 "lat24": lat24(lat), "lon24": lon24(lon), "rw": []}
            a["latE5"], a["lonE5"] = lat_e5(a["lat24"]), lon_e5(a["lon24"])
            aps.append(a)
    large = {a["ident"]: a for a in aps if a["large"]}
    dropped = 0
    with open(runways_path, newline="", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            a = large.get(r["airport_ident"])
            if not a or r.get("closed") == "1":
                continue
            try:
                lat1, lon1, lat2, lon2 = (float(r[k]) for k in ("le_latitude_deg", "le_longitude_deg",
                                                                "he_latitude_deg", "he_longitude_deg"))
            except ValueError:
                continue
            # Longitude offsets wrap into a half-turn, so a runway across 180 degrees stays short.
            d = [round(lat1 * 1e5) - a["latE5"], wrap_e5(round(lon1 * 1e5) - a["lonE5"]),
                 round(lat2 * 1e5) - a["latE5"], wrap_e5(round(lon2 * 1e5) - a["lonE5"])]
            if all(-32768 <= v <= 32767 for v in d) and len(a["rw"]) < MAX_RUNWAYS:
                a["rw"].append(d)
            else:
                dropped += 1
    aps.sort(key=lambda a: (a["lat24"], a["lon24"]))
    print(f"{dropped} runways dropped (an end over 0.33 degrees from the airport, or more than {MAX_RUNWAYS})")
    return aps


def runway_starts(airports):
    starts, n = [], 0
    for i, a in enumerate(airports):
        if i % RUNWAY_BLOCK == 0:
            starts.append(n)
        n += len(a["rw"])
    return starts or [0]


# ------------------------------------------------------------------ geometry

def area(poly):
    s = 0.0
    for (x0, y0), (x1, y1) in zip(poly, poly[1:] + poly[:1]):
        s += x0 * y1 - x1 * y0
    return s / 2


def simplify(ring, tol):
    """Douglas-Peucker on a closed ring (tolerance in degrees); the ring stays closed."""
    if tol <= 0 or len(ring) < 8:
        return ring
    x0, y0 = ring[0]
    far = max(range(len(ring)), key=lambda i: (ring[i][0] - x0) ** 2 + (ring[i][1] - y0) ** 2)
    keep = [False] * len(ring)
    keep[0] = keep[far] = True
    n = len(ring)
    for a, b in ((0, far), (far, n)):
        stack = [(a, b)]
        while stack:
            i, j = stack.pop()
            ax, ay = ring[i]
            bx, by = ring[j % n]
            dx, dy = bx - ax, by - ay
            norm = math.hypot(dx, dy)
            best, at = -1.0, -1
            for k in range(i + 1, j):
                px, py = ring[k]
                d = abs(dy * (px - ax) - dx * (py - ay)) / norm if norm else math.hypot(px - ax, py - ay)
                if d > best:
                    best, at = d, k
            if at >= 0 and best > tol:
                keep[at] = True
                stack.append((i, at))
                stack.append((at, j))
    out = [p for p, k in zip(ring, keep) if k]
    return out if len(out) >= 3 else ring


def clip(poly, axis, value, keep_low):
    """Sutherland-Hodgman against one axis-aligned line. Points made on the line get exactly
    `value` on that axis, so tile-cut edges can be recognised by exact comparison."""
    out = []
    if not poly:
        return out
    inside = (lambda p: p[axis] <= value) if keep_low else (lambda p: p[axis] >= value)
    prev = poly[-1]
    pin = inside(prev)
    for cur in poly:
        cin = inside(cur)
        if cin != pin:
            t = (value - prev[axis]) / (cur[axis] - prev[axis])
            q = [0.0, 0.0]
            q[axis] = value
            q[1 - axis] = prev[1 - axis] + (cur[1 - axis] - prev[1 - axis]) * t
            out.append((q[0], q[1]))
        if cin:
            out.append(cur)
        prev, pin = cur, cin
    return out


def tile_rings(rings):
    """{(row, col): [piece in degrees, ...]} for every tile the rings reach."""
    tiles = {}
    for ring in rings:
        lats = [p[1] for p in ring]
        r0 = max(MIN_ROW, int(math.floor((min(lats) + 90) / TILE_DEG)))
        r1 = min(ROWS - 1, int(math.floor((max(lats) + 90) / TILE_DEG)))
        for row in range(r0, r1 + 1):
            lat0 = -90 + row * TILE_DEG
            band = clip(clip(ring, 1, lat0, False), 1, lat0 + TILE_DEG, True)
            if len(band) < 3:
                continue
            lons = [p[0] for p in band]
            c0 = max(0, int(math.floor((min(lons) + 180) / TILE_DEG)))
            c1 = min(COLS - 1, int(math.floor((max(lons) + 180) / TILE_DEG)))
            for col in range(c0, c1 + 1):
                lon0 = -180 + col * TILE_DEG
                piece = clip(clip(band, 0, lon0, False), 0, lon0 + TILE_DEG, True)
                if len(piece) >= 3 and abs(area(piece)) > 1e-12:
                    tiles.setdefault((row, col), []).append(piece)
    return tiles


def quantise(piece, row, col):
    """Tile-local integer points [(x, y, cut)], cut meaning the edge INTO the point is a tile cut;
    point 0 carries the closing edge (last -> first). [] if fewer than 3 distinct points remain."""
    lat0, lon0 = -90 + row * TILE_DEG, -180 + col * TILE_DEG
    lo, hi = (lon0, lat0), (lon0 + TILE_DEG, lat0 + TILE_DEG)

    def on_border(a, b):
        # Both ends on the same tile edge: the clipper's cut (or the dataset's seam at 180 degrees).
        return any(a[ax] == b[ax] and (a[ax] == lo[ax] or a[ax] == hi[ax]) for ax in (0, 1))

    pts = []
    for i, p in enumerate(piece):
        x = min(UNITS, max(0, round((p[0] - lon0) / TILE_DEG * UNITS)))
        y = min(UNITS, max(0, round((p[1] - lat0) / TILE_DEG * UNITS)))
        pts.append((x, y, on_border(piece[i - 1], p)))
    out = []
    for x, y, cut in pts:
        if out and out[-1][0] == x and out[-1][1] == y:
            continue  # a repeat: the edge into the next point keeps its own flag
        out.append((x, y, cut))
    while len(out) > 1 and out[0][:2] == out[-1][:2]:
        last = out.pop()
        out[0] = (out[0][0], out[0][1], last[2])  # the closing edge is now the one into the popped point
    return out if len(out) >= 3 else []


def is_full(q):
    """A quantised ring that is the whole tile: every edge a cut, and the tile's area."""
    s = 0
    for (x0, y0, _), (x1, y1, _) in zip(q, q[1:] + q[:1]):
        s += x0 * y1 - x1 * y0
    return all(c for _, _, c in q) and abs(s) == 2 * UNITS * UNITS


def inside(rings, x, y):
    """Even-odd point-in-rings (any (x, y, ...) point tuples)."""
    c = False
    for ring in rings:
        for i in range(len(ring)):
            x0, y0 = ring[i - 1][0], ring[i - 1][1]
            x1, y1 = ring[i][0], ring[i][1]
            if (y0 > y) != (y1 > y) and x < x0 + (y - y0) * (x1 - x0) / (y1 - y0):
                c = not c
    return c


# ------------------------------------------------------------------ encoding

def encode_ring(q):
    n = len(q)
    assert 3 <= n < 0x8000, n
    out = bytearray(struct.pack("<HHH", n | (0x8000 if q[0][2] else 0), q[0][0], q[0][1]))
    px, py = q[0][0], q[0][1]
    for x, y, cut in q[1:]:
        dx, dy = x - px, y - py
        if cut:
            out.append(0x81)
        if -125 <= dx <= 127 and -128 <= dy <= 127:  # 0x80..0x82 never start a short step
            out += struct.pack("<bb", dx, dy)
        elif -2048 <= dx <= 2047 and -2048 <= dy <= 2047:
            out.append(0x82)
            out += ((dx & 0xFFF) | (dy & 0xFFF) << 12).to_bytes(3, "little")
        else:
            out.append(0x80)
            out += struct.pack("<hh", dx, dy)
        px, py = x, y
    return out


def encode_tile(rings):
    return struct.pack("<H", len(rings)) + b"".join(encode_ring(q) for q in rings)


def decode_tile(data):
    """The inverse of encode_tile: ([[(x, y, cut), ...], ...], bytes used)."""
    (n,) = struct.unpack_from("<H", data, 0)
    p, rings = 2, []
    for _ in range(n):
        head, x, y = struct.unpack_from("<HHH", data, p)
        p += 6
        pts = [(x, y, bool(head & 0x8000))]
        for _ in range((head & 0x7FFF) - 1):
            cut = data[p] == 0x81
            if cut:
                p += 1
            if data[p] == 0x80:
                dx, dy = struct.unpack_from("<hh", data, p + 1)
                p += 5
            elif data[p] == 0x82:
                v = int.from_bytes(data[p + 1:p + 4], "little")
                dx, dy = ((v & 0xFFF) ^ 0x800) - 0x800, ((v >> 12) ^ 0x800) - 0x800
                p += 4
            else:
                dx, dy = struct.unpack_from("<bb", data, p)
                p += 2
            x, y = x + dx, y + dy
            pts.append((x, y, cut))
        rings.append(pts)
    return rings, p


def build_tiles(land, lakes, tol_units):
    """(full-land tiles, {tile: encoded rings})."""
    tol = tol_units * TILE_DEG / UNITS
    land_t = tile_rings([simplify(r, tol) for r in land])
    lake_t = tile_rings([simplify(r, tol) for r in lakes])
    full, rings = set(), {}
    for key in set(land_t) | set(lake_t):
        row, col = key
        lq = [q for q in (quantise(p, row, col) for p in land_t.get(key, [])) if q]
        kq = [q for q in (quantise(p, row, col) for p in lake_t.get(key, [])) if q]
        if not kq and len(lq) == 1 and is_full(lq[0]):
            full.add(key)
        elif lq or kq:
            rings[key] = encode_tile(lq + kq)
    return full, rings


def land_at(full, rings, lat, lon):
    """Land or water on the encoded tiles, as the firmware's mask sees it."""
    row, col = int((lat + 90) // TILE_DEG), int((lon + 180) // TILE_DEG)
    if (row, col) in full:
        return True
    if (row, col) not in rings:
        return False
    x = (lon - (-180 + col * TILE_DEG)) / TILE_DEG * UNITS
    y = (lat - (-90 + row * TILE_DEG)) / TILE_DEG * UNITS
    return inside(decode_tile(rings[(row, col)])[0], x, y)


# ------------------------------------------------------------------ output

def tables(full, rings):
    full_bits, ring_bits = bytearray(BIT_BYTES), bytearray(BIT_BYTES)
    for r, c in full:
        i = r * COLS + c
        full_bits[i >> 3] |= 1 << (i & 7)
    offsets, data, rank = [], bytearray(), []
    for row in range(ROWS):
        rank.append(len(offsets))
        for col in range(COLS):
            if (row, col) in rings:
                i = row * COLS + col
                ring_bits[i >> 3] |= 1 << (i & 7)
                offsets.append(len(data))
                data += rings[(row, col)]
    rank.append(len(offsets))
    offsets.append(len(data))
    assert all(v < 1 << 16 for v in rank), "kRowRank is uint16"
    return full_bits, ring_bits, rank, offsets, data


def flash_bytes(t, airports):
    full_bits, ring_bits, rank, offsets, data = t
    runways = sum(len(a["rw"]) for a in airports)
    return (len(data) + 4 * len(offsets) + len(full_bits) + len(ring_bits) + 2 * len(rank) + 10 * len(airports)
            + 2 * len(runway_starts(airports)) + 8 * runways)


def c_rows(values, per=32):
    vals = list(values) or [0]
    return ["    " + ",".join(str(v) for v in vals[i:i + per]) + "," for i in range(0, len(vals), per)]


def write_cpp(path, t, airports, note):
    full_bits, ring_bits, rank, offsets, data = t
    out = ["// Generated by tools/gen_map.py. Do not edit.",
           "// Land and lakes: Natural Earth 1:50m (public domain, naturalearthdata.com).",
           "// Airports and runways: OurAirports (public domain, ourairports.com).",
           "// " + note,
           '#include "MapData.h"', "", "namespace basemap {", "namespace gen {", ""]

    def array(decl, values):
        out.append(decl + " = {")
        out.extend(c_rows(values))
        out.append("};")

    array("const uint8_t kFullLand[kBitBytes]", full_bits)
    array("const uint8_t kHasRings[kBitBytes]", ring_bits)
    array("const uint16_t kRowRank[kRows + 1]", rank)
    array("const uint32_t kTileOffset[]", offsets)
    array("const uint8_t kRingData[]", data)
    out.append(f"const uint32_t kRingDataSize = {len(data)};")
    assert len(airports) < 1 << 16, "AirportMark::index is uint16"
    assert sum(len(a["rw"]) for a in airports) < 1 << 16, "kRunwayStart and firstRunway() are uint16"
    out.append("const Airport kAirports[] = {")
    runways = []
    for a in airports:
        out.append("    {{" + ",".join(str(b) for b in pack_airport(a)) + "}},  // " + a["code"])
        runways.extend(a["rw"])
    if not airports:
        out.append("    {{0}},")
    out.append("};")
    out.append(f"const int kAirportCount = {len(airports)};")
    array("const uint16_t kRunwayStart[]", runway_starts(airports))
    out.append("const Runway kRunways[] = {")
    out.extend(f"    {{{d[0]}, {d[1]}, {d[2]}, {d[3]}}}," for d in runways)
    if not runways:
        out.append("    {0, 0, 0, 0},")
    out.append("};")
    out.append(f"const int kRunwayCount = {len(runways)};")
    out += ["", "}  // namespace gen", "}  // namespace basemap", ""]
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(out))


# ------------------------------------------------------------------ self-test

# The boundaries too: dx -125 is the last short step, -126 the first medium one; dy -128 is short;
# 2047 and -2048 are the medium step's ends.
STEP_RING = [(0, 0, True), (2048, 0, False), (2048, 2048, True), (2000, 2040, True), (1990, 2030, False),
             (1000, 1000, True), (500, 600, False), (375, 600, False), (249, 600, False), (249, 472, False),
             (1, 472, False), (2048, 472, False), (0, 472, True)]
STEP_RING_BYTES = ("0d 80 00 00 00 00 80 00 08 00 00 81 80 00 00 00 08 81 d0 f8 f6 f6 81 82 22 ac bf 82 0c 0e e7"
                   " 83 00 82 82 0f 00 00 80 82 08 0f 00 82 ff 07 00 81 82 00 08 00")

def selftest():
    def sq(x0, y0, x1, y1):
        return [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]

    t = tile_rings([sq(0, 0, 4, 4)])  # four whole tiles
    assert sorted(t) == [(45, 90), (45, 91), (46, 90), (46, 91)], sorted(t)
    for (row, col), pieces in t.items():
        assert len(pieces) == 1 and is_full(quantise(pieces[0], row, col)), (row, col)
    t = tile_rings([sq(0.5, 0.5, 1.5, 1.5)])  # an island: no cuts
    q = quantise(t[(45, 90)][0], 45, 90)
    assert len(q) == 4 and not any(c for _, _, c in q), q
    t = tile_rings([sq(1, 0.5, 3, 1.5)])  # across lon = 2: one cut on each side, on that border
    for key, border in (((45, 90), UNITS), ((45, 91), 0)):
        q = quantise(t[key][0], *key)
        cuts = [(x, y) for x, y, c in q if c]
        assert len(cuts) == 1 and cuts[0][0] == border, (key, q)
    rings = [[(10, 10, True), (20, 15, False), (25, 40, True), (5, 30, False)],
             [(0, 0, False), (1500, 0, False), (1500, 2048, True), (0, 2048, False)]]
    data = encode_tile(rings)
    back, used = decode_tile(data)
    assert back == rings and used == len(data), back
    # Short, medium and long steps, each with and without a cut: the same bytes as kSteps in
    # firmware/test/test_map_tiles, so the encoder and the firmware's reader agree.
    data = encode_ring(STEP_RING)
    assert data == bytes.fromhex(STEP_RING_BYTES), data.hex(" ")
    assert decode_tile(b"\x01\x00" + data) == ([STEP_RING], len(data) + 2)
    assert not tile_rings([sq(0, -70, 4, -61)]), "Antarctica is left out"
    t = tile_rings([sq(0, -61, 2, -59)])
    assert sorted(t) == [(MIN_ROW, 90)], sorted(t)  # the band north of 60 S stays
    a = {"code": "KSFO", "large": True, "lat24": lat24(37.618999), "lon24": lon24(-122.375), "rw": [[0, 0, 0, 0]] * 4}
    u = unpack_airport(pack_airport(a))
    assert u["code"] == "KSFO" and u["large"] and u["runways"] == 4, u
    assert abs(u["latE5"] - 3761900) <= 1 and abs(u["lonE5"] + 12237500) <= 2, u
    assert wrap_e5(35999000) == -1000 and wrap_e5(-35999000) == 1000 and wrap_e5(500) == 500
    for code in ("EGLL", "0A9", "ZZZZ", "999", "A00"):
        assert unident21(ident21(code)) == code and ident21(code) < 1 << 21, code
    for lat in (-90, -0.00001, 0, 45.5, 90):
        assert abs(lat_e5(lat24(lat)) - lat * 1e5) <= 1, lat
    for lon in (-180, -0.00001, 0, 179.999):  # 179.99999 packs as -180, the same meridian
        assert abs(lon_e5(lon24(lon)) - lon * 1e5) <= 2, lon
    full, enc = build_tiles([sq(0.5, 0.5, 1.5, 1.5)], [sq(0.9, 0.9, 1.1, 1.1)], 0)  # an island with a lake
    assert land_at(full, enc, 0.7, 0.7) and not land_at(full, enc, 1.0, 1.0) and not land_at(full, enc, 0.2, 0.2)
    full, enc = build_tiles([sq(0, 0, 4, 4)], [], 0)
    assert full == {(45, 90), (45, 91), (46, 90), (46, 91)} and not enc, (full, enc)
    print("selftest ok")


# ------------------------------------------------------------------ main

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    for name in ("land", "lakes", "airports", "runways", "out"):
        ap.add_argument("--" + name)
    ap.add_argument("--tolerance", type=float, default=1.0, help="Douglas-Peucker tolerance in tile units (~108 m)")
    ap.add_argument("--budget", type=int, default=DEFAULT_BUDGET, help="flash bytes allowed for all tables")
    ap.add_argument("--probe", action="append", default=[], metavar="LAT,LON")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        selftest()
        return 0
    missing = [n for n in ("land", "lakes", "airports", "runways", "out") if not getattr(a, n)]
    if missing:
        ap.error("missing " + ", ".join("--" + n for n in missing))
    land, lakes, dropped = load_geometry(a.land, a.lakes)
    print(f"{len(land)} land rings, {len(lakes)} lake rings ({dropped} lakes are already holes in the land)")
    full, rings = build_tiles(land, lakes, a.tolerance)
    airports = load_airports(a.airports, a.runways)
    for spec in a.probe:
        lat, lon = (float(v) for v in spec.split(","))
        src = "land" if inside(land + lakes, lon, lat) else "water"
        enc = "land" if land_at(full, rings, lat, lon) else "water"
        print(f"probe {lat},{lon}: source {src}, tiles {enc}")
    bad = [n for n, lat, lon, want in KNOWN if land_at(full, rings, lat, lon) != want]
    if bad:
        print("wrong land/water at: " + ", ".join(bad))
        return 3
    t = tables(full, rings)
    size = flash_bytes(t, airports)
    runways = sum(len(x["rw"]) for x in airports)
    note = (f"tolerance {a.tolerance} units; {len(full)} full tiles, {len(rings)} ring tiles, {len(t[4])} ring bytes; "
            f"{len(airports)} airports, {runways} runways; {size} bytes in all")
    print(note)
    if size > a.budget:
        print(f"over the {a.budget}-byte budget: rerun with a larger --tolerance")
        return 2
    write_cpp(a.out, t, airports, note)
    print(f"wrote {a.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
