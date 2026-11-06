#!/usr/bin/env python3
"""Builds /contrail/map.pak, the Live Map's detailed map for one home (5b spec §2, §3).

  python3 -I tools/build_map.py --lat 39.4176 --lon -77.3743 --out map.pak

Sources are downloaded once into tools/.cache/map/ (about 106 MB) and reused; --refresh downloads them again.
Natural Earth 1:10m (public domain), OurAirports (public domain), GeoNames cities5000 (CC BY 4.0).
"""
import argparse
import binascii
import csv
import io
import json
import math
import os
import re
import struct
import sys
import urllib.request
import zipfile
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_map import clip, encode_ring, simplify  # noqa: E402  (shared with the built-in map)

UNITS = 2048
NE = "https://raw.githubusercontent.com/nvkelso/natural-earth-vector/v5.1.2/geojson/"
SOURCES = {
    "ne_10m_land.geojson": NE + "ne_10m_land.geojson",
    "ne_10m_lakes.geojson": NE + "ne_10m_lakes.geojson",
    "ne_10m_lakes_north_america.geojson": NE + "ne_10m_lakes_north_america.geojson",
    "ne_10m_rivers_lake_centerlines.geojson": NE + "ne_10m_rivers_lake_centerlines.geojson",
    "ne_10m_rivers_north_america.geojson": NE + "ne_10m_rivers_north_america.geojson",
    "ne_10m_roads.geojson": NE + "ne_10m_roads.geojson",
    "airports.csv": "https://davidmegginson.github.io/ourairports-data/airports.csv",
    "runways.csv": "https://davidmegginson.github.io/ourairports-data/runways.csv",
    "cities5000.zip": "https://download.geonames.org/export/dump/cities5000.zip",
}
# (max radius nm, tile size in millidegrees, simplification tolerance in metres)
LEVELS = [(25, 250, 40), (60, 500, 100), (100, 1000, 250)]
CACHE_SIZE = 135168  # 255 tiles of 16 x 16 RGB565 (130,560), a 32-byte key and 1,440 bytes for marks, in 33 x 4 KB
K_RING, K_MAJOR, K_ROAD, K_RIVER, K_RUNWAY, K_TOWN, K_AIRPORT = 0, 1, 2, 3, 4, 16, 17
MAJOR_TYPES = ("Major Highway", "Beltway")
# town rank by population: 0 >= 300k, 1 >= 100k, 2 >= 50k, 3 >= 25k, 4 >= 10k, 5 >= 5k (cities5000 has nothing smaller)
TOWN_FLOORS = (300000, 100000, 50000, 25000, 10000, 0)
KIND_NAMES = {K_RING: "rings", K_MAJOR: "major roads", K_ROAD: "roads", K_RIVER: "rivers", K_RUNWAY: "runways",
              K_TOWN: "towns", K_AIRPORT: "airports"}
MINOR_TYPES = ("Secondary Highway", "Road", "Bypass")
COVER = 250  # nm from home to each side of the coverage square
SHAPE_FILES = ("ne_10m_land", "ne_10m_lakes", "ne_10m_lakes_north_america", "ne_10m_rivers_lake_centerlines",
               "ne_10m_rivers_north_america", "ne_10m_roads")
KEPT_PROPS = ("scalerank", "type")  # the only shape properties the map reads


def crc16(data, crc=0xFFFF):
    """CRC-16/CCITT-FALSE, as assets::crc16 (binascii.crc_hqx is the same CRC, in C)."""
    return binascii.crc_hqx(bytes(data), crc)


# ------------------------------------------------------------------ sources

def fetch(cache, refresh):
    os.makedirs(cache, exist_ok=True)
    for name, url in SOURCES.items():
        path = os.path.join(cache, name)
        if os.path.exists(path) and not refresh:
            continue
        print("downloading", url)
        tmp = path + ".part"
        with urllib.request.urlopen(url, timeout=120) as r, open(tmp, "wb") as f:
            want = r.headers.get("Content-Length")
            while chunk := r.read(1 << 20):
                f.write(chunk)
        got = os.path.getsize(tmp)
        if want is not None and got != int(want):
            os.remove(tmp)
            raise RuntimeError(f"{name}: got {got} of {want} bytes")
        os.replace(tmp, path)


class Box:
    """The coverage square around home, in degrees."""

    def __init__(self, lat, lon, cover_nm):
        self.lat0, self.lat1 = lat - cover_nm / 60, lat + cover_nm / 60
        dlon = cover_nm / 60 / math.cos(math.radians(lat))
        self.lon0, self.lon1 = lon - dlon, lon + dlon

    def has(self, lat, lon):
        return self.lat0 <= lat <= self.lat1 and self.lon0 <= lon <= self.lon1

    def touches(self, pts):
        xs, ys = [p[0] for p in pts], [p[1] for p in pts]
        return not (max(ys) < self.lat0 or min(ys) > self.lat1 or max(xs) < self.lon0 or min(xs) > self.lon1)


def geo_parts(path):
    """Every polygon (outer ring and holes) or line of a GeoJSON file, in file order, as (properties, rings)."""
    with open(path, encoding="utf-8") as f:
        for feat in json.load(f)["features"]:
            g = feat.get("geometry")
            if not g:
                continue
            t, c = g["type"], g["coordinates"]
            parts = {"Polygon": [c], "MultiPolygon": c, "LineString": [[c]], "MultiLineString": [[l] for l in c]}.get(t, [])
            for part in parts:  # polygon: [outer, holes...]; line: [line]
                yield feat["properties"], [[(float(p[0]), float(p[1])) for p in r] for r in part]


def public_code(a):
    """A small field's public code, or None: an ICAO code, or a K-code that is not a KSnn / KYnn private strip."""
    if a.get("icao_code"):
        return a["icao_code"]
    g = a.get("gps_code") or ""
    if re.fullmatch(r"K[A-Z0-9]{3}", g) and not re.fullmatch(r"K[SY][0-9]{2}", g):
        return g
    return None


def airport_name(a, rank):
    """The name drawn for an airport, or None if it is not shown. Large and medium are always shown."""
    if rank < 2:
        return (a.get("icao_code") or a.get("gps_code") or a["ident"])[:20]
    code = public_code(a)
    return code[:20] if code else None


def is_town(row):
    return row[7].startswith("PPL") and row[7] not in ("PPLX", "PPLQ")


def read_sources(cache, box=None):
    """The map's records, in source order. shapes: {layer: [(part, props, rings)]}, every part with its index in
    the layer (geo_parts order); towns: [(row, name, lat, lon, population)]; airports: [(row, rank, name, lat, lon,
    runways)], only those drawn. With a box, only records that could be in it are kept (assemble decides)."""
    shapes = {}
    for name in SHAPE_FILES:
        parts = []
        for i, (props, rings) in enumerate(geo_parts(os.path.join(cache, name + ".geojson"))):
            if box is None or any(box.touches(r) for r in rings):
                parts.append((i, {k: props[k] for k in KEPT_PROPS if props.get(k) is not None}, rings))
        shapes[name] = parts

    towns = []
    with zipfile.ZipFile(os.path.join(cache, "cities5000.zip")) as z:
        with z.open("cities5000.txt") as f:
            for n, row in enumerate(csv.reader(io.TextIOWrapper(f, encoding="utf-8"), delimiter="\t")):
                lat, lon = float(row[4]), float(row[5])
                if is_town(row) and (box is None or box.has(lat, lon)):
                    towns.append((n, row[2], lat, lon, int(row[14] or 0)))

    runways = {}
    with open(os.path.join(cache, "runways.csv"), encoding="utf-8") as f:
        for r in csv.DictReader(f):
            if r.get("closed") == "1":
                continue
            try:
                runways.setdefault(r["airport_ident"], []).append(
                    ((float(r["le_longitude_deg"]), float(r["le_latitude_deg"])),
                     (float(r["he_longitude_deg"]), float(r["he_latitude_deg"]))))
            except ValueError:
                pass
    airports = []
    rank_of = {"large_airport": 0, "medium_airport": 1, "small_airport": 2}
    with open(os.path.join(cache, "airports.csv"), encoding="utf-8") as f:
        for n, a in enumerate(csv.DictReader(f)):
            rank = rank_of.get(a["type"])
            if rank is None:
                continue
            try:
                lat, lon = float(a["latitude_deg"]), float(a["longitude_deg"])
            except ValueError:
                continue
            if box is not None and not box.has(lat, lon):
                continue
            ident = airport_name(a, rank)
            if ident is None:
                continue
            airports.append((n, rank, ident[:20], lat, lon, runways.get(a["ident"], [])))
    return shapes, towns, airports


def assemble(box, shapes, towns, airports):
    """The six lists write_pack takes, for one box, from read_sources' records (or load_regions')."""
    def touching(name):
        for _, props, rings in shapes[name]:
            if any(box.touches(r) for r in rings):
                yield props, rings

    land, lakes, rivers, roads = [], [], [], []
    for _, part in touching("ne_10m_land"):
        land += part  # outer rings and holes alike: the fill is even-odd
    for name in ("ne_10m_lakes", "ne_10m_lakes_north_america"):
        for _, part in touching(name):
            lakes += part
    for name in ("ne_10m_rivers_lake_centerlines", "ne_10m_rivers_north_america"):
        for props, part in touching(name):
            rivers += [(int(props.get("scalerank") or 0), part[0])]
    for props, part in touching("ne_10m_roads"):
        t = props.get("type")
        if t in MAJOR_TYPES:
            roads.append((K_MAJOR, int(props.get("scalerank") or 0), part[0]))
        elif t in MINOR_TYPES:
            roads.append((K_ROAD, int(props.get("scalerank") or 0), part[0]))

    town_list = []
    for _, name, lat, lon, pop in towns:
        if box.has(lat, lon):
            rank = next(i for i, floor in enumerate(TOWN_FLOORS) if pop >= floor)
            town_list.append((rank, pop, name, lat, lon))
    town_list.sort(key=lambda t: -t[1])
    airport_list = [(rank, name, lat, lon, rw) for _, rank, name, lat, lon, rw in airports if box.has(lat, lon)]
    airport_list.sort(key=lambda a: a[0])
    return land, lakes, rivers, roads, town_list, airport_list


def load_sources(cache, box):
    return assemble(box, *read_sources(cache, box))


def check_box(box):
    """Raises ValueError for a coverage square the pack format can't hold."""
    if box.lon0 < -180 or box.lon1 > 180 or box.lat0 < -85 or box.lat1 > 85:
        raise ValueError("the coverage square crosses longitude +-180 or latitude +-85; not supported")


# ------------------------------------------------------------------ geometry

def simplify_line(line, tol):
    """Douglas-Peucker on an open line (tolerance in degrees)."""
    if tol <= 0 or len(line) < 3:
        return line
    keep = [False] * len(line)
    keep[0] = keep[-1] = True
    stack = [(0, len(line) - 1)]
    while stack:
        i, j = stack.pop()
        (ax, ay), (bx, by) = line[i], line[j]
        dx, dy = bx - ax, by - ay
        norm = math.hypot(dx, dy)
        best, at = -1.0, -1
        for k in range(i + 1, j):
            px, py = line[k]
            d = abs(dy * (px - ax) - dx * (py - ay)) / norm if norm else math.hypot(px - ax, py - ay)
            if d > best:
                best, at = d, k
        if at >= 0 and best > tol:
            keep[at] = True
            stack += [(i, at), (at, j)]
    return [p for p, k in zip(line, keep) if k]


def clip_line(line, x0, y0, x1, y1):
    """Pieces of an open line inside the box (Liang-Barsky per segment, joined where they touch)."""
    pieces, cur = [], []
    for (ax, ay), (bx, by) in zip(line, line[1:]):
        t0, t1, dx, dy = 0.0, 1.0, bx - ax, by - ay
        ok = True
        for p, q in ((-dx, ax - x0), (dx, x1 - ax), (-dy, ay - y0), (dy, y1 - ay)):
            if p == 0:
                if q < 0:
                    ok = False
                    break
                continue
            r = q / p
            if p < 0:
                t0 = max(t0, r)
            else:
                t1 = min(t1, r)
            if t0 > t1:
                ok = False
                break
        if not ok:
            if cur:
                pieces.append(cur)
                cur = []
            continue
        a = (ax + t0 * dx, ay + t0 * dy)
        b = (ax + t1 * dx, ay + t1 * dy)
        if cur and cur[-1] == a:
            cur.append(b)
        else:
            if cur:
                pieces.append(cur)
            cur = [a, b]
        if t1 < 1.0:
            pieces.append(cur)
            cur = []
    if cur:
        pieces.append(cur)
    return [p for p in pieces if len(p) >= 2]


class Grid:
    """One level's tiles: square in degrees, origin at the coverage's south-west corner."""

    def __init__(self, box, milli):
        self.deg = milli / 1000
        self.lat0 = math.floor(box.lat0 / self.deg) * self.deg
        self.lon0 = math.floor(box.lon0 / self.deg) * self.deg
        self.rows = math.ceil((box.lat1 - self.lat0) / self.deg)
        self.cols = math.ceil((box.lon1 - self.lon0) / self.deg)

    def cell(self, lat, lon):
        return int((lat - self.lat0) // self.deg), int((lon - self.lon0) // self.deg)

    def bounds(self, row, col):
        lon0, lat0 = self.lon0 + col * self.deg, self.lat0 + row * self.deg
        return lon0, lat0, lon0 + self.deg, lat0 + self.deg

    def local(self, row, col, lon, lat):
        lon0, lat0, _, _ = self.bounds(row, col)
        x = min(UNITS, max(0, round((lon - lon0) / self.deg * UNITS)))
        y = min(UNITS, max(0, round((lat - lat0) / self.deg * UNITS)))
        return x, y

    def cells_of(self, pts):
        xs, ys = [p[0] for p in pts], [p[1] for p in pts]
        r0, c0 = self.cell(min(ys), min(xs))
        r1, c1 = self.cell(max(ys), max(xs))
        for r in range(max(0, r0), min(self.rows - 1, r1) + 1):
            for c in range(max(0, c0), min(self.cols - 1, c1) + 1):
                yield r, c


def ring_pieces(grid, ring):
    """{(row, col): quantised ring [(x, y, cut)]}, cut meaning the edge into the point is a tile cut."""
    out = {}
    for row, col in grid.cells_of(ring):
        lon0, lat0, lon1, lat1 = grid.bounds(row, col)
        piece = clip(clip(clip(clip(ring, 1, lat0, False), 1, lat1, True), 0, lon0, False), 0, lon1, True)
        if len(piece) < 3:
            continue

        def on_border(a, b):
            return any(a[ax] == b[ax] and a[ax] in ((lon0, lon1) if ax == 0 else (lat0, lat1)) for ax in (0, 1))

        q = []
        for i, p in enumerate(piece):
            x, y = grid.local(row, col, p[0], p[1])
            if q and q[-1][:2] == (x, y):
                continue
            q.append((x, y, on_border(piece[i - 1], p)))
        while len(q) > 1 and q[0][:2] == q[-1][:2]:
            last = q.pop()
            q[0] = (q[0][0], q[0][1], last[2])
        if len(q) >= 3:
            out.setdefault((row, col), []).append(q)
    return out


def is_full(q):
    s = sum(x0 * y1 - x1 * y0 for (x0, y0, _), (x1, y1, _) in zip(q, q[1:] + q[:1]))
    return all(c for _, _, c in q) and abs(s) == 2 * UNITS * UNITS


def encode_line(pts):
    """Like encode_ring, without cut flags: u16 count, u16 x0, u16 y0, steps."""
    out = bytearray(struct.pack("<HHH", len(pts), pts[0][0], pts[0][1]))
    px, py = pts[0]
    for x, y in pts[1:]:
        dx, dy = x - px, y - py
        if -125 <= dx <= 127 and -128 <= dy <= 127:
            out += struct.pack("<bb", dx, dy)
        elif -2048 <= dx <= 2047 and -2048 <= dy <= 2047:
            out.append(0x82)
            out += ((dx & 0xFFF) | (dy & 0xFFF) << 12).to_bytes(3, "little")
        else:
            out.append(0x80)
            out += struct.pack("<hh", dx, dy)
        px, py = x, y
    return out


def encode_point(x, y, name):
    b = name.encode("utf-8")[:20]
    while True:  # never cut a UTF-8 sequence in half, nor end on a space
        try:
            b.decode("utf-8")
        except UnicodeDecodeError:
            b = b[:-1]
            continue
        if b.endswith(b" "):
            b = b.rstrip()
            continue
        break
    return struct.pack("<HHB", x, y, len(b)) + b


def encode_tile(features):
    """features: [(kind, rank, data bytes, ymin, ymax)] in drawing order → tile bytes."""
    head = 4 + 8 * len(features)
    table, body = bytearray(), bytearray()
    for kind, rank, data, ymin, ymax in features:
        table += struct.pack("<BBHhh", kind, rank, head + len(body), ymin, ymax)
        body += data
    rest = struct.pack("<H", len(features)) + table + body
    assert len(rest) + 2 < 0x10000, "tile too large; use a smaller tile size"
    return struct.pack("<H", crc16(rest)) + rest


# ------------------------------------------------------------------ levels

def build_level(grid, level, src, metres):
    land, lakes, rivers, roads, towns, airports = src
    tol = metres / 111320.0
    tiles = {}  # (row, col) -> [(kind, rank, data, ymin, ymax)]

    def add(cell, kind, rank, data, ys):
        tiles.setdefault(cell, []).append((kind, rank, data, min(ys), max(ys)))

    full = set()
    land_q, lake_q = {}, {}
    for rings, into in ((land, land_q), (lakes, lake_q)):
        for ring in rings:
            for cell, qs in ring_pieces(grid, simplify(ring, tol)).items():
                into.setdefault(cell, []).extend(qs)
    for cell in set(land_q) | set(lake_q):
        lq, kq = land_q.get(cell, []), lake_q.get(cell, [])
        if not kq and len(lq) == 1 and is_full(lq[0]):
            full.add(cell)
            continue
        for q in lq + kq:
            add(cell, K_RING, 0, encode_ring(q), [p[1] for p in q])

    def add_line(kind, rank, line):
        for row, col in grid.cells_of(line):
            lon0, lat0, lon1, lat1 = grid.bounds(row, col)
            for piece in clip_line(line, lon0, lat0, lon1, lat1):
                q = []
                for lon, lat in piece:
                    p = grid.local(row, col, lon, lat)
                    if not q or q[-1] != p:
                        q.append(p)
                if len(q) >= 2:
                    add((row, col), kind, rank, encode_line(q), [p[1] for p in q])

    for scalerank, line in rivers:
        if level > 0 and scalerank > 8:
            continue
        add_line(K_RIVER, min(scalerank, 255), simplify_line(line, tol))
    for kind in (K_ROAD, K_MAJOR):  # minor first, so major roads draw over them
        for k, scalerank, line in roads:
            if k != kind or (level == 1 and kind == K_ROAD and scalerank > 7) or (level == 2 and kind == K_ROAD):
                continue
            add_line(kind, min(scalerank, 255), simplify_line(line, tol))
    for rank, ident, lat, lon, rws in airports:
        if level > 0 and rank == 2:
            continue
        for a, b in rws:
            add_line(K_RUNWAY, rank, [a, b])
    for rank, ident, lat, lon, _ in airports:
        if level > 0 and rank == 2:
            continue
        cell = grid.cell(lat, lon)
        x, y = grid.local(*cell, lon, lat)
        add(cell, K_AIRPORT, rank, encode_point(x, y, ident), [y])
    for rank, pop, name, lat, lon in towns:
        if (level == 1 and rank > 2) or (level == 2 and rank > 0):
            continue
        cell = grid.cell(lat, lon)
        if not (0 <= cell[0] < grid.rows and 0 <= cell[1] < grid.cols):
            continue
        x, y = grid.local(*cell, lon, lat)
        add(cell, K_TOWN, rank, encode_point(x, y, name), [y])
    order = {K_RING: 0, K_RIVER: 1, K_ROAD: 2, K_MAJOR: 3, K_RUNWAY: 4, K_AIRPORT: 5, K_TOWN: 6}
    encoded = {cell: encode_tile(sorted(f, key=lambda t: order[t[0]])) for cell, f in tiles.items()}
    return full, encoded


def write_pack(path, lat, lon, cover_nm, src, progress=None):
    """Writes the pack for one home; progress(level) is called before each level is built (the SD card page shows it)."""
    box = Box(lat, lon, cover_nm)
    levels = []
    for i, (max_r, milli, metres) in enumerate(LEVELS):
        if progress:
            progress(i)
        grid = Grid(box, milli)
        full, tiles = build_level(grid, i, src, metres)
        levels.append((max_r, milli, grid, full, tiles))
    table_at = 64
    body = bytearray()
    level_table = bytearray()
    at = table_at + 24 * len(levels)
    indexes = []
    for max_r, milli, grid, full, tiles in levels:
        index = bytearray(8 * grid.rows * grid.cols)
        indexes.append((at, index))
        at += len(index)
    blobs = []
    for (max_r, milli, grid, full, tiles), (idx_at, index) in zip(levels, indexes):
        for row in range(grid.rows):
            for col in range(grid.cols):
                e = 8 * (row * grid.cols + col)
                data = tiles.get((row, col))
                flags = 1 if (row, col) in full else 0
                if data:
                    struct.pack_into("<IHBB", index, e, at, len(data), flags, 0)
                    blobs.append(data)
                    at += len(data)
                else:
                    struct.pack_into("<IHBB", index, e, 0, 0, flags, 0)
        level_table += struct.pack("<HHiiHHIHH", max_r, milli, round(grid.lat0 * 1e5), round(grid.lon0 * 1e5),
                                   grid.rows, grid.cols, idx_at, crc16(index), 0)
    body = level_table + b"".join(i for _, i in indexes) + b"".join(blobs)
    cache_at = (table_at + len(body) + 4095) // 4096 * 4096
    head = bytearray(64)
    struct.pack_into("<4sHHiiHBBIII", head, 0, b"CMAP", 1, 64, round(lat * 1e5), round(lon * 1e5), cover_nm,
                     len(levels), 0, zlib.crc32(body), cache_at, CACHE_SIZE)
    struct.pack_into("<H", head, 32, crc16(level_table))  # the level table has no CRC of its own
    struct.pack_into("<H", head, 62, crc16(head[:62]))
    with open(path, "wb") as f:
        f.write(head)
        f.write(body)
        f.write(bytes(cache_at - 64 - len(body)))
        f.write(bytes(CACHE_SIZE))
    return levels, cache_at + CACHE_SIZE


def selftest():
    q = [(0, 0, True), (UNITS, 0, True), (UNITS, UNITS, True), (0, UNITS, True)]
    assert is_full(q)
    line = [(0.0, 0.0), (1.0, 1.0), (2.0, 0.0)]
    assert clip_line(line, 0.5, -1, 1.5, 2) == [[(0.5, 0.5), (1.0, 1.0), (1.5, 0.5)]]
    assert simplify_line([(0, 0), (1, 0.0001), (2, 0)], 0.01) == [(0, 0), (2, 0)]
    t = encode_tile([(K_TOWN, 2, encode_point(5, 6, "Frederick"), 6, 6)])
    assert struct.unpack_from("<H", t, 0)[0] == crc16(t[2:])
    assert encode_point(1, 2, "é" * 11)[4] == 20  # cut on a character boundary
    assert encode_point(1, 2, "A" * 19 + " B")[4] == 19  # cut at 20 bytes would end on a space
    for ident, gps, icao, want in (("KDMW", "KDMW", "", "KDMW"), ("KS12", "KS12", "", None), ("4MD8", "4MD8", "", None),
                                   ("MD77", "MD77", "", None), ("KY00", "KY00", "", None), ("KW05", "KW05", "", "KW05"),
                                   ("X", "", "EGLL", "EGLL")):
        assert public_code({"ident": ident, "gps_code": gps, "icao_code": icao}) == want, ident
    assert airport_name({"ident": "00A", "gps_code": "", "icao_code": ""}, 1) == "00A"
    print("selftest ok")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--lat", type=float)
    ap.add_argument("--lon", type=float)
    ap.add_argument("--cover", type=int, default=COVER, help="nm from home to each side of the square")
    ap.add_argument("--out", default="map.pak")
    ap.add_argument("--cache", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), ".cache", "map"))
    ap.add_argument("--refresh", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if a.lat is None or a.lon is None:
        ap.error("--lat and --lon are required")
    box = Box(a.lat, a.lon, a.cover)
    try:
        check_box(box)
    except ValueError as e:
        sys.exit(f"error: {e}")
    fetch(a.cache, a.refresh)
    src = load_sources(a.cache, box)
    levels, size = write_pack(a.out, a.lat, a.lon, a.cover, src)
    for (max_r, milli, grid, full, tiles), i in zip(levels, range(len(levels))):
        print(f"level {i}: <= {max_r} nm, {milli / 1000:g} deg tiles, {grid.rows}x{grid.cols}, "
              f"{len(tiles)} tiles, {sum(map(len, tiles.values())) / 1e6:.2f} MB, {len(full)} all land")
        counts = {}
        for t in tiles.values():
            for k in range(struct.unpack_from("<H", t, 2)[0]):
                kind = t[4 + 8 * k]
                counts[kind] = counts.get(kind, 0) + 1
        print("  features: " + ", ".join(f"{KIND_NAMES[k]} {n}" for k, n in sorted(counts.items())))
    print(f"{a.out}: {size / 1e6:.2f} MB ({len(src[4])} towns, {len(src[5])} airports)")


if __name__ == "__main__":
    main()
