# Contrail 5b — Detailed Map Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** With `/contrail/map.pak` on the SD card, the Live Map shows sharp coastlines and lakes, rivers, roads, towns and every public airfield around home, at any radius and in both themes; without it the map is exactly today's.

**Architecture:** A Python builder cuts Natural Earth 1:10m, OurAirports and GeoNames data around home into a three-level tiled pack with a reserved area at the end. On the board, `MapBase` paints the chart strip by strip from the pack's tiles (land by an XOR sub-scanline fill, lines anti-aliased, names placed once per rebuild) and writes each finished strip into the reserved area; normal frames read the saved chart back from the card instead of repainting. Any problem falls back to the built-in map.

**Tech Stack:** C++17 (PlatformIO, ESP32 Arduino and native test env with Unity), Python 3 (tools venv: Pillow only), our own SD/FAT stack from 5a.

**Spec:** `docs/superpowers/specs/2026-11-05-contrail-detailed-map-design.md`. Where this plan refines the spec (tile layout, strip scratch, fixture), Task 13 writes the *As built* notes into the spec.

**Refinements to the spec, decided while planning** (Task 13 records them):
1. **Tile layout gains a feature table** (Task 1): each tile starts with one 8-byte entry per feature (kind, byte offset, y range), so a strip reads only the features crossing it instead of the whole tile 17 times. Lines are cut at tile borders (no overflow of tile units). Points are features too.
2. **Land coverage is 2×2 supersampled, painted in half-strips of 8 rows** (Task 9): the scratch is 960 B, so it and 32 label marks fit in the arena space the built-in mask uses. Coastline strokes on top hide the steps.
3. **Tile data is read through a small stream** (Task 5), not a pointer: tiles live on the card, and there is no RAM to load one whole.
4. **The fixture is a built pack, not a checked-in source set** (Task 3): `map_small.pak`, 40 nm around a fixed point, plus the command that rebuilds it.
5. **"Drawing map…" shows in the bottom strip** (Task 10), not over the chart, so it is never saved into the chart.
6. **A tile's CRC is checked once per rebuild, not per read** (Tasks 6, 9): `layout()` verifies the view's tiles and remembers bad ones; painting reads without re-checking. Checking on every half-strip would read each tile dozens of times.
7. **The saved chart also holds the label marks** (Task 7): 1,440 B beside the 32-byte key (the reserved area had the room), so opening the map from a matching saved chart reads no tiles at all. The rows come first in the area, the key and marks after them, so a 16-row strip on a multiple of 16 rows is exactly 15 sectors with no read-modify-write.
8. **An all-land tile with bad data stays land, without its roads** (Task 9): the spec's §7 says a tile that fails its CRC is left as water, but the all-land flag is in the (checked) index, so land is still known; only the tile's rings and lines are dropped.
9. **The sector cache is two-way LRU** (`FatVolume`, +512 B static): index, feature-table and data sectors stop evicting one another, which removes 60-67% of a rebuild's block reads with no painter change.

## Commands used throughout

- Native tests (from `firmware/`): `pio test -e test` (all), `pio test -e test -f test_map_pack` (one suite).
- Board build: `pio run -e cyd2usb`. Sim build: `pio run -e sim`.
- Tools venv: `tools/.venv/bin/python -I tools/<script>.py …` (from the repo root).
- Golden refresh: `UPDATE_GOLDEN=1 pio test -e test -f <suite>`, then look at the PNGs (`tools/ppm2png.py`) before committing.
- Run one `pio` command at a time (they share `.pio/`).
- Commit messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Work on branch `detailed-map` (already created; the spec is its first commit).

## File map

| File | Status | Responsibility |
|---|---|---|
| `tools/build_map.py` | new | sources → `map.pak` (download cache, filters, simplify, tile, encode, index, CRCs) |
| `tools/map_preview.py` | new | `map.pak` → PNG at a radius and theme (reference look, builder check) |
| `.gitignore` | modify | `tools/.cache/` |
| `firmware/test/fixtures/map_small.pak` | new | 40 nm test pack |
| `firmware/lib/hal/hal.h` | modify | `IFile::writeAt` |
| `firmware/lib/core/assets/SdProtocol.h` | modify | data-response decoding for writes |
| `firmware/lib/core/assets/FatReader.{h,cpp}` | modify | `IBlockDevice::writeBlock`, `FatFile::writeAt`, two file slots |
| `firmware/lib/hal_esp32/SdSpi.{h,cpp}` | modify | CMD24 single-block write |
| `firmware/lib/hal_native/NativeStorage.{h,cpp}` | modify | read-write files |
| `firmware/test/common/TestStorage.h` | modify | `MemFile::writeAt` |
| `firmware/lib/core/map/MapPack.{h,cpp}` | new | pack header, levels, index, tile and feature-table reads |
| `firmware/lib/core/map/Features.{h,cpp}` | new | `ByteStream`; ring, line and point decoding from a stream |
| `firmware/lib/core/map/ChartCache.{h,cpp}` | new | the saved chart: key, rows in and out |
| `firmware/lib/core/map/DetailPainter.{h,cpp}` | new | one half-strip of the detailed chart; label marks |
| `firmware/lib/core/screens/Theme.{h,cpp}` | modify | `mapRoad`, `mapRoadMinor`, `mapRiver`, `mapTown` |
| `firmware/lib/core/screens/MapScreen.{h,cpp}` | modify | detailed or built-in, rebuild over frames, saved-chart reads, radius settle |
| `firmware/lib/core/screens/MapPlanes.cpp` | modify | label avoidance through `MapBase::chartMark`; hidden while drawing |
| `firmware/lib/core/screens/ScreenApp.{h,cpp}` | modify | `setMap(MapPack*)` |
| `firmware/src/main_esp32.cpp`, `src/main_sim.cpp` | modify | open the pack; `/api/info` map status |
| `firmware/web/index.html` | modify | map line on the Firmware card |
| `README.md`, `docs/HARDWARE_TESTING.md`, the spec | modify | how to build and install; checks; as built |
| tests: `test_map_pack`, `test_map_features`, `test_chart_cache`, `test_detail_painter`, `test_fat_reader`, `test_sd_protocol`, `test_map_screen` | new / modify | §9 of the spec |

---

### Task 1: The pack builder (`tools/build_map.py`)

**Files:**
- Create: `tools/build_map.py`
- Modify: `.gitignore` (add `tools/.cache/`)

The pack layout (refines spec §3; little-endian):

```
Header, 64 B
  0  "CMAP"
  4  u16 version = 1
  6  u16 headerSize = 64
  8  i32 homeLatE5, i32 homeLonE5
  16 u16 coverNm
  18 u8 levelCount = 3, u8 0
  20 u32 packId                CRC-32 (zlib) over everything from the level table to the end of tile data
  24 u32 cacheOffset, u32 cacheSize     4 KB aligned; cacheSize = 131072
  32 u16 levelsCrc             CRC-16/CCITT-FALSE over the levelCount x 24 B level table entries
  34 .. 61 zero
  62 u16 headerCrc             CRC-16/CCITT-FALSE over bytes 0..61
Level table at 64, levelCount × 24 B
  u16 maxRadiusNm, u16 tileMilliDeg, i32 originLatE5, i32 originLonE5, u16 rows, u16 cols,
  u32 indexOffset, u16 indexCrc (over rows*cols*8 index bytes), u16 0
Per level, at indexOffset: rows × cols entries of 8 B, row-major from the south-west
  u32 offset (0 = empty tile), u16 size, u8 flags (bit 0: all land), u8 0
Tile, at offset, size bytes:
  u16 crc                      CRC-16/CCITT-FALSE over the rest of the tile
  u16 featureCount
  featureCount × 8 B           u8 kind, u8 rank, u16 dataOffset (from tile start), i16 ymin, i16 ymax
  feature data
Feature kinds and data (tile-local units 0..2048, x east, y north):
  0 ring        today's basemap ring encoding (Tiles.h), cut flags included
  1 major road, 2 road, 3 river, 4 runway
                u16 count, u16 x0, u16 y0, then count-1 steps in the ring step encoding (no 0x81 cuts)
  16 town, 17 airport
                u16 x, u16 y, u8 nameLen, name bytes (UTF-8, ≤ 20 B; airports: ICAO ident)
  rank: towns 0 (≥ 300k people), 1 (≥ 100k), 2 (≥ 50k), 3 (≥ 25k), 4 (≥ 10k), 5 (≥ 5k); airports 0 large, 1 medium, 2 public small field;
        rivers and roads: Natural Earth scalerank; rings 0
Features are ordered: rings, rivers, roads (minor then major), runways, airports, towns.
Cache area at cacheOffset, cacheSize bytes of zeros.
```

Levels: `(maxRadiusNm, tileMilliDeg, tolerance m)` = `(25, 250, 40)`, `(60, 500, 100)`, `(100, 1000, 250)`. Filters per spec §5 (level 1: roads major + scalerank ≤ 7, rivers scalerank ≤ 8, towns ≥ 50k, airports large+medium; level 2: major roads, rivers ≤ 8, towns ≥ 300k, airports large+medium). Runways: level 0 for every shown airport, levels 1 and 2 for large and medium.

- [ ] **Step 1: Add the cache directory to `.gitignore`**

Append a line `tools/.cache/` to `/.gitignore`.

- [ ] **Step 2: Write `tools/build_map.py`**

```python
#!/usr/bin/env python3
"""Builds /contrail/map.pak, the Live Map's detailed map for one home (5b spec §2, §3).

  python3 -I tools/build_map.py --lat 39.4176 --lon -77.3743 --out map.pak

Sources are downloaded once into tools/.cache/map/ (about 50 MB) and reused; --refresh downloads them again.
Natural Earth 1:10m (public domain), OurAirports (public domain), GeoNames cities5000 (CC BY 4.0).
"""
import argparse
import binascii
import csv
import io
import json
import math
import os
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
CACHE_SIZE = 131072  # 270 x 240 RGB565, a 32-byte key and 1,440 bytes for marks
K_RING, K_MAJOR, K_ROAD, K_RIVER, K_RUNWAY, K_TOWN, K_AIRPORT = 0, 1, 2, 3, 4, 16, 17
MAJOR_TYPES = ("Major Highway", "Beltway")
MINOR_TYPES = ("Secondary Highway", "Road", "Bypass")


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
            while chunk := r.read(1 << 20):
                f.write(chunk)
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


def geo_features(path, box):
    with open(path, encoding="utf-8") as f:
        for feat in json.load(f)["features"]:
            g = feat.get("geometry")
            if not g:
                continue
            t, c = g["type"], g["coordinates"]
            parts = {"Polygon": [c], "MultiPolygon": c, "LineString": [[c]], "MultiLineString": [[l] for l in c]}.get(t, [])
            for part in parts:  # polygon: [outer, holes...]; line: [line]
                if any(box.touches(r) for r in part):
                    yield feat["properties"], [[(float(p[0]), float(p[1])) for p in r] for r in part]


def load_sources(cache, box):
    land, lakes, rivers, roads = [], [], [], []
    for _, part in geo_features(os.path.join(cache, "ne_10m_land.geojson"), box):
        land += part  # outer rings and holes alike: the fill is even-odd
    for name in ("ne_10m_lakes.geojson", "ne_10m_lakes_north_america.geojson"):
        for _, part in geo_features(os.path.join(cache, name), box):
            lakes += part
    for name in ("ne_10m_rivers_lake_centerlines.geojson", "ne_10m_rivers_north_america.geojson"):
        for props, part in geo_features(os.path.join(cache, name), box):
            rivers += [(int(props.get("scalerank") or 0), part[0])]
    for props, part in geo_features(os.path.join(cache, "ne_10m_roads.geojson"), box):
        t = props.get("type")
        if t in MAJOR_TYPES:
            roads.append((K_MAJOR, int(props.get("scalerank") or 0), part[0]))
        elif t in MINOR_TYPES:
            roads.append((K_ROAD, int(props.get("scalerank") or 0), part[0]))

    towns = []
    with zipfile.ZipFile(os.path.join(cache, "cities5000.zip")) as z:
        with z.open("cities5000.txt") as f:
            for row in csv.reader(io.TextIOWrapper(f, encoding="utf-8"), delimiter="\t"):
                lat, lon = float(row[4]), float(row[5])
                if box.has(lat, lon) and row[7].startswith("PPL"):
                    pop = int(row[14] or 0)
                    rank = 0 if pop >= 300000 else 1 if pop >= 50000 else 2
                    towns.append((rank, pop, row[2], lat, lon))
    towns.sort(key=lambda t: -t[1])

    runways = {}
    with open(os.path.join(cache, "runways.csv"), encoding="utf-8") as f:
        for r in csv.DictReader(f):
            try:
                runways.setdefault(r["airport_ident"], []).append(
                    ((float(r["le_longitude_deg"]), float(r["le_latitude_deg"])),
                     (float(r["he_longitude_deg"]), float(r["he_latitude_deg"]))))
            except ValueError:
                pass
    airports = []
    rank_of = {"large_airport": 0, "medium_airport": 1, "small_airport": 2}
    with open(os.path.join(cache, "airports.csv"), encoding="utf-8") as f:
        for a in csv.DictReader(f):
            rank = rank_of.get(a["type"])
            if rank is None:
                continue
            lat, lon = float(a["latitude_deg"]), float(a["longitude_deg"])
            if not box.has(lat, lon):
                continue
            ident = a.get("icao_code") or (a["ident"] if rank < 2 else "")
            if rank == 2 and not a.get("icao_code"):
                continue  # private strips have no ICAO code: never included
            airports.append((rank, ident[:20], lat, lon, runways.get(a["ident"], [])))
    airports.sort(key=lambda a: a[0])
    return land, lakes, rivers, roads, towns, airports


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
    while True:  # never cut a UTF-8 sequence in half
        try:
            b.decode("utf-8")
            break
        except UnicodeDecodeError:
            b = b[:-1]
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
        if (level == 1 and rank > 1) or (level == 2 and rank > 0):
            continue
        cell = grid.cell(lat, lon)
        if not (0 <= cell[0] < grid.rows and 0 <= cell[1] < grid.cols):
            continue
        x, y = grid.local(*cell, lon, lat)
        add(cell, K_TOWN, rank, encode_point(x, y, name), [y])
    order = {K_RING: 0, K_RIVER: 1, K_ROAD: 2, K_MAJOR: 3, K_RUNWAY: 4, K_AIRPORT: 5, K_TOWN: 6}
    encoded = {cell: encode_tile(sorted(f, key=lambda t: order[t[0]])) for cell, f in tiles.items()}
    return full, encoded


def write_pack(path, lat, lon, cover_nm, src):
    box = Box(lat, lon, cover_nm)
    levels = []
    for i, (max_r, milli, metres) in enumerate(LEVELS):
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
    print("selftest ok")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--lat", type=float)
    ap.add_argument("--lon", type=float)
    ap.add_argument("--cover", type=int, default=250, help="nm from home to each side of the square")
    ap.add_argument("--out", default="map.pak")
    ap.add_argument("--cache", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), ".cache", "map"))
    ap.add_argument("--refresh", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if a.lat is None or a.lon is None:
        ap.error("--lat and --lon are required")
    fetch(a.cache, a.refresh)
    src = load_sources(a.cache, Box(a.lat, a.lon, a.cover))
    levels, size = write_pack(a.out, a.lat, a.lon, a.cover, src)
    for (max_r, milli, grid, full, tiles), i in zip(levels, range(len(levels))):
        print(f"level {i}: <= {max_r} nm, {milli / 1000:g} deg tiles, {grid.rows}x{grid.cols}, "
              f"{len(tiles)} tiles, {sum(map(len, tiles.values())) / 1e6:.2f} MB, {len(full)} all land")
    print(f"{a.out}: {size / 1e6:.2f} MB ({len(src[4])} towns, {len(src[5])} airports)")


if __name__ == "__main__":
    main()
```

Check before running: `gen_map.py` must import without side effects (its `main()` is guarded by `if __name__ == "__main__"`); confirm with `grep -n "__main__" tools/gen_map.py`. `gen_map.clip` keeps exact border values, which `on_border` relies on.

- [ ] **Step 3: Run the self-test**

Run: `tools/.venv/bin/python -I tools/build_map.py --selftest`
Expected: `selftest ok`

- [ ] **Step 4: Build the pack for the board's home**

Run: `tools/.venv/bin/python -I tools/build_map.py --lat 39.4176 --lon -77.3743 --out /tmp/map.pak` (use the scratchpad, not `/tmp`, when running as an agent)
Expected: three `level` lines, a total between 5 and 15 MB, under a minute after the first download. If a tile is too large (assertion), halve that level's tile size in `LEVELS` and say so in the report.

- [ ] **Step 5: Commit**

```bash
git add tools/build_map.py .gitignore
git commit -m "Map pack builder: Natural Earth 10m, OurAirports and GeoNames around home"
```

---

### Task 2: The preview renderer (`tools/map_preview.py`)

**Files:**
- Create: `tools/map_preview.py`

It decodes a pack exactly as the firmware will (header, level choice, index, tile CRC, feature table) and draws the mockup look, so it is both the builder's check and the look's reference.

- [ ] **Step 1: Write `tools/map_preview.py`**

```python
#!/usr/bin/env python3
"""Renders a map.pak as the Live Map would show it: map_preview.py map.pak --radius 25 --theme cinematic -o out.png"""
import argparse
import math
import os
import struct
import sys

from PIL import Image, ImageDraw, ImageFont

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
            pts = [(x, y)]
            for dx, dy, _ in st:
                pts.append((pts[-1][0] + dx, pts[-1][1] + dy))
            feats.append((kind, rank, pts))
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
            ImageDraw.Draw(mask).rectangle([a, b], fill=1)
        for kind, _, pts in feats:
            if kind == K_RING:
                ring = Image.new("1", big.size, 0)
                ImageDraw.Draw(ring).polygon([P(r, c, x, y) for x, y in pts], fill=1)
                mask = Image.frombytes("1", big.size, bytes(a ^ b for a, b in zip(mask.tobytes(), ring.tobytes())))
    big.paste(Image.new("RGB", big.size, T["land"]), mask=mask)
    dr = ImageDraw.Draw(big)
    style = {K_RIVER: ("river", 1.2), K_ROAD: ("road2", 1.0), K_MAJOR: ("road", 1.5), K_RUNWAY: ("runway", 1.5)}
    for (r, c), (_, feats) in tiles.items():
        for kind, _, pts in feats:
            if kind == K_RING:  # coastline: every edge; the firmware skips tile cuts
                dr.line([P(r, c, x, y) for x, y in pts + pts[:1]], fill=T["coast"], width=SS)
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
                    key=lambda t: (t[0] == K_TOWN, t[1] if t[0] == K_AIRPORT else 0, t[1] if t[0] == K_TOWN else 0))
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
```

- [ ] **Step 2: Render the board's pack at four radii in both themes**

Run, for r in 10 25 50 100 and both themes: `tools/.venv/bin/python -I tools/map_preview.py <scratch>/map.pak --radius $r --theme cinematic -o <scratch>/prev_cin_$r.png`
Expected: no assertion; the images look like the approved mockups (`scratchpad/map5b/sheet.png` in the design session: roads, rivers, the Chesapeake at 50 and 100 nm, towns and airfields thinned by radius). Send the controller a contact sheet; differences from the mockups are acceptable only where the mockup used unfiltered data.

- [ ] **Step 3: Commit**

```bash
git add tools/map_preview.py
git commit -m "Map pack preview renderer: decodes map.pak as the firmware will"
```

---

### Task 3: The test fixture pack

**Files:**
- Create: `firmware/test/fixtures/map_small.pak`, `firmware/test/fixtures/MAP_SMALL.md`

- [ ] **Step 1: Build it**

Run: `tools/.venv/bin/python -I tools/build_map.py --lat 39.4176 --lon -77.3743 --cover 40 --out firmware/test/fixtures/map_small.pak`
Expected: under 1.5 MB. If larger, use `--cover 30` and note it; Task 10's goldens then use radii 10, 20 and 26 (26 picks level 1).

- [ ] **Step 2: Note how it was made**

`firmware/test/fixtures/MAP_SMALL.md`:

```markdown
# map_small.pak

Built by `tools/build_map.py --lat 39.4176 --lon -77.3743 --cover 40` from Natural Earth v5.1.2, OurAirports
and GeoNames cities5000 as downloaded on 2026-11-05. Rebuilding with newer OurAirports or GeoNames data
changes the bytes, so the detailed-map goldens (test_map_screen) and the counts in test_map_pack must be
refreshed with it.
```

- [ ] **Step 3: Commit**

```bash
git add firmware/test/fixtures/map_small.pak firmware/test/fixtures/MAP_SMALL.md
git commit -m "Test fixture: a 40 nm map pack"
```

---

### Task 4: Writing inside a file on the card

**Files:**
- Modify: `firmware/lib/hal/hal.h` (IFile)
- Modify: `firmware/lib/core/assets/SdProtocol.h`
- Modify: `firmware/lib/core/assets/FatReader.h`, `FatReader.cpp`
- Modify: `firmware/lib/hal_esp32/SdSpi.h`, `SdSpi.cpp`
- Modify: `firmware/lib/hal_native/NativeStorage.h`, `NativeStorage.cpp`
- Modify: `firmware/test/common/TestStorage.h`
- Test: `firmware/test/test_fat_reader/test_main.cpp`, `firmware/test/test_sd_protocol/test_main.cpp`

- [ ] **Step 1: Write the failing tests**

In `test_fat_reader/test_main.cpp`, give `MemDisk` a write and add tests (use the fixture images the suite already loads; `fat32_mbr.img.gz` holds `/CONTRAIL/LOGOS.PAK` in its existing tests: reuse whatever path and size constants those tests use, shown here as `kPath` / `kSeed`):

```cpp
// in struct MemDisk:
    int writes = 0;
    bool writeFail = false;
    bool writeBlock(uint32_t lba, const uint8_t* buf) override {
        ++writes;
        if (writeFail || (uint64_t(lba) + 1) * 512 > bytes.size()) return false;
        std::memcpy(bytes.data() + size_t(lba) * 512, buf, 512);
        return true;
    }

static void test_write_inside_a_file_leaves_the_rest_of_the_card_alone() {
    MemDisk d;
    d.bytes = gunzip("fat32_mbr.img.gz");
    const std::vector<uint8_t> before = d.bytes;
    assets::FatVolume v;
    TEST_ASSERT_TRUE(v.mount(&d));
    hal::IFile* f = v.open(kPath);
    TEST_ASSERT_NOT_NULL(f);
    // A run across a cluster boundary and not sector aligned.
    const uint32_t off = v.clusterBytes() - 700, n = 1500;
    std::vector<uint8_t> w(n);
    for (uint32_t i = 0; i < n; ++i) w[i] = uint8_t(i * 7 + 3);
    TEST_ASSERT_TRUE(f->writeAt(off, w.data(), n));
    std::vector<uint8_t> r(n);
    TEST_ASSERT_TRUE(f->readAt(off, r.data(), n));
    TEST_ASSERT_EQUAL_MEMORY(w.data(), r.data(), n);
    // Only the file's own data sectors changed: count changed bytes outside the written run's sectors.
    size_t changed = 0;
    for (size_t i = 0; i < d.bytes.size(); ++i) changed += d.bytes[i] != before[i];
    TEST_ASSERT_EQUAL(n, changed);  // the pattern never equals the fixture's bytes at the same spot
    // Bytes just outside the run still read as the file's pattern.
    uint8_t edge[2];
    TEST_ASSERT_TRUE(f->readAt(off - 1, edge, 1));
    TEST_ASSERT_TRUE(f->readAt(off + n, edge + 1, 1));
    TEST_ASSERT_TRUE(matches(edge, off - 1, 1, kSeed) && matches(edge + 1, off + n, 1, kSeed));
}

static void test_write_past_the_end_is_refused() {
    MemDisk d;
    d.bytes = gunzip("fat32_mbr.img.gz");
    assets::FatVolume v;
    TEST_ASSERT_TRUE(v.mount(&d));
    hal::IFile* f = v.open(kPath);
    uint8_t b[4] = {};
    TEST_ASSERT_FALSE(f->writeAt(f->size() - 2, b, 4));
    TEST_ASSERT_FALSE(f->writeAt(f->size() + 1, b, 1));
    TEST_ASSERT_EQUAL(0, d.writes);
}

static void test_write_failure_is_reported_and_leaves_no_stale_cache() {
    MemDisk d;
    d.bytes = gunzip("fat32_mbr.img.gz");
    assets::FatVolume v;
    TEST_ASSERT_TRUE(v.mount(&d));
    hal::IFile* f = v.open(kPath);
    uint8_t b[16];
    TEST_ASSERT_TRUE(f->readAt(10, b, 16));  // the sector is now in the cache
    uint8_t w[16] = {1, 2, 3};
    d.writeFail = true;
    TEST_ASSERT_FALSE(f->writeAt(10, w, 16));
    d.writeFail = false;
    TEST_ASSERT_TRUE(f->readAt(10, b, 16));
    TEST_ASSERT_TRUE(matches(b, 10, 16, kSeed));  // the card was not changed, and neither is what we read
}

static void test_two_files_open_at_once() {
    MemDisk d;
    d.bytes = gunzip("fat32_mbr.img.gz");
    assets::FatVolume v;
    TEST_ASSERT_TRUE(v.mount(&d));
    hal::IFile* a = v.open(kPath);
    hal::IFile* b = v.open(kSecondPath);  // any other file in the image the suite already names
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_TRUE(a != b);
}
```

If the image holds only one file, add a second file to `tools/make_fat_fixtures.sh` (`CONTRAIL/MAP.PAK`, 64 KB of pattern seed 7), regenerate the images and update any pinned values the suite's header comment warns about.

In `test_sd_protocol/test_main.cpp`:

```cpp
static void test_write_data_response() {
    using assets::sd::Write;
    TEST_ASSERT_EQUAL(int(Write::Ok), int(assets::sd::dataResponse(0xE5)));        // xxx0 0101: accepted
    TEST_ASSERT_EQUAL(int(Write::Signal), int(assets::sd::dataResponse(0xEB)));    // 0 101 1: CRC error
    TEST_ASSERT_EQUAL(int(Write::Refused), int(assets::sd::dataResponse(0xED)));   // 0 110 1: write error
    TEST_ASSERT_EQUAL(int(Write::Signal), int(assets::sd::dataResponse(0xFF)));    // no answer
}
```

Register each new test with `RUN_TEST` in the suites' `main`.

- [ ] **Step 2: Run them to see them fail**

Run: `pio test -e test -f test_fat_reader -f test_sd_protocol`
Expected: compile errors (`writeBlock`, `writeAt`, `dataResponse` not declared).

- [ ] **Step 3: Implement**

`hal.h`, in `IFile`:

```cpp
    // Overwrites n bytes inside the file (never past size(): files do not grow). False where writing is not
    // supported or failed; after a failure the bytes there are unknown.
    virtual bool writeAt(uint32_t offset, const void* buf, uint32_t n) { return false; }
```

`SdProtocol.h`, after `retrySlower`:

```cpp
// How a single-block write ended, from the data response token (xxx0sss1: 010 accepted, 101 CRC error, 110
// write error). A CRC error or no answer may be the clock (retry slower); a write error is the card's.
enum class Write : uint8_t { Ok, Signal, Refused };
inline Write dataResponse(uint8_t token) {
    if ((token & 0x11) != 0x01) return Write::Signal;  // not a data response at all
    const uint8_t s = token >> 1 & 7;
    return s == 2 ? Write::Ok : s == 5 ? Write::Signal : Write::Refused;
}
constexpr uint8_t kStartWrite = 0xFE;  // the data token before a single-block write (CMD24)
constexpr uint32_t kWriteBusyMs = 500;  // programming a block; SD cards may take up to 250 ms, some more
```

`FatReader.h`: in `IBlockDevice` add

```cpp
    // One block written; false when the write failed (or the device is read-only).
    virtual bool writeBlock(uint32_t lba, const uint8_t* buf512) { return false; }
```

In `FatFile` add `bool writeAt(uint32_t offset, const void* buf, uint32_t n) override;`, change `kMaxFiles` to 2 with the comment `// logos.pak and map.pak; each slot is ~190 B of static RAM`, and in `FatVolume` add a private `static void forget(IBlockDevice* dev, uint32_t lba);` and update the file comment's "one file slot" sentence to "two file slots (logos.pak, map.pak)" and "read-only" to "read-only except for overwriting bytes inside an existing file (map.pak's saved chart)".

`FatReader.cpp`:

```cpp
void FatVolume::forget(IBlockDevice* dev, uint32_t lba) {
    if (cache_.dev == dev && cache_.lba == lba) cache_.dev = nullptr;
}

// Overwrites bytes inside the file through its own cluster chain: never the FAT, a directory entry or the
// file's size. A partial sector is read, patched and written back.
bool FatFile::writeAt(uint32_t offset, const void* buf, uint32_t n) {
    if (!vol_ || !vol_->dev_ || offset > size_ || size_ - offset < n) return false;
    const uint32_t cb = vol_->clusterBytes();
    const uint8_t* in = static_cast<const uint8_t*>(buf);
    uint8_t sec[512];
    while (n) {
        const uint32_t clus = clusterAt(offset / cb);
        if (!clus) return false;
        const uint32_t inClus = offset % cb, inSec = inClus % 512;
        const uint32_t lba = vol_->clusterLba(clus) + inClus / 512;
        const uint32_t take = n < 512 - inSec ? n : 512 - inSec;
        const uint8_t* src = in;
        if (take != 512) {
            const uint8_t* s = vol_->sector(lba);
            if (!s) return false;
            std::memcpy(sec, s, 512);
            std::memcpy(sec + inSec, in, take);
            src = sec;
        }
        FatVolume::forget(vol_->dev_, lba);  // before the write: a failed write leaves the block unknown
        if (!vol_->dev_->writeBlock(lba, src)) return false;
        in += take, offset += take, n -= take;
    }
    return true;
}
```

`sec[512]` is on the caller's stack; the render task's stack has room (check `uxTaskGetStackHighWaterMark` in Task 12's board run).

`SdSpi.h`: declare `bool writeBlock(uint32_t lba, const uint8_t* buf512) override;` and private `assets::sd::Write writeBlockAt(uint32_t lba, const uint8_t* buf);`; change the class comment's "init and single-block reads, nothing else (read-only …)" to "init, single-block reads and single-block writes (writes only where FatFile::writeAt sends them: inside map.pak)".

`SdSpi.cpp`, after `readBlock`:

```cpp
Write SdSpi::writeBlockAt(uint32_t lba, const uint8_t* buf) {
    select();
    const uint8_t r1 = command(24, sdhc_ ? lba : lba * 512);
    if (r1 != 0x00) return fail(r1 == 0xFF ? "write: no answer (CMD24)" : "write refused (CMD24)"),
                           r1 == 0xFF ? Write::Signal : Write::Refused;
    sdBus.transfer(0xFF);  // one byte gap before the token
    sdBus.transfer(kStartWrite);
    sdBus.writeBytes(buf, 512);
    sdBus.transfer16(dataCrc(buf));
    const Write w = dataResponse(sdBus.transfer(0xFF));
    if (w != Write::Ok) return fail(w == Write::Signal ? "write CRC error" : "write error"), w;
    const uint32_t t0 = millis();
    while (sdBus.transfer(0xFF) == 0x00)  // busy while programming
        if (millis() - t0 > kWriteBusyMs) return fail("write timed out"), Write::Signal;
    deselect();
    return Write::Ok;
}

bool SdSpi::writeBlock(uint32_t lba, const uint8_t* buf) {
    if (!ready_) return false;
    const Write w = writeBlockAt(lba, buf);
    if (w == Write::Ok) return true;
    if (w != Write::Signal || hz_ <= kSafeHz) return false;
    hz_ = kSafeHz;
    return writeBlockAt(lba, buf) == Write::Ok;
}
```

Check before writing it: how `readBlockAt` applies `hz_` (the bus frequency is set in `select()` or per transaction); a frequency change must take effect the same way here. The card computes the data CRC only when CRC checking is on (CMD59); since command CRCs are off, the card ignores the data CRC on writes, which is fine.

`NativeStorage`: open files with `"r+b"` (falling back to `"rb"` when that fails, e.g. a read-only folder) and add to `NativeFile`:

```cpp
bool NativeFile::writeAt(uint32_t offset, const void* buf, uint32_t n) {
    if (offset > size_ || size_ - offset < n) return false;
    if (std::fseek(f_, long(offset), SEEK_SET) != 0) return false;
    if (std::fwrite(buf, 1, n, f_) != n) return false;
    return std::fflush(f_) == 0;
}
```

(declare it `override` in the header).

`TestStorage.h`, in `MemFile`:

```cpp
    bool writeFail = false;
    int writes = 0;
    bool writeAt(uint32_t off, const void* buf, uint32_t n) override {
        ++writes;
        if (fail || writeFail || off > bytes.size() || bytes.size() - off < n) return false;
        std::memcpy(bytes.data() + off, buf, n);
        return true;
    }
```

and in `MemStorage` a binary-safe adder: `MemFile& addBytes(const std::string& path, std::vector<uint8_t> data)`.

- [ ] **Step 4: Run the tests**

Run: `pio test -e test -f test_fat_reader -f test_sd_protocol`
Expected: PASS. Then `pio run -e cyd2usb` and `pio run -e sim`: both build.

- [ ] **Step 5: Commit**

```bash
git add -A firmware/lib/hal firmware/lib/core/assets firmware/lib/hal_esp32 firmware/lib/hal_native firmware/test tools/make_fat_fixtures.sh
git commit -m "SD: overwrite bytes inside an open file; two open files"
```

---

### Task 5: Reading features from a stream (`map/Features`)

**Files:**
- Create: `firmware/lib/core/map/Features.h`, `firmware/lib/core/map/Features.cpp`
- Test: `firmware/test/test_map_features/test_main.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// Feature decoding from a byte stream: the ring and line step encodings, points, truncation.
#include <unity.h>

#include <vector>

#include "TestStorage.h"
#include "map/Features.h"

void setUp() {}
void tearDown() {}

using namespace basemap;

static MemFile file(std::vector<uint8_t> b) {
    MemFile f;
    f.bytes = std::move(b);
    return f;
}

static void test_line_with_short_medium_and_long_steps() {
    // count 4, start (100, 200), steps (+5, -3) short, (+300, 0) medium, (+3000, -2500) long.
    std::vector<uint8_t> b = {4, 0, 100, 0, 200, 0, 5, 0xFD, 0x82};
    const uint32_t v = (300 & 0xFFF) | (0 & 0xFFF) << 12;
    b.push_back(uint8_t(v)), b.push_back(uint8_t(v >> 8)), b.push_back(uint8_t(v >> 16));
    b.push_back(0x80);
    b.push_back(uint8_t(3000 & 0xFF)), b.push_back(uint8_t(3000 >> 8));
    b.push_back(uint8_t(-2500 & 0xFF)), b.push_back(uint8_t((-2500 >> 8) & 0xFF));
    MemFile f = file(b);
    ByteStream s(f, 0, uint32_t(b.size()));
    LineReader r(s);
    TEST_ASSERT_EQUAL(4, r.count());
    int32_t x, y;
    TEST_ASSERT_TRUE(r.next(x, y)), TEST_ASSERT_EQUAL(100, x), TEST_ASSERT_EQUAL(200, y);
    TEST_ASSERT_TRUE(r.next(x, y)), TEST_ASSERT_EQUAL(105, x), TEST_ASSERT_EQUAL(197, y);
    TEST_ASSERT_TRUE(r.next(x, y)), TEST_ASSERT_EQUAL(405, x), TEST_ASSERT_EQUAL(197, y);
    TEST_ASSERT_TRUE(r.next(x, y)), TEST_ASSERT_EQUAL(3405, x), TEST_ASSERT_EQUAL(-2303, y);
    TEST_ASSERT_FALSE(r.next(x, y));
    TEST_ASSERT_TRUE(s.ok());
}

static void test_ring_reports_cuts_and_closes() {
    // 3 points, closing edge a cut (bit 15), the edge into point 2 a cut (0x81).
    std::vector<uint8_t> b = {3, 0x80, 0, 0, 0, 0, 10, 0, 0x81, 0, 10};
    MemFile f = file(b);
    ByteStream s(f, 0, uint32_t(b.size()));
    StreamRingReader r(s);
    RingPoint p;
    TEST_ASSERT_TRUE(r.next(p)), TEST_ASSERT_FALSE(p.cut);
    TEST_ASSERT_TRUE(r.next(p)), TEST_ASSERT_EQUAL(10, p.x), TEST_ASSERT_FALSE(p.cut);
    TEST_ASSERT_TRUE(r.next(p)), TEST_ASSERT_EQUAL(10, p.y), TEST_ASSERT_TRUE(p.cut);
    TEST_ASSERT_TRUE(r.next(p)), TEST_ASSERT_EQUAL(0, p.x), TEST_ASSERT_EQUAL(0, p.y), TEST_ASSERT_TRUE(p.cut);
    TEST_ASSERT_FALSE(r.next(p));
}

static void test_point_name_is_bounded_and_terminated() {
    std::vector<uint8_t> b = {7, 0, 9, 0, 5, 'K', 'F', 'D', 'K', 'X'};
    MemFile f = file(b);
    ByteStream s(f, 0, uint32_t(b.size()));
    Point p;
    TEST_ASSERT_TRUE(readPoint(s, p));
    TEST_ASSERT_EQUAL(7, p.x), TEST_ASSERT_EQUAL(9, p.y);
    TEST_ASSERT_EQUAL_STRING("KFDKX", p.name);
}

static void test_truncated_data_stops_cleanly() {
    std::vector<uint8_t> b = {5, 0, 1, 0, 1, 0, 2, 2};  // promises 5 points, holds 2
    MemFile f = file(b);
    ByteStream s(f, 0, uint32_t(b.size()));
    LineReader r(s);
    int32_t x, y;
    int n = 0;
    while (r.next(x, y)) ++n;
    TEST_ASSERT_EQUAL(2, n);
    TEST_ASSERT_FALSE(s.ok());
}

static void test_failed_reads_stop_cleanly() {
    std::vector<uint8_t> b(100, 0);
    MemFile f = file(b);
    f.fail = true;
    ByteStream s(f, 0, 100);
    TEST_ASSERT_EQUAL(0, s.u8());
    TEST_ASSERT_FALSE(s.ok());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_line_with_short_medium_and_long_steps);
    RUN_TEST(test_ring_reports_cuts_and_closes);
    RUN_TEST(test_point_name_is_bounded_and_terminated);
    RUN_TEST(test_truncated_data_stops_cleanly);
    RUN_TEST(test_failed_reads_stop_cleanly);
    return UNITY_END();
}
```

- [ ] **Step 2: Run it to see it fail**

Run: `pio test -e test -f test_map_features`
Expected: compile error, `map/Features.h` not found.

- [ ] **Step 3: Implement `map/Features.h`**

```cpp
#pragma once
// Reading a map pack tile's features from the card (5b spec §3): a small buffered stream over an IFile, and
// readers for rings (the built-in map's encoding, Tiles.h), lines (the same steps, no cuts) and points.
// Nothing is loaded whole: a tile can be several KB and the chart has no RAM for it.
#include <cstdint>

#include "Tiles.h"
#include "hal.h"

namespace basemap {

// Bytes [begin, end) of a file, 32 at a time. After a short or failed read every byte reads as 0 and ok()
// turns false; readers stop on it.
class ByteStream {
   public:
    ByteStream(hal::IFile& f, uint32_t begin, uint32_t end) : f_(f), pos_(begin), end_(end) {}
    uint8_t u8() {
        if (at_ == got_ && !fill()) return 0;
        return buf_[at_++];
    }
    uint16_t u16() { return uint16_t(u8() | u8() << 8); }
    int16_t i16() { return int16_t(u16()); }
    void seek(uint32_t pos) { pos_ = pos, at_ = got_ = 0; }
    uint32_t tell() const { return pos_ - (got_ - at_); }
    bool ok() const { return ok_; }

   private:
    bool fill();
    hal::IFile& f_;
    uint32_t pos_, end_;
    uint8_t buf_[32];
    uint8_t at_ = 0, got_ = 0;
    bool ok_ = true;
};

// One step: short (int8 dx, dy), medium (0x82, int12 pair), long (0x80, int16 pair). `lead` is the byte
// already read.
void readStep(ByteStream& s, uint8_t lead, int32_t& dx, int32_t& dy);

// A ring, as RingReader: after the last point it reports the first again with the closing edge's cut flag.
class StreamRingReader {
   public:
    explicit StreamRingReader(ByteStream& s);
    bool next(RingPoint& out);
    int count() const { return count_; }

   private:
    ByteStream& s_;
    int count_ = 0, left_ = 0;
    int32_t x0_ = 0, y0_ = 0, x_ = 0, y_ = 0;
    bool closingCut_ = false, started_ = false, closed_ = false;
};

// An open line: u16 count, u16 x0, u16 y0, then count - 1 steps.
class LineReader {
   public:
    explicit LineReader(ByteStream& s);
    bool next(int32_t& x, int32_t& y);
    int count() const { return count_; }

   private:
    ByteStream& s_;
    int count_ = 0, left_ = 0;
    int32_t x_ = 0, y_ = 0;
    bool started_ = false;
};

// A town or airport: u16 x, u16 y, u8 length, name.
struct Point {
    uint16_t x = 0, y = 0;
    char name[21] = "";
};
bool readPoint(ByteStream& s, Point& out);

}  // namespace basemap
```

`map/Features.cpp`:

```cpp
#include "Features.h"

namespace basemap {

bool ByteStream::fill() {
    if (!ok_ || pos_ >= end_) return ok_ = false;
    const uint32_t n = end_ - pos_ < sizeof buf_ ? end_ - pos_ : uint32_t(sizeof buf_);
    if (!f_.readAt(pos_, buf_, n)) return ok_ = false;
    pos_ += n, at_ = 0, got_ = uint8_t(n);
    return true;
}

void readStep(ByteStream& s, uint8_t lead, int32_t& dx, int32_t& dy) {
    if (lead == 0x80) {
        dx = s.i16(), dy = s.i16();
    } else if (lead == 0x82) {
        const uint32_t v = s.u8() | uint32_t(s.u8()) << 8 | uint32_t(s.u8()) << 16;
        dx = int32_t((v & 0xFFF) ^ 0x800) - 0x800, dy = int32_t((v >> 12 & 0xFFF) ^ 0x800) - 0x800;
    } else {
        dx = int8_t(lead), dy = int8_t(s.u8());
    }
}

StreamRingReader::StreamRingReader(ByteStream& s) : s_(s) {
    const uint16_t head = s.u16();
    count_ = left_ = head & 0x7FFF;
    closingCut_ = head & 0x8000;
    x0_ = x_ = s.u16(), y0_ = y_ = s.u16();
}

bool StreamRingReader::next(RingPoint& out) {
    if (closed_ || !s_.ok() || count_ < 1) return false;
    if (!started_) {
        started_ = true, --left_;
        out = {x_, y_, false};
        return true;
    }
    if (left_ == 0) {
        closed_ = true;
        out = {x0_, y0_, closingCut_};
        return true;
    }
    uint8_t lead = s_.u8();
    const bool cut = lead == 0x81;
    if (cut) lead = s_.u8();
    int32_t dx, dy;
    readStep(s_, lead, dx, dy);
    if (!s_.ok()) return false;
    x_ += dx, y_ += dy, --left_;
    out = {x_, y_, cut};
    return true;
}

LineReader::LineReader(ByteStream& s) : s_(s) {
    count_ = left_ = s.u16();
    x_ = s.u16(), y_ = s.u16();
}

bool LineReader::next(int32_t& x, int32_t& y) {
    if (left_ <= 0 || !s_.ok()) return false;
    if (started_) {
        int32_t dx, dy;
        readStep(s_, s_.u8(), dx, dy);
        if (!s_.ok()) return false;
        x_ += dx, y_ += dy;
    }
    started_ = true, --left_;
    x = x_, y = y_;
    return true;
}

bool readPoint(ByteStream& s, Point& out) {
    out.x = s.u16(), out.y = s.u16();
    int n = s.u8();
    int k = 0;
    for (int i = 0; i < n; ++i) {
        const char c = char(s.u8());
        if (k < int(sizeof out.name) - 1) out.name[k++] = c;
    }
    out.name[k] = 0;
    return s.ok();
}

}  // namespace basemap
```

Check `RingPoint`'s field order in `Tiles.h` (`x, y, cut`) matches the brace initialisers.

- [ ] **Step 4: Run the test**

Run: `pio test -e test -f test_map_features`
Expected: PASS (5 tests).

- [ ] **Step 5: Commit**

```bash
git add firmware/lib/core/map/Features.h firmware/lib/core/map/Features.cpp firmware/test/test_map_features
git commit -m "Map: read rings, lines and points from a stream"
```

---

### Task 6: The pack reader (`map/MapPack`)

**Files:**
- Create: `firmware/lib/core/map/MapPack.h`, `firmware/lib/core/map/MapPack.cpp`
- Test: `firmware/test/test_map_pack/test_main.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// MapPack over the fixture (map_small.pak, 40 nm around 39.4176, -77.3743) and corrupted copies of it.
#include <unity.h>

#include <cstring>

#include "Fixture.h"
#include "TestStorage.h"
#include "map/MapPack.h"

void setUp() {}
void tearDown() {}

using basemap::MapPack;

static MemStorage sd;

static MemFile& card(const std::string& bytes) { return sd.add(MapPack::kPath, bytes); }

static void test_opens_the_fixture() {
    card(loadFixture("map_small.pak"));
    MapPack p;
    TEST_ASSERT_TRUE(p.begin(&sd));
    TEST_ASSERT_EQUAL_STRING("", p.error());
    TEST_ASSERT_EQUAL(3, p.levelCount());
    TEST_ASSERT_EQUAL(40, p.coverNm());
    TEST_ASSERT_INT32_WITHIN(1, 3941760, p.homeLatE5());
    TEST_ASSERT_EQUAL(131072u, p.cacheSize());
    TEST_ASSERT_EQUAL(0u, p.cacheOffset() % 4096);
}

static void test_level_by_radius() {
    card(loadFixture("map_small.pak"));
    MapPack p;
    p.begin(&sd);
    TEST_ASSERT_EQUAL(0, p.levelFor(5)), TEST_ASSERT_EQUAL(0, p.levelFor(25));
    TEST_ASSERT_EQUAL(1, p.levelFor(26)), TEST_ASSERT_EQUAL(1, p.levelFor(60));
    TEST_ASSERT_EQUAL(2, p.levelFor(61)), TEST_ASSERT_EQUAL(2, p.levelFor(100));
}

static void test_covers() {
    card(loadFixture("map_small.pak"));
    MapPack p;
    p.begin(&sd);
    // The view spans the radius across half the width and 1.125 x it across half the height.
    TEST_ASSERT_TRUE(p.covers(39.4176, -77.3743, 25));
    TEST_ASSERT_TRUE(p.covers(39.4176, -77.3743, 35));
    TEST_ASSERT_FALSE(p.covers(39.4176, -77.3743, 36));   // 1.125 x 36 > 40
    TEST_ASSERT_FALSE(p.covers(40.2, -77.3743, 10));      // home moved 47 nm north
}

static void test_tile_at_home_has_features() {
    card(loadFixture("map_small.pak"));
    MapPack p;
    p.begin(&sd);
    const basemap::PackLevel& lv = p.level(0);
    int row, col;
    lv.cell(39.4176, -77.3743, row, col);
    basemap::TileInfo t;
    TEST_ASSERT_TRUE(p.tile(0, row, col, t));
    TEST_ASSERT_TRUE(t.featureCount > 0);  // Frederick: roads, KFDK, towns
    int towns = 0, airports = 0;
    for (int i = 0; i < t.featureCount; ++i) {
        basemap::FeatureEntry e;
        TEST_ASSERT_TRUE(p.feature(t, i, e));
        towns += e.kind == basemap::kTown, airports += e.kind == basemap::kAirport;
    }
    TEST_ASSERT_TRUE(towns > 0 && airports > 0);
}

static void test_refuses_a_bad_header() {
    std::string b = loadFixture("map_small.pak");
    b[10] ^= 1;  // inside the CRC'd header
    card(b);
    MapPack p;
    TEST_ASSERT_FALSE(p.begin(&sd));
    TEST_ASSERT_EQUAL_STRING("map.pak is damaged or from another version", p.error());
}

static void test_refuses_a_newer_version() {
    std::string b = loadFixture("map_small.pak");
    b[4] = 2;
    card(b);
    MapPack p;
    TEST_ASSERT_FALSE(p.begin(&sd));
}

static void test_refuses_a_bad_index() {
    card(loadFixture("map_small.pak"));
    MapPack probe;
    probe.begin(&sd);
    const uint32_t at = probe.level(1).indexOffset;
    std::string b = loadFixture("map_small.pak");
    b[at + 3] ^= 0x40;
    card(b);
    MapPack p;
    TEST_ASSERT_FALSE(p.begin(&sd));
}

static void test_bad_tile_is_skipped_and_counted() {
    card(loadFixture("map_small.pak"));
    MapPack probe;
    probe.begin(&sd);
    int row, col;
    probe.level(0).cell(39.4176, -77.3743, row, col);
    basemap::TileInfo t;
    probe.tile(0, row, col, t);
    std::string b = loadFixture("map_small.pak");
    b[t.offset + t.size - 1] ^= 0xFF;  // the tile's last byte
    card(b);
    MapPack p;
    TEST_ASSERT_TRUE(p.begin(&sd));
    TEST_ASSERT_FALSE(p.tile(0, row, col, t));
    TEST_ASSERT_EQUAL(1, p.damagedTiles());
    TEST_ASSERT_FALSE(p.tile(0, row, col, t));
    TEST_ASSERT_EQUAL(1, p.damagedTiles());  // counted once per tile
}

static void test_missing_pack_and_no_card() {
    MemStorage empty;
    MapPack p;
    TEST_ASSERT_FALSE(p.begin(&empty));
    TEST_ASSERT_EQUAL_STRING("no map.pak", p.error());
    empty.isMounted = false;
    TEST_ASSERT_FALSE(p.begin(&empty));
    TEST_ASSERT_EQUAL_STRING("", p.error());
    TEST_ASSERT_FALSE(p.begin(nullptr));
}

static void test_read_failure_turns_the_pack_off() {
    MemFile& f = card(loadFixture("map_small.pak"));
    MapPack p;
    TEST_ASSERT_TRUE(p.begin(&sd));
    f.fail = true;
    int row, col;
    p.level(0).cell(39.4176, -77.3743, row, col);
    basemap::TileInfo t;
    TEST_ASSERT_FALSE(p.tile(0, row, col, t));
    TEST_ASSERT_FALSE(p.isOpen());
    TEST_ASSERT_EQUAL_STRING("card error", p.error());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_opens_the_fixture);
    RUN_TEST(test_level_by_radius);
    RUN_TEST(test_covers);
    RUN_TEST(test_tile_at_home_has_features);
    RUN_TEST(test_refuses_a_bad_header);
    RUN_TEST(test_refuses_a_newer_version);
    RUN_TEST(test_refuses_a_bad_index);
    RUN_TEST(test_bad_tile_is_skipped_and_counted);
    RUN_TEST(test_missing_pack_and_no_card);
    RUN_TEST(test_read_failure_turns_the_pack_off);
    return UNITY_END();
}
```

- [ ] **Step 2: Run it to see it fail**

Run: `pio test -e test -f test_map_pack`
Expected: compile error, `map/MapPack.h` not found.

- [ ] **Step 3: Implement `map/MapPack.h`**

```cpp
#pragma once
// /contrail/map.pak, the Live Map's detailed map for one home (5b spec §3): header, three levels of square
// tiles, each tile a CRC'd feature table and its data, and a reserved area at the end for the saved chart.
// Opened once at boot (the header and each level's index are checked then, ~20 KB of reads); tiles are checked
// as they are read. Render thread only, except the status getters, which the web task may read.
#include <atomic>
#include <cstdint>

#include "hal.h"

namespace basemap {

enum FeatureKind : uint8_t {
    kRing = 0, kMajorRoad = 1, kRoad = 2, kRiver = 3, kRunway = 4, kTown = 16, kAirport = 17
};

struct PackLevel {
    uint16_t maxRadiusNm = 0, tileMilliDeg = 0;
    int32_t originLatE5 = 0, originLonE5 = 0;
    uint16_t rows = 0, cols = 0;
    uint32_t indexOffset = 0;
    double tileDeg() const { return tileMilliDeg * 1e-3; }
    // The tile holding (lat, lon); may be outside 0..rows-1 / 0..cols-1.
    void cell(double lat, double lon, int& row, int& col) const;
};

struct TileInfo {
    uint32_t offset = 0;  // into the file
    uint16_t size = 0;
    uint16_t featureCount = 0;
    bool allLand = false;
    int level = 0, row = 0, col = 0;
};

struct FeatureEntry {
    uint8_t kind = 0, rank = 0;
    uint32_t begin = 0, end = 0;  // the feature's bytes in the file
    int16_t ymin = 0, ymax = 0;   // tile units
};

class MapPack {
   public:
    static constexpr const char* kPath = "/contrail/map.pak";
    static constexpr int kMaxLevels = 3;

    // Opens and checks the pack. Starts over: a read failure from before is forgotten.
    bool begin(hal::IStorage* storage);
    bool isOpen() const { return open_ && !failed_; }
    // Why there is no detailed map: "" (open, or no card), "no map.pak",
    // "map.pak is damaged or from another version", "card error". A static literal.
    const char* error() const { return status_.load(); }
    int damagedTiles() const { return damaged_.load(); }

    int levelCount() const { return levels_; }
    const PackLevel& level(int i) const { return level_[i]; }
    int levelFor(float radiusNm) const;
    uint16_t coverNm() const { return coverNm_; }
    int32_t homeLatE5() const { return homeLatE5_; }
    int32_t homeLonE5() const { return homeLonE5_; }
    uint32_t packId() const { return packId_; }
    uint32_t cacheOffset() const { return cacheOffset_; }
    uint32_t cacheSize() const { return cacheSize_; }
    // True when the whole view (home ± radius across, ± 1.125 radius down) lies inside the coverage.
    bool covers(double lat, double lon, float radiusNm) const;

    // A tile's header (false: empty, out of range, damaged or unreadable; t.allLand is set whenever the index
    // says so, and t.offset != 0 when the tile has data). `verify` also checks the tile's CRC, a full read of
    // it: the painter does that once per rebuild. A failed read turns the pack off (readFailed).
    bool tile(int level, int row, int col, TileInfo& t, bool verify = true);
    bool feature(const TileInfo& t, int i, FeatureEntry& out);
    hal::IFile* file() { return file_; }
    // A read or write failed (card pulled): the detailed map is off until restart.
    void readFailed();

   private:
    bool fail(const char* why) { return status_ = why, open_ = false, false; }
    bool readAt(uint32_t off, void* buf, uint32_t n);
    hal::IFile* file_ = nullptr;
    bool open_ = false;
    std::atomic<bool> failed_{false};
    std::atomic<const char*> status_{""};
    std::atomic<int> damaged_{0};
    int levels_ = 0;
    PackLevel level_[kMaxLevels];
    uint16_t coverNm_ = 0;
    int32_t homeLatE5_ = 0, homeLonE5_ = 0;
    uint32_t packId_ = 0, cacheOffset_ = 0, cacheSize_ = 0;
    // Damaged tiles already counted: a small set of (level, row, col) keys, so a tile is counted once.
    static constexpr int kDamagedKeys = 16;
    uint32_t damagedKey_[kDamagedKeys] = {};
};

}  // namespace basemap
```

`map/MapPack.cpp`:

```cpp
#include "MapPack.h"

#include <cmath>
#include <cstring>

#include "assets/Pack.h"  // crc16

namespace basemap {
namespace {
uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
constexpr const char* kDamaged = "map.pak is damaged or from another version";
}  // namespace

void PackLevel::cell(double lat, double lon, int& row, int& col) const {
    row = int(std::floor((lat - originLatE5 * 1e-5) / tileDeg()));
    col = int(std::floor((lon - originLonE5 * 1e-5) / tileDeg()));
}

bool MapPack::readAt(uint32_t off, void* buf, uint32_t n) {
    if (!file_ || failed_) return false;
    if (file_->readAt(off, buf, n)) return true;
    readFailed();
    return false;
}

bool MapPack::begin(hal::IStorage* storage) {
    open_ = false, failed_ = false, status_ = "", damaged_ = 0, file_ = nullptr;
    std::memset(damagedKey_, 0, sizeof damagedKey_);
    if (!storage || !storage->mounted()) return false;
    file_ = storage->open(kPath);
    if (!file_) return fail("no map.pak");
    uint8_t h[64];
    if (!file_->readAt(0, h, sizeof h)) return fail("card error");
    if (std::memcmp(h, "CMAP", 4) != 0 || le16(h + 4) != 1 || le16(h + 6) != 64 ||
        assets::crc16(h, 62) != le16(h + 62))
        return fail(kDamaged);
    homeLatE5_ = int32_t(le32(h + 8)), homeLonE5_ = int32_t(le32(h + 12));
    coverNm_ = le16(h + 16);
    levels_ = h[18];
    packId_ = le32(h + 20), cacheOffset_ = le32(h + 24), cacheSize_ = le32(h + 28);
    if (levels_ < 1 || levels_ > kMaxLevels || cacheOffset_ % 4096 || cacheSize_ < 131072 ||
        uint64_t(cacheOffset_) + cacheSize_ > file_->size())
        return fail(kDamaged);
    for (int i = 0; i < levels_; ++i) {
        uint8_t e[24];
        if (!file_->readAt(64 + 24 * i, e, sizeof e)) return fail("card error");
        PackLevel& lv = level_[i];
        lv.maxRadiusNm = le16(e), lv.tileMilliDeg = le16(e + 2);
        lv.originLatE5 = int32_t(le32(e + 4)), lv.originLonE5 = int32_t(le32(e + 8));
        lv.rows = le16(e + 12), lv.cols = le16(e + 14);
        lv.indexOffset = le32(e + 16);
        const uint32_t bytes = uint32_t(lv.rows) * lv.cols * 8;
        if (!lv.tileMilliDeg || !lv.rows || !lv.cols || uint64_t(lv.indexOffset) + bytes > cacheOffset_)
            return fail(kDamaged);
        // The index CRC, 64 bytes at a time.
        uint16_t crc = 0xFFFF;
        uint8_t buf[64];
        for (uint32_t at = 0; at < bytes; at += sizeof buf) {
            const uint32_t n = bytes - at < sizeof buf ? bytes - at : uint32_t(sizeof buf);
            if (!file_->readAt(lv.indexOffset + at, buf, n)) return fail("card error");
            crc = assets::crc16(buf, n, crc);
        }
        if (crc != le16(e + 20)) return fail(kDamaged);
    }
    open_ = true;
    return true;
}

int MapPack::levelFor(float radiusNm) const {
    for (int i = 0; i < levels_; ++i)
        if (radiusNm <= level_[i].maxRadiusNm) return i;
    return levels_ - 1;
}

bool MapPack::covers(double lat, double lon, float radiusNm) const {
    if (!isOpen()) return false;
    const double hl = homeLatE5_ * 1e-5, hn = homeLonE5_ * 1e-5;
    const double dLatNm = std::fabs(lat - hl) * 60 + 1.125 * radiusNm;
    const double dLonNm = std::fabs(lon - hn) * 60 * std::cos(hl * M_PI / 180) + radiusNm;
    return dLatNm <= coverNm_ && dLonNm <= coverNm_;
}

bool MapPack::tile(int level, int row, int col, TileInfo& t, bool verify) {
    t = TileInfo{};
    t.level = level, t.row = row, t.col = col;
    if (!isOpen() || level < 0 || level >= levels_) return false;
    const PackLevel& lv = level_[level];
    if (row < 0 || col < 0 || row >= lv.rows || col >= lv.cols) return false;
    uint8_t e[8];
    if (!readAt(lv.indexOffset + (uint32_t(row) * lv.cols + col) * 8, e, 8)) return false;
    t.offset = le32(e), t.size = le16(e + 4), t.allLand = e[6] & 1;
    if (!t.offset) return false;
    if (t.size < 4 || uint64_t(t.offset) + t.size > cacheOffset_) return false;
    // The tile's CRC over everything after it, 64 bytes at a time (a tile is at most 64 KB).
    uint8_t buf[64];
    if (!readAt(t.offset, buf, 4)) return false;
    const uint16_t want = le16(buf);
    t.featureCount = le16(buf + 2);
    if (!verify) return 4u + 8u * t.featureCount <= t.size;
    uint16_t crc = 0xFFFF;
    for (uint32_t at = 2; at < t.size; at += sizeof buf) {
        const uint32_t n = t.size - at < sizeof buf ? t.size - at : uint32_t(sizeof buf);
        if (!readAt(t.offset + at, buf, n)) return false;
        crc = assets::crc16(buf, n, crc);
    }
    if (crc != want || 4u + 8u * t.featureCount > t.size) {
        const uint32_t key = uint32_t(level + 1) << 28 | uint32_t(row) << 14 | uint32_t(col);
        bool seen = false;
        for (uint32_t& k : damagedKey_)
            if (k == key || !k) {
                seen = k == key;
                if (!k) k = key;
                break;
            }
        if (!seen) ++damaged_;
        return false;
    }
    return true;
}

bool MapPack::feature(const TileInfo& t, int i, FeatureEntry& out) {
    if (i < 0 || i >= t.featureCount) return false;
    uint8_t e[8];
    if (!readAt(t.offset + 4 + 8 * uint32_t(i), e, 8)) return false;
    out.kind = e[0], out.rank = e[1];
    out.begin = t.offset + le16(e + 2);
    out.ymin = int16_t(le16(e + 4)), out.ymax = int16_t(le16(e + 6));
    uint16_t next = t.size;
    if (i + 1 < t.featureCount) {
        uint8_t n[8];
        if (!readAt(t.offset + 4 + 8 * uint32_t(i + 1), n, 8)) return false;
        next = le16(n + 2);
    }
    out.end = t.offset + next;
    return out.begin < out.end && out.end <= t.offset + t.size;
}

void MapPack::readFailed() {
    failed_ = true;
    status_ = "card error";
}

}  // namespace basemap
```

- [ ] **Step 4: Run the test**

Run: `pio test -e test -f test_map_pack`
Expected: PASS (10 tests). If `test_covers`'s edge cases disagree by a nautical mile because of the fixture's rounded home, adjust the test's radii (36 → 37), not the formula.

- [ ] **Step 5: Commit**

```bash
git add firmware/lib/core/map/MapPack.h firmware/lib/core/map/MapPack.cpp firmware/test/test_map_pack
git commit -m "Map pack reader: header, levels, index and tile checks"
```

---

### Task 7: The saved chart (`map/ChartCache`)

**Files:**
- Create: `firmware/lib/core/map/ChartCache.h`, `firmware/lib/core/map/ChartCache.cpp`
- Test: `firmware/test/test_chart_cache/test_main.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// ChartCache: the key's write order and each field, rows in and out.
#include <unity.h>

#include <vector>

#include "TestStorage.h"
#include "map/ChartCache.h"

void setUp() {}
void tearDown() {}

using basemap::ChartCache;
using basemap::ChartKey;

static MemFile pack() {
    MemFile f;
    f.bytes.assign(4096 + 131072, 0);
    return f;
}

static ChartKey key() {
    ChartKey k;
    k.latE5 = 3941760, k.lonE5 = -7737430, k.radiusNm10 = 250, k.theme = 0x1234, k.level = 0, k.packId = 0xABCD;
    return k;
}

static void test_empty_area_is_not_valid() {
    MemFile f = pack();
    ChartCache c(f, 4096, 131072);
    TEST_ASSERT_FALSE(c.matches(key()));
}

static void test_rows_round_trip_and_key_is_written_last() {
    MemFile f = pack();
    ChartCache c(f, 4096, 131072);
    TEST_ASSERT_TRUE(c.begin(key()));
    TEST_ASSERT_FALSE(c.matches(key()));  // begun, not finished
    std::vector<uint16_t> row(240 * 16);
    for (int i = 0; i < 240 * 16; ++i) row[i] = uint16_t(i * 13);
    TEST_ASSERT_TRUE(c.writeRows(32, 16, row.data()));
    TEST_ASSERT_TRUE(c.finish(key()));
    TEST_ASSERT_TRUE(c.matches(key()));
    std::vector<uint16_t> back(30);
    TEST_ASSERT_TRUE(c.readSpan(40, 10, 30, back.data()));  // row 40, x 10..39
    for (int i = 0; i < 30; ++i) TEST_ASSERT_EQUAL(row[(40 - 32) * 240 + 10 + i], back[i]);
}

static void test_restart_mid_rebuild_reads_as_invalid() {
    MemFile f = pack();
    {
        ChartCache c(f, 4096, 131072);
        c.begin(key());
        c.finish(key());
        c.begin(key());  // a second rebuild starts and the board restarts
    }
    ChartCache again(f, 4096, 131072);
    TEST_ASSERT_FALSE(again.matches(key()));
}

static void test_each_key_field_matters() {
    MemFile f = pack();
    ChartCache c(f, 4096, 131072);
    c.begin(key());
    c.finish(key());
    ChartKey k = key();
    k.latE5 += 1;
    TEST_ASSERT_FALSE(c.matches(k));
    k = key(), k.lonE5 += 1;
    TEST_ASSERT_FALSE(c.matches(k));
    k = key(), k.radiusNm10 += 1;
    TEST_ASSERT_FALSE(c.matches(k));
    k = key(), k.theme += 1;
    TEST_ASSERT_FALSE(c.matches(k));
    k = key(), k.level = 1;
    TEST_ASSERT_FALSE(c.matches(k));
    k = key(), k.packId += 1;
    TEST_ASSERT_FALSE(c.matches(k));
    TEST_ASSERT_TRUE(c.matches(key()));
}

static void test_extra_round_trip_and_bounds() {
    MemFile f = pack();
    ChartCache c(f, 4096, 131072);
    const char msg[] = "marks";
    TEST_ASSERT_TRUE(c.writeExtra(10, msg, sizeof msg));
    char back[sizeof msg];
    TEST_ASSERT_TRUE(c.readExtra(10, back, sizeof back));
    TEST_ASSERT_EQUAL_STRING(msg, back);
    TEST_ASSERT_FALSE(c.writeExtra(ChartCache::kExtraBytes - 2, msg, sizeof msg));
    // Extra bytes do not disturb the key or the first row.
    c.begin(key());
    c.finish(key());
    TEST_ASSERT_TRUE(c.writeExtra(0, msg, sizeof msg));
    TEST_ASSERT_TRUE(c.matches(key()));
}

static void test_write_failure_is_reported() {
    MemFile f = pack();
    ChartCache c(f, 4096, 131072);
    f.writeFail = true;
    TEST_ASSERT_FALSE(c.begin(key()));
}

static void test_rows_outside_the_chart_are_refused() {
    MemFile f = pack();
    ChartCache c(f, 4096, 131072);
    std::vector<uint16_t> row(240 * 16);
    TEST_ASSERT_FALSE(c.writeRows(260, 16, row.data()));  // 260 + 16 > 270
    uint16_t px;
    TEST_ASSERT_FALSE(c.readSpan(270, 0, 1, &px));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_empty_area_is_not_valid);
    RUN_TEST(test_rows_round_trip_and_key_is_written_last);
    RUN_TEST(test_restart_mid_rebuild_reads_as_invalid);
    RUN_TEST(test_each_key_field_matters);
    RUN_TEST(test_extra_round_trip_and_bounds);
    RUN_TEST(test_write_failure_is_reported);
    RUN_TEST(test_rows_outside_the_chart_are_refused);
    return UNITY_END();
}
```

- [ ] **Step 2: Run it to see it fail**

Run: `pio test -e test -f test_chart_cache`
Expected: compile error.

- [ ] **Step 3: Implement**

`map/ChartCache.h`:

```cpp
#pragma once
// The finished detailed chart, saved in map.pak's reserved area (5b spec §4): 270 rows of 240 RGB565 pixels from the
// area's start, then 32 B of key, then room for the label marks. The area is 4 KB aligned and a row is 480 B, so a
// 16-row strip starting on a multiple of 16 rows is exactly 15 sectors, written with no read-modify-write. Normal
// frames read the chart back from here instead of repainting it. The key is cleared when a rebuild begins and
// written when it finishes, so a rebuild cut short never reads as valid.
#include <cstdint>

#include "hal.h"

namespace basemap {

struct ChartKey {
    int32_t latE5 = 0, lonE5 = 0;
    uint16_t radiusNm10 = 0;  // tenths of a nautical mile
    uint16_t theme = 0;       // a hash of the theme's map colours
    uint8_t level = 0;
    uint32_t packId = 0;
};

class ChartCache {
   public:
    static constexpr int kW = 240, kH = 270;
    static constexpr uint32_t kRowBytes = uint32_t(kW) * kH * 2, kKeyBytes = 32, kExtraBytes = 1440;  // 129,600 + 32 + 1,440
    static constexpr uint32_t kBytes = kRowBytes + kKeyBytes + kExtraBytes;                           // 131,072
    ChartCache(hal::IFile& f, uint32_t offset, uint32_t size) : f_(f), off_(offset), size_(size) {}
    bool matches(const ChartKey& k);
    bool begin(const ChartKey& k);   // clears the key
    bool writeRows(int y, int rows, const uint16_t* px);  // full-width rows y .. y + rows - 1
    bool finish(const ChartKey& k);  // writes the key
    bool readSpan(int y, int x, int w, uint16_t* out);    // one row's pixels x .. x + w - 1
    bool readRows(int y, int rows, uint16_t* out);        // full-width rows
    // The caller's bytes kept beside the chart (the label marks): offset within kExtraBytes.
    bool writeExtra(uint32_t at, const void* p, uint32_t n);
    bool readExtra(uint32_t at, void* p, uint32_t n);

   private:
    static void encode(const ChartKey& k, uint8_t out[32]);
    uint32_t rowOffset(int y) const { return off_ + uint32_t(y) * kW * 2; }
    uint32_t keyOffset() const { return off_ + kRowBytes; }
    uint32_t extraOffset() const { return keyOffset() + kKeyBytes; }
    hal::IFile& f_;
    uint32_t off_, size_;
};

}  // namespace basemap
```

`map/ChartCache.cpp`:

```cpp
#include "ChartCache.h"

#include <cstring>

#include "assets/Pack.h"  // crc16

namespace basemap {
namespace {
constexpr uint8_t kMagic[4] = {'C', 'H', 'R', '1'};
}

void ChartCache::encode(const ChartKey& k, uint8_t out[32]) {
    std::memset(out, 0, 32);
    std::memcpy(out, kMagic, 4);
    auto put32 = [&](int at, uint32_t v) { for (int i = 0; i < 4; ++i) out[at + i] = uint8_t(v >> 8 * i); };
    put32(4, uint32_t(k.latE5)), put32(8, uint32_t(k.lonE5));
    out[12] = uint8_t(k.radiusNm10), out[13] = uint8_t(k.radiusNm10 >> 8);
    out[14] = uint8_t(k.theme), out[15] = uint8_t(k.theme >> 8);
    out[16] = k.level;
    put32(20, k.packId);
    const uint16_t crc = assets::crc16(out, 30);
    out[30] = uint8_t(crc), out[31] = uint8_t(crc >> 8);
}

bool ChartCache::matches(const ChartKey& k) {
    if (size_ < kBytes) return false;
    uint8_t want[32], got[32];
    encode(k, want);
    return f_.readAt(keyOffset(), got, sizeof got) && std::memcmp(want, got, sizeof got) == 0;
}

bool ChartCache::begin(const ChartKey&) {
    const uint8_t zero[32] = {};
    return f_.writeAt(keyOffset(), zero, sizeof zero);
}

bool ChartCache::finish(const ChartKey& k) {
    uint8_t b[32];
    encode(k, b);
    return f_.writeAt(keyOffset(), b, sizeof b);
}

bool ChartCache::writeRows(int y, int rows, const uint16_t* px) {
    if (y < 0 || rows < 1 || y + rows > kH) return false;
    return f_.writeAt(rowOffset(y), px, uint32_t(rows) * kW * 2);
}

bool ChartCache::readSpan(int y, int x, int w, uint16_t* out) {
    if (y < 0 || y >= kH || x < 0 || w < 1 || x + w > kW) return false;
    return f_.readAt(rowOffset(y) + uint32_t(x) * 2, out, uint32_t(w) * 2);
}

bool ChartCache::readRows(int y, int rows, uint16_t* out) {
    if (y < 0 || rows < 1 || y + rows > kH) return false;
    return f_.readAt(rowOffset(y), out, uint32_t(rows) * kW * 2);
}

bool ChartCache::writeExtra(uint32_t at, const void* p, uint32_t n) {
    return at + n <= kExtraBytes && f_.writeAt(extraOffset() + at, p, n);
}

bool ChartCache::readExtra(uint32_t at, void* p, uint32_t n) {
    return at + n <= kExtraBytes && f_.readAt(extraOffset() + at, p, n);
}

}  // namespace basemap
```

Pixels are stored in the strip buffer's own byte order (as `uint16_t` in memory), so the device and the native tests each read back exactly what they wrote; the file is never shared between them.

- [ ] **Step 4: Run the test**

Run: `pio test -e test -f test_chart_cache`
Expected: PASS (7 tests).

- [ ] **Step 5: Commit**

```bash
git add firmware/lib/core/map/ChartCache.h firmware/lib/core/map/ChartCache.cpp firmware/test/test_chart_cache
git commit -m "Map: the saved chart in map.pak's reserved area"
```

---

### Task 8: Theme colours

**Files:**
- Modify: `firmware/lib/core/screens/Theme.h`, `firmware/lib/core/screens/Theme.cpp`

- [ ] **Step 1: Add the fields**

In `Theme.h`, after `gfx::Rgb mapCard, mapCardEdge;`:

```cpp
    // Live Map from a map pack (5b): major and minor roads, rivers, town dots and names.
    gfx::Rgb mapRoad, mapRoadMinor, mapRiver, mapTown;
```

In `Theme.cpp`, after each theme's `// map strip` line, add (Cinematic):

```cpp
        {58, 74, 112}, {42, 56, 90}, {44, 72, 128}, {150, 162, 200},  // map pack: roads, minor roads, rivers, towns
```

and (Editorial):

```cpp
        {84, 88, 96}, {64, 68, 74}, {52, 66, 84}, {170, 170, 176},    // map pack: roads, minor roads, rivers, towns
```

- [ ] **Step 2: Build and run the whole suite**

Run: `pio test -e test`
Expected: PASS, every golden unchanged (the new fields are not drawn yet).

- [ ] **Step 3: Commit**

```bash
git add firmware/lib/core/screens/Theme.h firmware/lib/core/screens/Theme.cpp
git commit -m "Theme: map pack colours"
```

---

### Task 9: The strip painter and label marks (`map/DetailPainter`)

**Files:**
- Create: `firmware/lib/core/map/DetailPainter.h`, `firmware/lib/core/map/DetailPainter.cpp`
- Test: `firmware/test/test_detail_painter/test_main.cpp`

What it does, per chart (spec §4, refinement 2):
- `layout(...)`: once per rebuild, before any painting. Verifies the view's tiles' CRCs once (bad ones are remembered and skipped), then reads every point feature of the view's tiles, places names (large airports, medium, towns by rank, small fields; caps per level: towns 10 / 7 / 5; at level 2 medium airports up to 10 named), and fills `DetailMark`s.
- `paint(Canvas&, y0, rows)`: one half-strip (8 rows) at a time; the caller (MapBase) calls it twice per 16-row strip with the canvas clipped to the half. Order: water fill; land coverage (2×2 XOR sub-scanlines into the 960-byte scratch; all-land tiles fill directly); coastline (non-cut ring edges, `aaLine` 1 px); rivers (`aaLine` 1.2 px); minor roads 1.0 px; major roads 1.5 px; runways 1.5 px. MapBase then draws rings, marks, names, home, scale bar and header on top, as today.

- [ ] **Step 1: Write the failing test**

```cpp
// DetailPainter over the fixture pack: land and water where the data says, lines drawn, labels placed without
// overlaps, nothing outside the half-strip touched.
#include <unity.h>

#include <cstring>
#include <vector>

#include "Fixture.h"
#include "TestStorage.h"
#include "gfx/Canvas.h"
#include "map/DetailPainter.h"
#include "map/LandMask.h"  // viewFor
#include "screens/Theme.h"

void setUp() {}
void tearDown() {}

using namespace basemap;

static MemStorage sd;
static MapPack pack;

static void openPack() {
    sd.add(MapPack::kPath, loadFixture("map_small.pak"));
    TEST_ASSERT_TRUE(pack.begin(&sd));
}

struct Chart {
    std::vector<uint16_t> px = std::vector<uint16_t>(240 * 270);
    uint8_t scratch[DetailPainter::kScratchBytes];
    DetailMark marks[DetailPainter::kMaxMarks];
};

// Paints the whole chart in half-strips, as MapBase does.
static void paintAll(DetailPainter& p, Chart& ch) {
    for (int y = 0; y < 270; y += DetailPainter::kRows) {
        const int rows = y + DetailPainter::kRows > 270 ? 270 - y : DetailPainter::kRows;
        gfx::Canvas c(ch.px.data() + y * 240, 240, y, rows);
        p.paint(c, y, rows);
    }
}

static void test_paints_land_roads_and_water() {
    openPack();
    Chart ch;
    DetailPainter p(scr::cinematicSky());
    p.setPack(&pack);
    const MapView v = viewFor(39.4176, -77.3743, 25);
    p.begin(v, 25, ch.scratch, ch.marks);
    int marks = p.layout(240, nullptr, 0);
    TEST_ASSERT_TRUE(marks > 3);
    paintAll(p, ch);
    const uint16_t land = gfx::rgb565(scr::cinematicSky().mapLand);
    int landPx = 0, other = 0;
    for (uint16_t c : ch.px) landPx += c == land, other += c != land;
    TEST_ASSERT_TRUE(landPx > 240 * 270 / 2);  // inland Maryland: mostly land
    TEST_ASSERT_TRUE(other > 2000);             // roads, rivers, coast at the edges
    TEST_ASSERT_EQUAL(0, pack.damagedTiles());
}

static void test_labels_do_not_overlap_or_leave_the_chart() {
    openPack();
    Chart ch;
    DetailPainter p(scr::cinematicSky());
    p.setPack(&pack);
    p.begin(viewFor(39.4176, -77.3743, 25), 25, ch.scratch, ch.marks);
    const gfx::Rect keep[] = {{110, 126, 20, 20}};
    const int n = p.layout(240, keep, 1);
    for (int i = 0; i < n; ++i) {
        const DetailMark& a = ch.marks[i];
        if (!a.labelled) continue;
        const gfx::Rect ra = a.box();
        TEST_ASSERT_TRUE(ra.x >= 0 && ra.right() <= 240 && ra.y >= 0 && ra.bottom() <= 270);
        TEST_ASSERT_FALSE(ra.intersects(keep[0]));
        for (int j = 0; j < i; ++j)
            if (ch.marks[j].labelled) TEST_ASSERT_FALSE(ra.intersects(ch.marks[j].box()));
    }
}

static void test_town_caps_by_level() {
    openPack();
    for (float r : {10.f, 30.f}) {
        Chart ch;
        DetailPainter p(scr::cinematicSky());
        p.setPack(&pack);
        p.begin(viewFor(39.4176, -77.3743, r), r, ch.scratch, ch.marks);
        const int n = p.layout(240, nullptr, 0);
        int towns = 0;
        for (int i = 0; i < n; ++i) towns += ch.marks[i].kind == kTown && ch.marks[i].labelled;
        TEST_ASSERT_TRUE(towns <= (r <= 25 ? 10 : 7));
    }
}

static void test_half_strip_paints_only_its_rows() {
    openPack();
    Chart ch;
    const uint16_t sentinel = 0xF81F;
    for (auto& c : ch.px) c = sentinel;
    DetailPainter p(scr::cinematicSky());
    p.setPack(&pack);
    p.begin(viewFor(39.4176, -77.3743, 25), 25, ch.scratch, ch.marks);
    p.layout(240, nullptr, 0);
    gfx::Canvas c(ch.px.data() + 64 * 240, 240, 64, 8);
    p.paint(c, 64, 8);
    for (int y = 0; y < 270; ++y)
        for (int x = 0; x < 240; ++x) {
            const bool inside = y >= 64 && y < 72;
            if (inside) TEST_ASSERT_NOT_EQUAL(sentinel, ch.px[y * 240 + x]);
            else TEST_ASSERT_EQUAL(sentinel, ch.px[y * 240 + x]);
        }
}

static void test_card_pulled_mid_paint_stops_cleanly() {
    openPack();
    Chart ch;
    DetailPainter p(scr::cinematicSky());
    p.setPack(&pack);
    p.begin(viewFor(39.4176, -77.3743, 25), 25, ch.scratch, ch.marks);
    p.layout(240, nullptr, 0);
    static_cast<MemFile*>(pack.file())->fail = true;
    gfx::Canvas c(ch.px.data(), 240, 0, 8);
    p.paint(c, 0, 8);
    TEST_ASSERT_TRUE(p.failed());
    TEST_ASSERT_FALSE(pack.isOpen());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_paints_land_roads_and_water);
    RUN_TEST(test_labels_do_not_overlap_or_leave_the_chart);
    RUN_TEST(test_town_caps_by_level);
    RUN_TEST(test_half_strip_paints_only_its_rows);
    RUN_TEST(test_card_pulled_mid_paint_stops_cleanly);
    return UNITY_END();
}
```

Before writing it, check `gfx::Canvas`'s constructor arguments (`Canvas(buf, width, bandY, bandH)`) and that `gfx::Rect` has `right()`, `bottom()` and `intersects()` (it does in `MapScreen.cpp`).

- [ ] **Step 2: Run it to see it fail**

Run: `pio test -e test -f test_detail_painter`
Expected: compile error.

- [ ] **Step 3: Implement `map/DetailPainter.h`**

```cpp
#pragma once
// Paints the detailed chart from a map pack (5b spec §4), 8 rows at a time, into the strip buffer: water,
// land (an even-odd XOR fill of the rings at 2 x 2 sub-pixels, so lakes cut holes and islands fill again),
// coastline (ring edges that are not tile cuts), rivers, roads and runways. Names are placed once per rebuild
// by layout() into DetailMarks, which the map screen draws and plane labels keep clear of.
//
// Memory: kScratchBytes of caller scratch (the screen arena) for the sub-pixel rows, and the marks. Tiles are
// read through ByteStream; nothing is allocated.
#include <cstdint>

#include "Features.h"
#include "LandMask.h"  // MapView
#include "MapPack.h"
#include "gfx/Canvas.h"
#include "screens/Theme.h"

namespace basemap {

struct DetailMark {
    int16_t x = 0, y = 0;                    // the dot
    int16_t lx = 0, ly = 0;                  // label anchor (left of the baseline)
    int16_t bx = 0, by = 0, bw = 0, bh = 0;  // label box
    uint8_t kind = 0, rank = 0;              // kTown or kAirport; the feature's rank
    bool labelled = false;
    char name[21] = "";
    gfx::Rect box() const { return {bx, by, bw, bh}; }
};

class DetailPainter {
   public:
    static constexpr int kRows = 8;                       // a half-strip
    static constexpr int kSubW = 480, kSubRows = 2 * kRows;  // 2 x 2 sub-pixels
    static constexpr int kScratchBytes = kSubW / 8 * kSubRows;  // 960
    static constexpr int kMaxMarks = 32;
    static constexpr int kMaxTiles = 64;  // tiles in view; a larger view skips the rest (and says so)

    explicit DetailPainter(const scr::Theme& t) : t_(t) {}
    void setPack(MapPack* p) { pack_ = p; }
    // A new chart for `v` at `radiusNm`; scratch is kScratchBytes, marks kMaxMarks.
    void begin(const MapView& v, float radiusNm, uint8_t* scratch, DetailMark* marks);
    // Checks the view's tiles and reads their towns and airports, placing the names; returns the marks filled.
    // Call it before paint(). `w` is the chart
    // width; names keep clear of the nKeep rects in keep.
    int layout(int w, const gfx::Rect* keep, int nKeep);
    // Paints rows [y0, y0 + rows) (rows ≤ kRows) of the chart into c, which covers at least those rows.
    void paint(gfx::Canvas& c, int y0, int rows);
    // A read failed while painting or laying out: the caller drops to the built-in map.
    bool failed() const { return failed_; }
    int level() const { return level_; }

   private:
    struct Tf {  // tile units to screen pixels for one tile
        float sx, ox, sy, oy;
        float x(int32_t u) const { return ox + sx * float(u); }
        float y(int32_t u) const { return oy + sy * float(u); }
    };
    Tf tf(int row, int col) const;
    void tileRange(int& r0, int& r1, int& c0, int& c1, float yTop, float yBottom) const;
    // The tile's header without its CRC; false when empty, unreadable or found bad by layout().
    bool tileFor(int row, int col, TileInfo& t);
    void verifyView();
    bool isBad(int row, int col) const;
    void fillLand(gfx::Canvas& c, int y0, int rows);
    void edge(float x0, float y0, float x1, float y1, int sy0);
    void strokeLines(gfx::Canvas& c, int y0, int rows);
    void lineFeature(gfx::Canvas& c, const TileInfo& t, const FeatureEntry& e, const Tf& m, gfx::Rgb col, float w);
    void coast(gfx::Canvas& c, const TileInfo& t, const FeatureEntry& e, const Tf& m);

    MapPack* pack_ = nullptr;
    const scr::Theme& t_;
    uint64_t bad_ = 0;  // tiles layout() found damaged, one bit per tile of the view (at most kMaxTiles)
    int vr0_ = 0, vc0_ = 0, vcols_ = 1, vrows_ = 0;
    MapView v_;
    float radius_ = 25;
    int level_ = 0;
    uint8_t* sub_ = nullptr;
    DetailMark* marks_ = nullptr;
    int nMarks_ = 0;
    bool failed_ = false;
};

}  // namespace basemap
```

`map/DetailPainter.cpp`:

```cpp
#include "DetailPainter.h"

#ifdef CONTRAIL_ESP32
#pragma GCC optimize("O2")
#endif

#include <cmath>
#include <cstring>

#include "gfx/Font.h"
#include "gfx/Raster.h"
#include "gfx/fonts/Fonts.h"
#include "ui/Widgets.h"  // ui::textBox

namespace basemap {
using namespace gfx;

namespace {
constexpr int kTownCap[3] = {10, 7, 5};
constexpr int kMediumCapLevel2 = 10;
// Feature y ranges are tile units; a feature crosses rows [yTop, yBottom) of the screen when its range does.
bool crosses(const FeatureEntry& e, float syMin, float syMax, float sy, float oy) {
    // sy < 0 (north up): unit y maps to screen oy + sy * y.
    const float top = oy + sy * float(e.ymax), bottom = oy + sy * float(e.ymin);
    return bottom >= syMin && top <= syMax;
}
}  // namespace

void DetailPainter::begin(const MapView& v, float radiusNm, uint8_t* scratch, DetailMark* marks) {
    v_ = v, radius_ = radiusNm, sub_ = scratch, marks_ = marks, nMarks_ = 0, failed_ = false, bad_ = 0;
    level_ = pack_->levelFor(radiusNm);
}

bool DetailPainter::isBad(int row, int col) const {
    const int r = row - vr0_, c = col - vc0_, i = r * vcols_ + c;
    return r >= 0 && c >= 0 && c < vcols_ && i < kMaxTiles && (bad_ >> i & 1);
}

void DetailPainter::verifyView() {
    int r1, c1;
    tileRange(vr0_, r1, vc0_, c1, 0, float(v_.h));
    vcols_ = c1 - vc0_ + 1, vrows_ = r1 - vr0_ + 1;
    for (int r = vr0_; r <= r1; ++r)
        for (int c = vc0_; c <= c1; ++c) {
            TileInfo t;
            if (pack_->tile(level_, r, c, t, true) || !t.offset) continue;
            if (!pack_->isOpen()) {
                failed_ = true;
                return;
            }
            const int i = (r - vr0_) * vcols_ + (c - vc0_);
            if (i < kMaxTiles) bad_ |= uint64_t(1) << i;
        }
}

bool DetailPainter::tileFor(int row, int col, TileInfo& t) {
    const bool ok = pack_->tile(level_, row, col, t, false);
    if (!pack_->isOpen()) failed_ = true;
    return ok && !isBad(row, col);
}

DetailPainter::Tf DetailPainter::tf(int row, int col) const {
    const PackLevel& lv = pack_->level(level_);
    const double deg = lv.tileDeg();
    const double lat0 = lv.originLatE5 * 1e-5 + row * deg, lon0 = lv.originLonE5 * 1e-5 + col * deg;
    const double ppnLat = v_.pxPerNm * 60, ppnLon = v_.pxPerNm * 60 * v_.cosLat;
    Tf m;
    m.sx = float(deg / 2048 * ppnLon);
    m.ox = float(v_.cx + (lon0 - v_.lon) * ppnLon);
    m.sy = float(-deg / 2048 * ppnLat);
    m.oy = float(v_.cy - (lat0 - v_.lat) * ppnLat);
    return m;
}

void DetailPainter::tileRange(int& r0, int& r1, int& c0, int& c1, float yTop, float yBottom) const {
    const PackLevel& lv = pack_->level(level_);
    const double ppnLat = v_.pxPerNm * 60, ppnLon = v_.pxPerNm * 60 * v_.cosLat;
    const double latTop = v_.lat + (v_.cy - yTop) / ppnLat, latBottom = v_.lat + (v_.cy - yBottom) / ppnLat;
    const double lonL = v_.lon - v_.cx / ppnLon, lonR = v_.lon + (v_.w - v_.cx) / ppnLon;
    lv.cell(latBottom, lonL, r0, c0);
    lv.cell(latTop, lonR, r1, c1);
}

// ------------------------------------------------------------------ land

// One ring edge in screen pixels into the sub-pixel rows of this half-strip (sub-row k is screen row
// y0 + k / 2, centre at y0 + (k + 0.5) / 2): toggle the bit where the edge crosses each sub-row's centre.
void DetailPainter::edge(float x0, float y0, float x1, float y1, int sy0) {
    if (y0 == y1) return;
    const float top = std::fmin(y0, y1), bottom = std::fmax(y0, y1);
    int k0 = int(std::ceil((top - sy0) * 2 - 0.5f)), k1 = int(std::ceil((bottom - sy0) * 2 - 0.5f));
    if (k0 < 0) k0 = 0;
    if (k1 > kSubRows) k1 = kSubRows;
    const float dxdy = (x1 - x0) / (y1 - y0);
    for (int k = k0; k < k1; ++k) {
        const float yc = sy0 + (k + 0.5f) / 2;
        const float xc = (x0 + (yc - y0) * dxdy) * 2;  // sub-pixel x
        int bit = int(std::ceil(xc - 0.5f));
        if (bit >= kSubW) continue;
        if (bit < 0) bit = 0;
        sub_[k * (kSubW / 8) + (bit >> 3)] ^= uint8_t(0x80 >> (bit & 7));
    }
}

void DetailPainter::fillLand(Canvas& c, int y0, int rows) {
    std::memset(sub_, 0, kScratchBytes);
    int r0, r1, c0, c1;
    tileRange(r0, r1, c0, c1, float(y0), float(y0 + rows));
    const uint16_t landPx = rgb565(t_.mapLand);
    for (int r = r0; r <= r1 && !failed_; ++r)
        for (int col = c0; col <= c1 && !failed_; ++col) {
            TileInfo t;
            const bool have = tileFor(r, col, t);
            const Tf m = tf(r, col);
            if (t.allLand) {  // a whole tile of land, with or without roads on it: its rectangle's two sides
                edge(m.x(0), m.y(0), m.x(0), m.y(2048), y0);
                edge(m.x(2048), m.y(0), m.x(2048), m.y(2048), y0);
            }
            if (!have) continue;
            for (int i = 0; i < t.featureCount; ++i) {
                FeatureEntry e;
                if (!pack_->feature(t, i, e)) break;
                if (e.kind != kRing) break;  // rings come first
                if (!crosses(e, float(y0), float(y0 + rows), m.sy, m.oy)) continue;
                ByteStream s(*pack_->file(), e.begin, e.end);
                StreamRingReader rr(s);
                RingPoint p, prev;
                bool first = true;
                while (rr.next(p)) {
                    if (!first) edge(m.x(prev.x), m.y(prev.y), m.x(p.x), m.y(p.y), y0);
                    prev = p, first = false;
                }
                if (!s.ok() && !first) failed_ = true;
            }
        }
    // Running parity along each sub-row, then 2 x 2 sub-pixels to a coverage of 0..4 per pixel.
    for (int k = 0; k < 2 * rows; ++k) {
        uint8_t* row = sub_ + k * (kSubW / 8);
        uint8_t carry = 0;
        for (int b = 0; b < kSubW / 8; ++b) {
            uint8_t v = row[b];
            v ^= v >> 1, v ^= v >> 2, v ^= v >> 4;  // prefix parity within the byte
            if (carry) v = uint8_t(~v);
            row[b] = v;
            carry = v & 1;
        }
    }
    const Rect cl = c.clip();
    for (int y = y0; y < y0 + rows; ++y) {
        if (y < cl.y || y >= cl.bottom()) continue;
        const uint8_t* a = sub_ + (2 * (y - y0)) * (kSubW / 8);
        const uint8_t* b = a + kSubW / 8;
        for (int x = cl.x; x < cl.right(); ++x) {
            const int bit = 2 * x;
            auto at = [](const uint8_t* r, int i) { return r[i >> 3] >> (7 - (i & 7)) & 1; };
            const int cov = at(a, bit) + at(a, bit + 1) + at(b, bit) + at(b, bit + 1);
            if (cov == 4) c.at(x, y) = landPx;
            else if (cov) c.at(x, y) = blend565(c.at(x, y), t_.mapLand, uint8_t(cov * 64));
        }
    }
}

// ------------------------------------------------------------------ lines

void DetailPainter::coast(Canvas& c, const TileInfo&, const FeatureEntry& e, const Tf& m) {
    ByteStream s(*pack_->file(), e.begin, e.end);
    StreamRingReader rr(s);
    RingPoint p, prev;
    bool first = true;
    while (rr.next(p)) {
        if (!first && !p.cut) aaLine(c, m.x(prev.x), m.y(prev.y), m.x(p.x), m.y(p.y), 1.0f, t_.mapCoast, 230);
        prev = p, first = false;
    }
    if (!s.ok()) failed_ = true;
}

void DetailPainter::lineFeature(Canvas& c, const TileInfo&, const FeatureEntry& e, const Tf& m, Rgb col, float w) {
    ByteStream s(*pack_->file(), e.begin, e.end);
    LineReader lr(s);
    int32_t x, y, px = 0, py = 0;
    bool first = true;
    while (lr.next(x, y)) {
        if (!first) aaLine(c, m.x(px), m.y(py), m.x(x), m.y(y), w, col, 255);
        px = x, py = y, first = false;
    }
    if (!s.ok()) failed_ = true;
}

void DetailPainter::strokeLines(Canvas& c, int y0, int rows) {
    int r0, r1, c0, c1;
    // Lines are up to 1.5 px wide: take tiles a little past the half-strip.
    tileRange(r0, r1, c0, c1, float(y0) - 2, float(y0 + rows) + 2);
    for (int r = r0; r <= r1 && !failed_; ++r)
        for (int col = c0; col <= c1 && !failed_; ++col) {
            TileInfo t;
            if (!tileFor(r, col, t)) continue;
            const Tf m = tf(r, col);
            for (int i = 0; i < t.featureCount; ++i) {
                FeatureEntry e;
                if (!pack_->feature(t, i, e)) break;
                if (e.kind >= kTown) break;  // points come last
                if (!crosses(e, float(y0) - 2, float(y0 + rows) + 2, m.sy, m.oy)) continue;
                switch (e.kind) {
                    case kRing: coast(c, t, e, m); break;
                    case kRiver: lineFeature(c, t, e, m, t_.mapRiver, 1.2f); break;
                    case kRoad: lineFeature(c, t, e, m, t_.mapRoadMinor, 1.0f); break;
                    case kMajorRoad: lineFeature(c, t, e, m, t_.mapRoad, 1.5f); break;
                    case kRunway: lineFeature(c, t, e, m, t_.mapRunway, 1.5f); break;
                }
            }
        }
}

void DetailPainter::paint(Canvas& c, int y0, int rows) {
    const Rect band{0, y0, 240, rows};
    const Rect saved = c.clip();
    c.setClip(saved.intersect(band));
    c.fill(c.clip(), rgb565(t_.mapWater));
    if (!failed_) fillLand(c, y0, rows);
    if (!failed_) strokeLines(c, y0, rows);
    c.setClip(saved);
}

// ------------------------------------------------------------------ names

int DetailPainter::layout(int w, const Rect* keep, int nKeep) {
    nMarks_ = 0;
    verifyView();
    if (failed_) return 0;
    const int r0 = vr0_, r1 = vr0_ + vrows_ - 1, c0 = vc0_, c1 = vc0_ + vcols_ - 1;
    // Gather candidates in priority order: four passes over the view's points (large airports, medium,
    // towns by rank, small fields), so the marks array holds the most important ones first.
    auto pass = [&](uint8_t kind, uint8_t rank) {
        for (int r = r0; r <= r1 && !failed_; ++r)
            for (int col = c0; col <= c1 && !failed_; ++col) {
                TileInfo t;
                if (!tileFor(r, col, t)) continue;
                const Tf m = tf(r, col);
                for (int i = t.featureCount - 1; i >= 0 && nMarks_ < kMaxMarks; --i) {  // points are last
                    FeatureEntry e;
                    if (!pack_->feature(t, i, e) || e.kind < kTown) break;
                    if (e.kind != kind || e.rank != rank) continue;
                    ByteStream s(*pack_->file(), e.begin, e.end);
                    Point p;
                    if (!readPoint(s, p)) continue;
                    const float x = m.x(p.x), y = m.y(p.y);
                    if (x < 4 || x > w - 4 || y < 18 || y > v_.h - 4) continue;
                    DetailMark& k = marks_[nMarks_++];
                    k = DetailMark{};
                    k.x = int16_t(std::lround(x)), k.y = int16_t(std::lround(y));
                    k.kind = kind, k.rank = rank;
                    std::memcpy(k.name, p.name, sizeof k.name);
                }
            }
    };
    pass(kAirport, 0), pass(kAirport, 1);
    for (uint8_t rank = 0; rank <= 2; ++rank) pass(kTown, rank);
    pass(kAirport, 2);
    // Place labels in that order: right, left, below, above; skip one that would overlap.
    int towns = 0, mediums = 0;
    for (int i = 0; i < nMarks_; ++i) {
        DetailMark& k = marks_[i];
        if (k.kind == kTown && towns >= kTownCap[level_]) continue;
        if (level_ == 2 && k.kind == kAirport && k.rank == 1 && mediums >= kMediumCapLevel2) continue;
        char text[21];
        std::memcpy(text, k.name, sizeof text);
        if (k.kind == kTown && t_.upperHeads)
            for (char* q = text; *q; ++q)
                if (*q >= 'a' && *q <= 'z') *q = char(*q - 32);
        const Rect probe = ui::textBox(kFontSmall, 0, 0, text);
        const int tw = probe.w, th = probe.h, asc = -probe.y;
        const int cands[4][2] = {{k.x + 4, k.y + asc / 2}, {k.x - 4 - tw, k.y + asc / 2},
                                 {k.x - tw / 2, k.y + 4 + asc}, {k.x - tw / 2, k.y - 4}};
        for (const auto& cand : cands) {
            const Rect b{cand[0] - 1, cand[1] - asc, tw + 2, th};
            if (b.x < 2 || b.right() > w - 2 || b.y < 2 || b.bottom() > v_.h - 2) continue;
            bool clear = true;
            for (int j = 0; j < nKeep && clear; ++j) clear = !b.intersects(keep[j]);
            for (int j = 0; j < i && clear; ++j) clear = !(marks_[j].labelled && b.intersects(marks_[j].box()));
            if (!clear) continue;
            k.labelled = true;
            k.lx = int16_t(cand[0]), k.ly = int16_t(cand[1]);
            k.bx = int16_t(b.x), k.by = int16_t(b.y), k.bw = int16_t(b.w), k.bh = int16_t(b.h);
            std::memcpy(k.name, text, sizeof text);
            towns += k.kind == kTown;
            mediums += k.kind == kAirport && k.rank == 1;
            break;
        }
    }
    return nMarks_;
}

}  // namespace basemap
```

Notes for the implementer:
- `ui::textBox(font, x, y, text)` is the helper `MapScreen.cpp` uses; check what its rect means (box from the baseline) and adjust `asc` so labels sit as in the mockups.
- The byte-wise prefix parity: after `v ^= v >> 1; v ^= v >> 2; v ^= v >> 4`, bit (7 - i) of `v` is the parity of bits 0..i counting from the most significant bit, which is what an MSB-first row needs. Write a two-line unit check of it into `test_detail_painter` if unsure.
- Coastline needs only rings that cross the half-strip, which `strokeLines` already filters.

- [ ] **Step 4: Run the test**

Run: `pio test -e test -f test_detail_painter`
Expected: PASS (5 tests). Then dump a chart to PNG once (write `ch.px` as PPM in a throwaway test, convert with `tools/ppm2png.py`) and compare it with `tools/map_preview.py map_small.pak --radius 25`: same land, water, roads and rivers apart from anti-aliasing. Send the controller both images.

- [ ] **Step 5: Commit**

```bash
git add firmware/lib/core/map/DetailPainter.h firmware/lib/core/map/DetailPainter.cpp firmware/test/test_detail_painter
git commit -m "Map: paint the detailed chart in half-strips; place town and airport names"
```

---

### Task 10: The Live Map uses the pack

**Files:**
- Modify: `firmware/lib/core/screens/MapScreen.h`, `MapScreen.cpp`
- Modify: `firmware/lib/core/screens/MapPlanes.cpp`
- Modify: `firmware/lib/core/screens/ScreenApp.h`, `ScreenApp.cpp`
- Modify: `firmware/test/common/GoldenRender.h`
- Test: `firmware/test/test_map_screen/test_main.cpp`

Behaviour (spec §4, refinements 2 and 5):
- `ScreenApp::setMap(basemap::MapPack*)` passes the pack to `MapScreen`, which passes it to `MapBase`.
- `MapBase::rebuild()` decides `detailed_ = pack && pack->isOpen() && pack->covers(home, radius)`. Built-in: exactly today's code. Detailed: builds the key; if `cache.matches(key)`, the marks are read from the saved chart (no tiles touched) and the chart is ready; otherwise it starts a job: `cache.begin()`, `painter.begin()`, `layout()`, `jobRow_ = 0`, `ready_ = false`.
- The job works by **rows, not fixed strips**, because the renderer chunks damage into 16-row strips starting wherever the damage starts: `MapBase::update()` adds damage `{0, jobRow_, 240, kJobRows}` (`kJobRows = 32`, about two strips a frame). `MapBase::draw()` looks at the rows its canvas holds:
  - rows above `jobRow_` are saved already: read them back;
  - rows from `jobRow_` on, when the canvas is full width and starts at or above `jobRow_`: paint (half-strips), overlay (clipped to those rows), `cache.writeRows`, `jobRow_` moves on;
  - any other rows (a plane's old position being cleared further down): fill with water for now; the job repaints them when it gets there.
  - When `jobRow_` reaches 270, the next `update()` saves the marks, writes the key (`cache.finish()`), sets `ready_ = true`, `++version_`, and adds full damage.
- `MapBase::draw()` when ready: for each row of the clip inside the chart, `cache.readSpan(y, clip.x, clip.w, &c.at(clip.x, y))`.
- Any read or write failure: `pack->readFailed()`, `detailed_ = false`, `full_ = true` (the next update rebuilds with the built-in map).
- Radius settle: when only the radius changed and the map is detailed, wait until `scene_->radiusNm` has been the same for 1.5 s of `update(dt)` before rebuilding.
- `MapBase::ready()`: built-in always true; detailed true once the job finished. `MapPlanes` draws nothing and `MapStrip` shows "Drawing map…" (centred, `t_.mapText`) while `!base_.ready()`.
- `MapBase::chartMark(i)` returns `ChartMark { int16 x, y; bool labelled; Rect box; }` from either the airport marks or the detail marks; `MapPlanes` uses it in place of `mark(i)`.
- Arena: `Scratch` becomes

```cpp
    struct Scratch {
        union {
            struct {
                uint8_t mask[basemap::LandMask::kBytes];
                basemap::AirportMark marks[MapBase::kMaxMarks];
            } builtin;
            struct {
                uint8_t sub[basemap::DetailPainter::kScratchBytes];
                basemap::DetailMark marks[basemap::DetailPainter::kMaxMarks];
            } detail;
        };
        MapPlanes::Plane planes[Scene::kMaxFlights];
    };
```

`ScreenArena::claim` already `static_assert`s the size. If it does not fit in 4,096 B, shrink `DetailMark::name` to 17 bytes and `kMaxMarks` to 28 before touching the arena size, and record the numbers in the spec's as-built note.

- Theme key: `uint16_t themeKey = assets::crc16(reinterpret_cast<const uint8_t*>(&t_.mapWater), offsetof(Theme, mapCard) - offsetof(Theme, mapWater))` combined with `crc16` over `mapRoad..mapTown` (both ranges are plain `Rgb` arrays of bytes), plus `t_.upperHeads`.

- [ ] **Step 1: Write the failing tests**

In `GoldenRender.h`, give `Rig` a map card:

```cpp
    basemap::MapPack map;
    // A card holding the 40 nm fixture map pack.
    void useMap() {
        sd.add(basemap::MapPack::kPath, loadFixture("map_small.pak"));
        map.begin(&sd);
        app->setMap(&map);
    }
```

and a `bool mapPack = false` parameter on `golden::render` that calls it. In `test_map_screen/test_main.cpp` add:

```cpp
// The board's home with the detailed map at three radii (levels 0, 0 and 1), both themes.
static void sceneHome(scr::Scene& s, float radius) {
    scr::sceneQuiet(s);  // planes and trails as the quiet scene
    s.homeLat = 39.4176, s.homeLon = -77.3743, s.radiusNm = radius, s.locationSet = true;
}
static void home10(scr::Scene& s) { sceneHome(s, 10); }
static void home25(scr::Scene& s) { sceneHome(s, 25); }
static void home35(scr::Scene& s) { sceneHome(s, 35); }

static void test_detail_goldens() {
    golden::render(ScreenId::Map, home10, "map_detail_10", scr::cinematicSky(), 0, 75, false, true);
    golden::render(ScreenId::Map, home25, "map_detail_25", scr::cinematicSky(), 0, 75, false, true);
    golden::render(ScreenId::Map, home35, "map_detail_35", scr::cinematicSky(), 0, 75, false, true);
    golden::render(ScreenId::Map, home25, "map_detail_25_ed", scr::editorial(), 0, 75, false, true);
}

static void test_detail_reopens_from_the_saved_chart() {
    golden::Rig g;
    g.useMap();
    home25(g.app->scene());
    g.app->sceneUpdated();
    g.show(ScreenId::Map);
    g.frames(30);
    auto& f = *static_cast<MemFile*>(g.map.file());
    const int writes = f.writes;
    TEST_ASSERT_TRUE(writes > 0);
    g.show(ScreenId::Radar), g.frames(5), g.show(ScreenId::Map), g.frames(30);
    TEST_ASSERT_EQUAL(writes, f.writes);  // no rebuild
}

static void test_detail_falls_back_when_home_is_outside() {
    golden::Rig g;
    g.useMap();
    auto& s = g.app->scene();
    sceneHome(s, 25);
    s.homeLat = 45.0;  // far outside the 40 nm pack
    g.app->sceneUpdated();
    g.show(ScreenId::Map);
    g.frames(30);
    golden::Rig plain;
    sceneHome(plain.app->scene(), 25);
    plain.app->scene().homeLat = 45.0;
    plain.app->sceneUpdated();
    plain.show(ScreenId::Map);
    plain.frames(30);
    TEST_ASSERT_EQUAL_MEMORY(plain.d.px.data(), g.d.px.data(), plain.d.px.size() * 2);
}

static void test_detail_card_pulled_falls_back_without_a_crash() {
    golden::Rig g;
    g.useMap();
    home25(g.app->scene());
    g.app->sceneUpdated();
    g.show(ScreenId::Map);
    g.frames(30);
    static_cast<MemFile*>(g.map.file())->fail = true;
    g.app->scene().flights[0].lat += 0.01;  // planes move: the chart is read back
    g.app->sceneUpdated();
    g.frames(10);
    TEST_ASSERT_FALSE(g.map.isOpen());
    g.assertNoStale();
}

static void test_detail_radius_settles_before_rebuilding() {
    golden::Rig g;
    g.useMap();
    home25(g.app->scene());
    g.app->sceneUpdated();
    g.show(ScreenId::Map);
    g.frames(30);
    auto& f = *static_cast<MemFile*>(g.map.file());
    const int writes = f.writes;
    for (float r : {24.f, 23.f, 22.f}) {
        g.app->scene().radiusNm = r;
        g.app->sceneUpdated();
        g.frames(10);  // 1/3 s each
    }
    TEST_ASSERT_EQUAL(writes, f.writes);
    g.frames(60);  // 2 s steady
    TEST_ASSERT_TRUE(f.writes > writes);
}
```

(`writes` counts `writeAt` calls: the job's rows, the marks and the key.) Register them in `main`.

- [ ] **Step 2: Run them to see them fail**

Run: `pio test -e test -f test_map_screen`
Expected: compile errors (`setMap`, `useMap`).

- [ ] **Step 3: Implement**

`MapScreen.h` (`MapBase`): add includes `map/ChartCache.h`, `map/DetailPainter.h`, `map/MapPack.h`; public `void setPack(basemap::MapPack* p) { pack_ = p, full_ = true; }`, `bool ready() const { return !detailed_ || ready_; }`, `struct ChartMark { int16_t x, y; bool labelled; gfx::Rect box; }; int chartMarkCount() const; ChartMark chartMark(int i) const;`; change `attach` to take the `Scratch` pieces both modes need:

```cpp
    void attach(uint8_t* mask, basemap::AirportMark* marks, uint8_t* sub, basemap::DetailMark* dmarks,
                const ScreenArena& a, const void* owner);
```

private members:

```cpp
    static constexpr int kJobRows = 32;
    static constexpr float kRadiusSettleS = 1.5f;
    void startDetail();
    void detailDraw(gfx::Canvas& c, const gfx::Rect& chart);
    void overlay(gfx::Canvas& c, const gfx::Rect& chart);  // rings, marks, names, home, scale bar, header
    void dropDetail();                                     // a card error: back to the built-in map
    basemap::MapPack* pack_ = nullptr;
    basemap::DetailPainter painter_;  // constructed with the theme; setPack also calls painter_.setPack(p)
    uint8_t* sub_ = nullptr;
    ArenaArray<basemap::DetailMark, basemap::DetailPainter::kMaxMarks> dmarks_;
    bool detailed_ = false, ready_ = true;
    int jobRow_ = 0;  // rows above it are painted and saved
    float settle_ = 0, pendingRadius_ = 0;
    basemap::ChartKey chartKey_;
    uint16_t themeKey_ = 0;
```

`MapBase`'s constructor initialises `painter_(t)`; `setPack` is `pack_ = p, painter_.setPack(p), full_ = true`.

`MapScreen.cpp`, the pieces (keep the built-in code paths byte-for-byte; move the overlay part of `draw()` — from `c.setClip(chart)` to the end — into `overlay()` so both modes share it, with the airport loop replaced by a `detailed_` branch that draws `DetailMark`s: airports `aaCircle(…, rank == 0 ? 2.6f : rank == 1 ? 1.8f : 1.4f, t_.mapAirport)`, towns a 3×3 `c.fill` in `t_.mapTown`, labels `drawText(c, kFontSmall, lx, ly, name, kind == kTown ? t_.mapTown : t_.mapText, Align::Left, 190)`):

```cpp
void MapBase::rebuild() {
    ++version_;
    located_ = scene_->locationSet;
    radius_ = scene_->radiusNm;
    nMarks_ = 0;
    std::snprintf(header_, sizeof header_, "LIVE MAP · %d NM", int(radius_ + 0.5f));
    if (!located_) return;
    buildShades();
    view_ = basemap::viewFor(scene_->homeLat, scene_->homeLon, radius_);
    // (scale bar and fixed_[] exactly as today)
    ...
    detailed_ = pack_ && pack_->isOpen() && pack_->covers(scene_->homeLat, scene_->homeLon, radius_);
    if (detailed_) return startDetail();
    ready_ = true;
    mask_.build(view_);
    const Rect keep[] = {fixed_[0], fixed_[1], fixed_[2], homeBox(), {0, kMapH, 240, 320 - kMapH}};
    nMarks_ = basemap::airportsInView(view_, marks_.data(), kMaxMarks);
    basemap::placeLabels(marks_.data(), nMarks_, kFontSmall, 240, keep, int(sizeof keep / sizeof keep[0]));
}

void MapBase::startDetail() {
    basemap::ChartKey k;
    k.latE5 = int32_t(std::lround(scene_->homeLat * 1e5)), k.lonE5 = int32_t(std::lround(scene_->homeLon * 1e5));
    k.radiusNm10 = uint16_t(std::lround(radius_ * 10));
    k.theme = themeKey_;
    k.level = uint8_t(pack_->levelFor(radius_));
    k.packId = pack_->packId();
    k.format = uint16_t(basemap::kChartFormat * 256 + sizeof(basemap::DetailMark));  // a drawing or mark-layout change
    chartKey_ = k;
    basemap::ChartCache cache(*pack_->file(), pack_->cacheOffset(), pack_->cacheSize());
    if (cache.matches(k)) {  // the saved chart holds its marks: no tiles to read
        uint32_t n = 0;
        if (!cache.readExtra(0, &n, 4) || n > uint32_t(basemap::DetailPainter::kMaxMarks) ||
            !cache.readExtra(4, dmarks_.data(), n * sizeof(basemap::DetailMark)))
            return dropDetail();
        nMarks_ = int(n), ready_ = true;
        return;
    }
    painter_.begin(view_, radius_, sub_, dmarks_.data());
    const Rect keep[] = {fixed_[0], fixed_[1], fixed_[2], homeBox()};
    nMarks_ = painter_.layout(240, keep, int(sizeof keep / sizeof keep[0]));
    if (painter_.failed() || !cache.begin(k)) return dropDetail();
    ready_ = false, jobRow_ = 0;
}

void MapBase::dropDetail() {
    if (pack_) pack_->readFailed();
    detailed_ = false, ready_ = true, full_ = true;
}
```

`update()`:

```cpp
void MapBase::update(float dt, DamageList& damage) {
    if (!mask_.bits()) return;
    marks_.check();
    if (scene_ && (full_ || dirty_ || settle_ > 0)) {
        ViewKey k;
        k.lat = scene_->homeLat, k.lon = scene_->homeLon, k.radius = scene_->radiusNm, k.located = scene_->locationSet;
        const bool onlyRadius = k.lat == key_.lat && k.lon == key_.lon && k.located == key_.located && k.radius != key_.radius;
        if (!full_ && detailed_ && onlyRadius) {
            // A radius being adjusted: rebuild once it has stayed put for kRadiusSettleS.
            if (k.radius != pendingRadius_) pendingRadius_ = k.radius, settle_ = kRadiusSettleS;
            else if ((settle_ -= dt) <= 0) settle_ = 0, key_ = k, rebuild(), full_ = true;
        } else if (full_ || !(k == key_)) {
            settle_ = 0, key_ = k, rebuild(), full_ = true;
        }
    }
    dirty_ = false;
    if (detailed_ && !ready_) {
        if (jobRow_ < kMapH) {  // the job: only its rows are damaged, never the whole chart
            damage.add({0, jobRow_, 240, std::min(kJobRows, kMapH - jobRow_)});
            full_ = false;
            return;
        }
        basemap::ChartCache cache(*pack_->file(), pack_->cacheOffset(), pack_->cacheSize());
        const uint32_t n = uint32_t(nMarks_);
        if (!cache.writeExtra(0, &n, 4) || !cache.writeExtra(4, dmarks_.data(), n * sizeof(basemap::DetailMark)) ||
            !cache.finish(chartKey_))
            return dropDetail();
        ready_ = true, ++version_, full_ = true;
    }
    if (full_) damage.add({0, 0, 240, 320});
    full_ = false;
}
```

Add `static_assert(4 + sizeof(basemap::DetailMark) * basemap::DetailPainter::kMaxMarks <= basemap::ChartCache::kExtraBytes)` beside `startDetail()`, and `#include <algorithm>` is already there.

`draw()`:

```cpp
void MapBase::draw(Canvas& c) {
    marks_.check();
    const Rect cl = c.clip();
    Rect below = Rect{0, kMapH, 240, 320 - kMapH}.intersect(cl);
    if (!below.empty()) c.fill(below, rgb565(t_.mapWater));
    const Rect chart = Rect{0, 0, 240, kMapH}.intersect(cl);
    if (chart.empty()) return;
    if (!located_) { /* today's "Set location" */ }
    if (detailed_) return detailDraw(c, chart);
    for (int y = chart.y; y < chart.bottom(); ++y)
        chartRow(&c.at(chart.x, y), mask_.bits(), chart.x, chart.right(), y, shade_);
    overlay(c, chart);
}

void MapBase::detailDraw(Canvas& c, const Rect& chart) {
    basemap::ChartCache cache(*pack_->file(), pack_->cacheOffset(), pack_->cacheSize());
    int y = chart.y;
    // Rows already saved (all of them once the chart is ready): read them back.
    const int savedEnd = std::min(chart.bottom(), ready_ ? kMapH : jobRow_);
    for (; y < savedEnd; ++y)
        if (!cache.readSpan(y, chart.x, chart.w, &c.at(chart.x, y))) return dropDetail();
    if (y >= chart.bottom()) return;
    // Rows the job has not reached. It can only take rows starting exactly where it stands, full width.
    const bool full = chart.x == 0 && chart.w == 240 && c.bandX() == 0 && c.width() == 240;
    if (!full || y != jobRow_) {
        c.fill({chart.x, y, chart.w, chart.bottom() - y}, rgb565(t_.mapWater));
        return;
    }
    const int y1 = chart.bottom();
    const Rect saved = c.clip();
    c.setClip({0, y, 240, y1 - y});
    for (int r = y; r < y1; r += basemap::DetailPainter::kRows)
        painter_.paint(c, r, std::min(basemap::DetailPainter::kRows, y1 - r));
    if (painter_.failed()) return c.setClip(saved), dropDetail();
    overlay(c, Rect{0, y, 240, y1 - y});
    c.setClip(saved);
    if (!cache.writeRows(y, y1 - y, &c.at(0, y))) return dropDetail();
    jobRow_ = y1;
}
```

`MapScreen::enter()` attaches both scratch views:

```cpp
void MapScreen::enter() {
    Scratch* s = arena_.claim<Scratch>(this);
    base_.attach(s->builtin.mask, s->builtin.marks, s->detail.sub, s->detail.marks, arena_, this);
    planes_.attach(s->planes, arena_, this);
    base_.reset(), planes_.reset(), strip_.reset();
}
```

(`mask` and `sub` overlap in the union: only the active mode uses its half.)

`MapStrip`: give it `const MapBase* base_` (set in `MapScreen`'s constructor) and, in `update()`, fold `base_->ready()` into the signature; in `draw()`, when `!base_->ready()`, draw the panel and `drawText(c, kFontBody, 120, kBox.y + 24, t_.upperHeads ? "DRAWING MAP…" : "Drawing map…", t_.mapText, Align::Center)` and return.

`MapPlanes.cpp`: replace the airport loop in the cost function with

```cpp
    for (int i = 0; i < base_.chartMarkCount(); ++i) {
        const MapBase::ChartMark m = base_.chartMark(i);
        soft({m.x - 3, m.y - 3, 7, 7});
        if (m.labelled) hard(m.box);
    }
```

and in `sync()` treat `!base_.ready()` like `!base_.located()` (no planes drawn); `version_` bumps when the job finishes, which already triggers the planes' full redraw.

`ScreenApp`: `void setMap(basemap::MapPack* p);` → `map_.setPack(p); host().reenter();` (use whatever `setLogos` does to re-enter the current screen). `MapScreen::setPack(p)` forwards to `base_.setPack(p)`.

`themeKey_`: computed once in `MapBase`'s constructor:

```cpp
    const auto* a = reinterpret_cast<const uint8_t*>(&t.mapWater);
    const auto* b = reinterpret_cast<const uint8_t*>(&t.mapRoad);
    themeKey_ = assets::crc16(a, offsetof(Theme, mapCard) - offsetof(Theme, mapWater));
    themeKey_ = assets::crc16(b, 4 * sizeof(gfx::Rgb), themeKey_);
    themeKey_ = uint16_t(themeKey_ ^ (t.upperHeads ? 0x5A5A : 0));
```

(`gfx::Rgb` is three `uint8_t`; check `sizeof(gfx::Rgb) == 3` with a `static_assert`.)

- [ ] **Step 4: Run the tests, look at the goldens**

Run: `UPDATE_GOLDEN=1 pio test -e test -f test_map_screen`, then convert the four new goldens with `tools/ppm2png.py` and compare each with `tools/map_preview.py firmware/test/fixtures/map_small.pak --radius <r>`. They should match apart from planes, anti-aliasing and label positions that differ by a pixel or two. Send the controller the images. Then `pio test -e test` (all suites): PASS, and the existing map goldens unchanged.

- [ ] **Step 5: Commit**

```bash
git add -A firmware/lib/core/screens firmware/lib/core/map firmware/test
git commit -m "Live Map: draw from the map pack, saved on the card; built-in map as the fallback"
```

---

### Task 11: Opening the pack and reporting its status

**Files:**
- Modify: `firmware/src/main_esp32.cpp`, `firmware/src/main_sim.cpp`
- Modify: `firmware/web/index.html`

- [ ] **Step 1: main_esp32.cpp**

Next to `assets::LogoStore logos;` add `basemap::MapPack mapPack;` (include `map/MapPack.h`). After `logos.begin(&sd);`:

```cpp
    mapPack.begin(&sd);
    if (mapPack.isOpen())
        Serial.printf("[sd] map.pak: %u nm around %.4f, %.4f\n", unsigned(mapPack.coverNm()),
                      mapPack.homeLatE5() * 1e-5, mapPack.homeLonE5() * 1e-5);
    else if (mapPack.error()[0])
        Serial.printf("[sd] map.pak not used: %s\n", mapPack.error());
```

After `screens->setLogos(&logos);`: `screens->setMap(&mapPack);`.

In `/api/info`'s `sd` object (next to `s["logos"]`):

```cpp
        JsonObject m = s["map"].to<JsonObject>();
        m["state"] = mapPack.isOpen() ? "detailed" : "builtin";
        const char* why = mapPack.error();
        m["why"] = why[0] ? why : nullptr;
        m["damaged"] = mapPack.damagedTiles();
        if (mapPack.isOpen()) {
            m["cover"] = mapPack.coverNm();
            m["lat"] = mapPack.homeLatE5() * 1e-5, m["lon"] = mapPack.homeLonE5() * 1e-5;
        }
```

The "home is outside the map pack" case is decided per view on the render thread; expose it with an `std::atomic<bool> outside_` in `MapPack` set by `MapBase::rebuild()` (`pack_->setOutside(!covers)`) and report `m["why"] = "home is outside the map pack; rebuild it"` when it is set and the pack is open. Add `void setOutside(bool v) { outside_ = v; } bool outside() const { return outside_; }` to `MapPack` with that atomic.

- [ ] **Step 2: main_sim.cpp**

After `logos.begin(&sd);`: `basemap::MapPack mapPack; mapPack.begin(&sd);` and after `screens->setLogos(&logos);`: `screens->setMap(&mapPack);`. Update the comment that lists what the sim's SD folder holds (`contrail/logos.pak` and `contrail/map.pak`).

- [ ] **Step 3: web/index.html**

Find `sdLine` (5a). Below the logos line add a map line built from `info.sd.map`:

```js
function mapLine(m) {
  if (!m) return '';
  if (m.state === 'detailed') {
    if (m.why) return 'Map: built-in (' + m.why + ')';
    const dmg = m.damaged ? ' (' + m.damaged + ' damaged tiles)' : '';
    return 'Map: detailed · ' + m.cover + ' nm around ' + m.lat.toFixed(2) + ', ' + m.lon.toFixed(2) + dmg;
  }
  return m.why && m.why !== 'no map.pak' ? 'Map: built-in (' + m.why + ')' : 'Map: built-in';
}
```

and render it where `sdLine`'s result is shown, in the same element style. Wording per spec §7. Add the credit line "Map data: Natural Earth, OurAirports, GeoNames" under it when `state === 'detailed'`.

- [ ] **Step 4: Build all three environments**

Run: `pio run -e cyd2usb`, `pio run -e sim`, `pio run -e cyd_sdload` (one at a time).
Expected: all succeed. Note the cyd2usb flash and RAM figures against 5a's (1,872,113 B flash).

- [ ] **Step 5: Commit**

```bash
git add firmware/src firmware/web firmware/lib/core/map/MapPack.h firmware/lib/core/screens/MapScreen.cpp
git commit -m "Open map.pak at boot; report the map's state on the web page"
```

---

### Task 12: On the board

**Files:**
- Modify: `docs/HARDWARE_TESTING.md`

Needs the board on `/dev/cu.usbserial-114440` (IP `<board-ip>`) with the card in. Copy the pack with the USB tool from 5a (`tools/sd_upload.py`, `cyd_sdload` firmware; see the README's SD section for its exact use) or ask the user to copy it with a card reader.

- [ ] **Step 1: Build and install the pack**

Run the builder for the board's home (Task 1 Step 4 command) and upload `map.pak` to `/contrail/map.pak`. Reflash `cyd2usb`.

- [ ] **Step 2: Measure**

With the Live Map on (the director's screen mode or the web page's screen setting), collect from serial and frame stats:
- rebuild time per level: radius 10, 25 (level 0), 50 (level 1), 100 (level 2), from the job's first strip to `finish()` (add a one-line `[map] chart drawn in N ms (level L)` log in `MapBase::update` when the job finishes);
- frame time and pushed pixels with traffic over 2 minutes, against the built-in map at the same radius (rename `map.pak` away for the comparison run, or set a home outside the pack);
- `minheap` over 10 minutes with the map up;
- render task stack high-water mark (`FatFile::writeAt` adds 512 B of stack);
- a restart: the map opens without a rebuild (no "chart drawn" log line).

Budgets (spec §8): rebuild ≤ 1.5 s, frames no slower than the built-in map, `minheap` ≥ 30 KB, flash ≤ +25 KB. If rebuild is over budget, first look at reads per tile (the log's block-read count), then `kJobRows = 64`; if frames are slower, read only the damaged rows' spans in sector-sized pieces (they already are spans) and measure the block reads per frame. Report every number, met or not.

- [ ] **Step 3: Failure checks**

- No `map.pak` (rename it): built-in map, web page "Map: built-in".
- A home outside the pack (set lat 45 on the web page): built-in map, "home is outside the map pack; rebuild it". Set it back.
- Card pulled with the map up: built-in map within a frame or two, no crash, "card error".
- A damaged tile (flip a byte in a level 0 tile of a copy with a hex editor, upload): detailed map with that tile as water, "(1 damaged tiles)".

- [ ] **Step 4: Eye check**

Take screenshots through the sim (`pio run -e sim` with `CONTRAIL_SD` pointing at a folder holding the same `map.pak`) at 10, 25, 50 and 100 nm in both themes, put them on one sheet and send it to the user; the board itself needs the user's eyes (add it to the pending eye checks in memory).

- [ ] **Step 5: Write the results into `docs/HARDWARE_TESTING.md`**

Add a section "Detailed map (5b)" in the style of "SD card and logos (5a)": what was run, the numbers, the failure checks, and what still needs a person.

- [ ] **Step 6: Commit**

```bash
git add docs/HARDWARE_TESTING.md firmware/lib/core/screens/MapScreen.cpp
git commit -m "Hardware testing: detailed map (5b)"
```

---

### Task 13: Docs and as-built notes

**Files:**
- Modify: `README.md`
- Modify: `docs/superpowers/specs/2026-11-05-contrail-detailed-map-design.md`

- [ ] **Step 1: README**

In the SD card section (after `logos.pak`), add "Detailed map": what it adds, the builder command with `--lat/--lon`, where the file goes (`/contrail/map.pak`), that it covers 250 nm around the home it was built for (rebuild it after moving the board), the size (5–15 MB), and the credit "Map data: Natural Earth (public domain), OurAirports (public domain), GeoNames (CC BY 4.0)". Mention `tools/map_preview.py` for checking a pack on the computer.

- [ ] **Step 2: Spec**

Set `Status:` to "Implemented (branch `detailed-map`, <date>)". Add *As built* notes where the build differs: the seven refinements listed at the top of this plan; the arena numbers (`Scratch` size with both modes); the measured budgets from Task 12 (a "§10 Results" section like 5a's §9); anything Task 12 changed (rows per frame).

- [ ] **Step 3: Full verification**

Run: `pio test -e test` (all suites), `pio run -e cyd2usb`, `pio run -e sim`, `pio run -e cyd_sdload`, `tools/.venv/bin/python -I tools/build_map.py --selftest`.
Expected: all pass. Record the test count.

- [ ] **Step 4: Commit**

```bash
git add README.md docs/superpowers/specs/2026-11-05-contrail-detailed-map-design.md
git commit -m "Docs: detailed map install and as-built notes"
```
