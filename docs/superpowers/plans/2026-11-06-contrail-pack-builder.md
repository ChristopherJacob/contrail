# SD Card Pack Builder (6c) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** An SD card page on the site that builds `contrail-sd-card.zip` (the detailed map for a chosen home plus
`logos.pak`) in the browser, with a `map.pak` byte-identical to `tools/build_map.py`'s.

**Architecture:** `build_map.py`'s source loading is split into *records* (every shape part, town and airport with
its position in the source files) and one `assemble(box, …)` step that filters and orders them. A new
`tools/map_regions.py` writes the world's records into 5° cells on the site; `load_regions` reads only the cells
around home and calls the same `assemble`, so both paths share the selection code. The page runs `build_map.py`
in Pyodide in a Web Worker and zips the result with Python's `zipfile`.

**Tech Stack:** Python 3.12 (standard library only), Pyodide 0.27.7 (Python 3.12, from jsDelivr; from npm for the
Node test), plain HTML/CSS/JS (no build step), GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-11-06-contrail-pack-builder-design.md`

**Branch:** `pack-builder` (already created, holds the spec).

---

## Background for the engineer

- `tools/build_map.py` builds `map.pak` for one home: `Box(lat, lon, 250)` is the coverage square;
  `load_sources(cache, box)` returns six lists `(land, lakes, rivers, roads, towns, airports)`; `write_pack(path,
  lat, lon, cover_nm, src)` writes the pack. The sources (about 106 MB) are in `tools/.cache/map/` (downloaded by
  `fetch(cache, refresh)` on first use). A build for KFDK takes about 9 s natively: 1.3 s loading, 7.7 s building.
- Byte-for-byte equality is the whole point: anything that changes the order of features, the floats, or which
  features are selected changes the pack. Python's `json` writes floats with `repr`, which round-trips exactly.
- Python for tools runs as `tools/.venv/bin/python -I` (the venv has Pillow; `build_map.py` needs only the
  standard library, so plain `python3 -I` works too).
- The site is static (`site/`), deployed by `.github/workflows/pages.yml`. Generated folders (`site/shots/`,
  `site/firmware/`) are git-ignored and made by the workflow.
- The board's web page is `firmware/web/index.html`; `firmware/scripts/web_embed.py` turns it into
  `src/web/WebPage.h` at build time.
- Every task's commands run from the repository root and use a scratch folder:
  `S=/private/tmp/claude-501/-Users-cjacob-Development-CYD-FlightTracker/349e1a41-e738-4917-a984-4bdae24fade4/scratchpad`
  (set it in each shell). Task 1 leaves `$S/before-kfdk.pak`, the reference pack later tasks compare with.

## File map

| File | Change |
|---|---|
| `tools/build_map.py` | `geo_parts`, `read_sources`, `assemble`, `load_sources` on top of them; `COVER`, `check_box`, `progress`; `REGION_DEG`, `region_files`, `load_regions`, `--regions` |
| `tools/map_regions.py` | new: split the sources into `cells/`, `shared/`, `index.json`, `search.json`; `--selftest` parity |
| `tools/sd_card.py` | new: `region_files(regions, lat, lon)`, `make(regions, lat, lon, logos, out, progress)`; CLI |
| `tools/sd_node_test.mjs` | new: Pyodide under Node builds KFDK and compares with the native pack |
| `tools/sd_site.sh` | new: puts `mapdata/` and `py/` on the site |
| `.github/workflows/ci.yml` | new `pack-builder` job |
| `.github/workflows/pages.yml` | cached sources, `sd_site.sh`, `logos.pak` with the install images |
| `.gitignore` | `site/mapdata/`, `site/py/` |
| `site/sd.html`, `site/sd.js`, `site/sd-worker.js` | new page |
| `site/style.css` | SD card page styles |
| `site/index.html`, `site/install.html` | nav link; SD text points at the page |
| `README.md` | Detailed map section points at the page |
| `firmware/web/index.html` | "Build an SD card for this location" link |
| spec | §9 "As built" |

---

### Task 1: Records and `assemble` in `build_map.py` (no change in output)

**Files:**
- Modify: `tools/build_map.py` (the `# sources` section, `write_pack`, `main`)

- [ ] **Step 1: Record the current output**

```bash
for h in "39.4176 -77.3743 kfdk" "52.3086 4.7639 eham"; do set -- $h
  python3 -I tools/build_map.py --lat $1 --lon $2 --out $S/before-$3.pak >/dev/null; done
shasum $S/before-*.pak
```
Keep the two hashes; Step 4 must reproduce them.

- [ ] **Step 2: Replace `geo_features` and `load_sources`**

Replace everything from `def geo_features(path, box):` to the end of `load_sources` (but keep `public_code` and
`airport_name` where they are, between them) with the code below. Add these constants after `MINOR_TYPES`:

```python
COVER = 250  # nm from home to each side of the coverage square
SHAPE_FILES = ("ne_10m_land", "ne_10m_lakes", "ne_10m_lakes_north_america", "ne_10m_rivers_lake_centerlines",
               "ne_10m_rivers_north_america", "ne_10m_roads")
KEPT_PROPS = ("scalerank", "type")  # the only shape properties the map reads
```

```python
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
```

(`public_code` and `airport_name` stay here unchanged.)

```python
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
```

- [ ] **Step 3: Progress callback, `COVER` and `check_box` in `write_pack` and `main`**

In `write_pack`, change the signature and the level loop:

```python
def write_pack(path, lat, lon, cover_nm, src, progress=None):
    """... progress(level) is called before each level is built (the SD card page shows it)."""
    box = Box(lat, lon, cover_nm)
    levels = []
    for i, (max_r, milli, metres) in enumerate(LEVELS):
        if progress:
            progress(i)
        grid = Grid(box, milli)
```
(Keep the rest of `write_pack` unchanged; if it has no docstring, add the one-line docstring above.)

In `main`: `ap.add_argument("--cover", type=int, default=COVER, …)`, and replace the range check with:

```python
    box = Box(a.lat, a.lon, a.cover)
    try:
        check_box(box)
    except ValueError as e:
        sys.exit(f"error: {e}")
```

- [ ] **Step 4: Same bytes as before**

```bash
for h in "39.4176 -77.3743 kfdk" "52.3086 4.7639 eham"; do set -- $h
  python3 -I tools/build_map.py --lat $1 --lon $2 --out $S/after-$3.pak >/dev/null; done
shasum $S/before-*.pak $S/after-*.pak
python3 -I tools/build_map.py --selftest
python3 -I tools/build_map.py --lat 89 --lon 0; echo "exit $?"
```
Expected: `after-*` hashes equal `before-*`; `selftest ok`; the last prints `error: the coverage square crosses …`
and `exit 1`.

- [ ] **Step 5: Commit**

```bash
git add tools/build_map.py
git commit -m "build_map: records and assemble(), a progress callback, check_box (same output)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Region loader and `tools/map_regions.py` with the parity self-test

**Files:**
- Modify: `tools/build_map.py` (after `check_box`; `main`)
- Create: `tools/map_regions.py`

- [ ] **Step 1: Write `tools/map_regions.py` with its self-test first**

```python
#!/usr/bin/env python3
"""Splits the map sources into 5-degree regions for the site's SD card page (6c spec §2).

  python3 -I tools/map_regions.py --out site/mapdata [--cache tools/.cache/map]
  python3 -I tools/map_regions.py --selftest [--cache tools/.cache/map]

Writes <out>/index.json, <out>/cells/<lat>_<lon>.json (south-west corner in degrees), <out>/shared/<layer>-<part>.json
and <out>/search.json. A cell holds every record build_map.read_sources makes whose shape's rings' bounding boxes,
or whose point, fall in it; shapes are never clipped, and a part with more than SHARED_POINTS points is written
once to shared/ and named in the cells. build_map.load_regions reads them back for one home and gives exactly
what load_sources gives; --selftest proves it, byte for byte, for a few homes. Sources missing from the cache are
downloaded first (build_map.fetch).
"""
import argparse
import csv
import io
import json
import os
import shutil
import sys
import tempfile
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_map  # noqa: E402
from build_map import REGION_DEG, Box, cell_range  # noqa: E402

SHARED_POINTS = 2000
HOMES = (("KFDK", 39.4176, -77.3743), ("Boston", 42.3656, -71.0096), ("Anchorage", 61.1743, -149.9963),
         ("Amsterdam", 52.3086, 4.7639))


def cell_key(cy, cx):
    return f"{cy * REGION_DEG}_{cx * REGION_DEG}"


def cells_of(rings):
    """The cells the rings' bounding boxes overlap, as (row, col) in REGION_DEG steps."""
    out = set()
    for r in rings:
        xs, ys = [p[0] for p in r], [p[1] for p in r]
        for cy in cell_range(min(ys), max(ys)):
            for cx in cell_range(min(xs), max(xs)):
                out.add((cy, cx))
    return out


def dump(path, value):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(value, f, separators=(",", ":"), ensure_ascii=False)
    return os.path.getsize(path)


def split(cache, out):
    shapes, towns, airports = build_map.read_sources(cache)
    cells, shared = {}, {}

    def cell(c):
        return cells.setdefault(c, {"shapes": {}, "towns": [], "airports": [], "shared": set()})

    for name, parts in shapes.items():
        for i, props, rings in parts:
            key = None
            if sum(len(r) for r in rings) > SHARED_POINTS:
                key = f"{name}-{i}"
                shared[key] = {"layer": name, "part": [i, props, rings]}
            for c in cells_of(rings):
                entry = cell(c)
                entry["shapes"].setdefault(name, []).append([i] if key else [i, props, rings])
                if key:
                    entry["shared"].add(key)
    for t in towns:
        cell((cell_range(t[2], t[2])[0], cell_range(t[3], t[3])[0]))["towns"].append(t)
    for a in airports:
        cell((cell_range(a[3], a[3])[0], cell_range(a[4], a[4])[0]))["airports"].append(a)

    shutil.rmtree(out, ignore_errors=True)
    index = {"deg": REGION_DEG, "cells": {}, "shared": {}}
    for key, value in shared.items():
        index["shared"][key] = dump(os.path.join(out, "shared", key + ".json"), value)
    for (cy, cx), c in sorted(cells.items()):
        key = cell_key(cy, cx)
        names = sorted(c.pop("shared"))
        index["cells"][key] = {"bytes": dump(os.path.join(out, "cells", key + ".json"), c), "shared": names}
    dump(os.path.join(out, "index.json"), index)
    search = write_search(cache, os.path.join(out, "search.json"))
    total = sum(v["bytes"] for v in index["cells"].values()) + sum(index["shared"].values())
    print(f"{out}: {len(cells)} cells, {len(shared)} shared parts, {total / 1e6:.1f} MB; search {search / 1e6:.1f} MB")


def write_search(cache, path):
    """The page's search index: airports (code, name, town, region, lat, lon) and towns (name, admin1, country,
    lat, lon, population), the towns biggest first."""
    airports = []
    with open(os.path.join(cache, "airports.csv"), encoding="utf-8") as f:
        for a in csv.DictReader(f):
            if a["type"] in ("large_airport", "medium_airport"):
                code = a.get("icao_code") or a.get("gps_code") or a["ident"]
            elif a["type"] == "small_airport":
                code = build_map.public_code(a)
            else:
                continue
            try:
                lat, lon = round(float(a["latitude_deg"]), 4), round(float(a["longitude_deg"]), 4)
            except ValueError:
                continue
            if code:
                airports.append([code, a["name"], a["municipality"], a["iso_region"], lat, lon])
    towns = []
    with zipfile.ZipFile(os.path.join(cache, "cities5000.zip")) as z:
        with z.open("cities5000.txt") as f:
            for row in csv.reader(io.TextIOWrapper(f, encoding="utf-8"), delimiter="\t"):
                if build_map.is_town(row):
                    towns.append([row[1], row[10], row[8], round(float(row[4]), 4), round(float(row[5]), 4),
                                  int(row[14] or 0)])
    towns.sort(key=lambda t: -t[5])
    return dump(path, {"airports": airports, "towns": towns})


def selftest(cache):
    assert list(cell_range(-0.1, 0.1)) == [-1, 0]
    assert list(cell_range(5.0, 9.99)) == [1]
    assert cells_of([[(-1, -1), (1, 1)]]) == {(-1, -1), (-1, 0), (0, -1), (0, 0)}
    with tempfile.TemporaryDirectory() as tmp:
        split(cache, tmp)
        for name, lat, lon in HOMES:
            box = Box(lat, lon, build_map.COVER)
            raw, regions = build_map.load_sources(cache, box), build_map.load_regions(tmp, box)
            for what, a, b in zip(("land", "lakes", "rivers", "roads", "towns", "airports"), raw, regions):
                assert a == b, f"{name}: {what} differ ({len(a)} from the sources, {len(b)} from the regions)"
            paks = []
            for which, src in (("raw", raw), ("regions", regions)):
                path = os.path.join(tmp, f"{which}.pak")
                build_map.write_pack(path, lat, lon, build_map.COVER, src)
                with open(path, "rb") as f:
                    paks.append(f.read())
            assert paks[0] == paks[1], f"{name}: the packs differ"
            files = build_map.region_files(json.load(open(os.path.join(tmp, "index.json"))), box)
            print(f"{name}: identical, {len(paks[0]) / 1e6:.2f} MB pack from {len(files)} files "
                  f"({sum(n for _, n in files) / 1e6:.1f} MB)")
    print("selftest ok")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out")
    ap.add_argument("--cache", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), ".cache", "map"))
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    build_map.fetch(a.cache, False)
    if a.selftest:
        return selftest(a.cache)
    if not a.out:
        ap.error("--out is required")
    split(a.cache, a.out)


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Run it to see it fail**

Run: `python3 -I tools/map_regions.py --selftest`
Expected: `ImportError: cannot import name 'REGION_DEG' from 'build_map'`.

- [ ] **Step 3: Add the region loader to `build_map.py`**

After `check_box`:

```python
# ------------------------------------------------------------------ regions (the SD card page, tools/map_regions.py)

REGION_DEG = 5


def cell_range(lo, hi):
    """The REGION_DEG cells from lo to hi degrees, both included, as cell numbers (floor(deg / REGION_DEG))."""
    return range(math.floor(lo / REGION_DEG), math.floor(hi / REGION_DEG) + 1)


def region_files(index, box):
    """[(path, bytes)] a build for this box reads from a regions folder: its cells, then the shared parts they name."""
    keys = [f"{cy * REGION_DEG}_{cx * REGION_DEG}" for cy in cell_range(box.lat0, box.lat1)
            for cx in cell_range(box.lon0, box.lon1)]
    keys = [k for k in keys if k in index["cells"]]
    shared = sorted({s for k in keys for s in index["cells"][k]["shared"]})
    return ([(f"cells/{k}.json", index["cells"][k]["bytes"]) for k in keys] +
            [(f"shared/{s}.json", index["shared"][s]) for s in shared])


def load_regions(regions, box):
    """load_sources from a regions folder (tools/map_regions.py): the same six lists, in the same order."""
    def read(path):
        with open(os.path.join(regions, path), encoding="utf-8") as f:
            return json.load(f)

    shapes = {name: {} for name in SHAPE_FILES}
    towns, airports = {}, {}
    for path, _ in region_files(read("index.json"), box):
        data = read(path)
        if path.startswith("shared/"):
            shapes[data["layer"]][data["part"][0]] = data["part"]
            continue
        for name, parts in data["shapes"].items():
            for p in parts:
                if len(p) == 3:  # [part] alone: the part is in shared/
                    shapes[name][p[0]] = p
        for t in data["towns"]:
            towns[t[0]] = t
        for a in data["airports"]:
            airports[a[0]] = a
    records = {name: [(i, props, [[tuple(p) for p in r] for r in rings]) for i, props, rings in
                      (parts[i] for i in sorted(parts))] for name, parts in shapes.items()}
    return assemble(box, records,
                    [tuple(towns[n]) for n in sorted(towns)],
                    [(n, rank, name, lat, lon, [tuple(tuple(p) for p in rw) for rw in rws])
                     for n, rank, name, lat, lon, rws in (airports[k] for k in sorted(airports))])
```

In `main`, add `ap.add_argument("--regions", help="read a regions folder (tools/map_regions.py) instead of the sources")`
and replace `fetch(a.cache, a.refresh)` / `src = load_sources(a.cache, box)` with:

```python
    if a.regions:
        src = load_regions(a.regions, box)
    else:
        fetch(a.cache, a.refresh)
        src = load_sources(a.cache, box)
```

- [ ] **Step 4: Run the self-test**

Run: `python3 -I tools/map_regions.py --selftest`
Expected: four lines `<home>: identical, … MB pack from N files (… MB)` and `selftest ok`. Note the per-home
download sizes (the spec says "a few MB"; report them). If a list differs, the message names it: fix the
splitter or loader, never `assemble`.

- [ ] **Step 5: Write the site data and try the CLI on it**

```bash
python3 -I tools/map_regions.py --out $S/mapdata
python3 -I tools/build_map.py --regions $S/mapdata --lat 39.4176 --lon -77.3743 --out $S/regions-kfdk.pak
shasum $S/before-kfdk.pak $S/regions-kfdk.pak
du -sh $S/mapdata; ls $S/mapdata/cells | wc -l; ls $S/mapdata/shared | wc -l
```
Expected: the two hashes are equal. Report the total size (it must stay well under Pages' 1 GB).

- [ ] **Step 6: Commit**

```bash
git add tools/build_map.py tools/map_regions.py
git commit -m "map_regions: the map sources in 5-degree cells for the SD card page, read back by load_regions

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: `tools/sd_card.py` and the Pyodide test

**Files:**
- Create: `tools/sd_card.py`, `tools/sd_node_test.mjs`

- [ ] **Step 1: Write `tools/sd_card.py`**

```python
#!/usr/bin/env python3
"""Makes contrail-sd-card.zip, as the site's SD card page does (6c spec §3): contrail/map.pak for one home, built
from a regions folder (tools/map_regions.py), and contrail/logos.pak when one is given.

  python3 -I tools/sd_card.py --regions site/mapdata --lat 39.4176 --lon -77.3743 [--logos logos.pak] --out sd.zip

The page's worker (site/sd-worker.js) calls region_files() and make() in Pyodide.
"""
import argparse
import json
import os
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_map  # noqa: E402


def home_box(lat, lon):
    box = build_map.Box(lat, lon, build_map.COVER)
    build_map.check_box(box)
    return box


def region_files(regions, lat, lon):
    """[(path, bytes)] under regions that a build for this home reads (index.json must be there already)."""
    with open(os.path.join(regions, "index.json"), encoding="utf-8") as f:
        return build_map.region_files(json.load(f), home_box(lat, lon))


def make(regions, lat, lon, logos, out, progress=None):
    """Writes the zip; progress(level) before each map level, then progress(len(LEVELS)) before zipping."""
    box = home_box(lat, lon)
    src = build_map.load_regions(regions, box)
    pak = out + ".map.pak"
    _, size = build_map.write_pack(pak, lat, lon, build_map.COVER, src, progress)
    if progress:
        progress(len(build_map.LEVELS))
    with zipfile.ZipFile(out, "w", zipfile.ZIP_STORED) as z:  # the packs barely compress
        z.write(pak, "contrail/map.pak")
        if logos:
            z.write(logos, "contrail/logos.pak")
    os.remove(pak)
    return {"map_bytes": size, "towns": len(src[4]), "airports": len(src[5]), "logos": bool(logos)}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--regions", required=True)
    ap.add_argument("--lat", type=float, required=True)
    ap.add_argument("--lon", type=float, required=True)
    ap.add_argument("--logos")
    ap.add_argument("--out", default="contrail-sd-card.zip")
    a = ap.parse_args()
    try:
        s = make(a.regions, a.lat, a.lon, a.logos, a.out, lambda level: print(f"level {level}"))
    except ValueError as e:
        sys.exit(f"error: {e}")
    print(f"{a.out}: map.pak {s['map_bytes'] / 1e6:.2f} MB ({s['towns']} towns, {s['airports']} airports)"
          + (", logos.pak" if s["logos"] else ""))


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Try it natively**

```bash
python3 -I tools/sd_card.py --regions $S/mapdata --lat 39.4176 --lon -77.3743 --out $S/sd.zip
unzip -l $S/sd.zip
unzip -p $S/sd.zip contrail/map.pak | shasum; shasum $S/before-kfdk.pak
```
Expected: `level 0` … `level 3`, then the summary; the zip lists `contrail/map.pak`; the hashes are equal.

- [ ] **Step 3: Write `tools/sd_node_test.mjs`**

```js
// Builds KFDK's SD card in Pyodide under Node, the way site/sd-worker.js does, and checks its map.pak against
// the native build (6c spec §5).
//   node tools/sd_node_test.mjs <path to pyodide/pyodide.mjs> <regions folder> <native map.pak>
import { readFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const [pyodideModule, regions, nativePak] = process.argv.slice(2);
if (!nativePak) {
    console.error("usage: node tools/sd_node_test.mjs <pyodide.mjs> <regions folder> <native map.pak>");
    process.exit(2);
}
const { loadPyodide } = await import(pathToFileURL(resolve(pyodideModule)).href);
const py = await loadPyodide();
const tools = dirname(fileURLToPath(import.meta.url));
for (const f of ["build_map.py", "gen_map.py", "sd_card.py"]) {
    py.FS.writeFile(`/home/pyodide/${f}`, readFileSync(`${tools}/${f}`));
}
py.FS.mkdir("/regions");
py.FS.mount(py.FS.filesystems.NODEFS, { root: resolve(regions) }, "/regions");

const sd = py.pyimport("sd_card");
const levels = [];
const started = Date.now();
const summary = sd.make("/regions", 39.4176, -77.3743, null, "/home/pyodide/sd.zip", (level) => levels.push(level))
    .toJs({ dict_converter: Object.fromEntries });
const seconds = (Date.now() - started) / 1000;
const built = py.runPython(`import zipfile; zipfile.ZipFile("/home/pyodide/sd.zip").read("contrail/map.pak")`).toJs();
const native = readFileSync(nativePak);

let ok = true;
if (Buffer.compare(Buffer.from(built), native) !== 0) {
    console.error(`map.pak differs: ${built.length} bytes from Pyodide, ${native.length} native`);
    ok = false;
}
if (levels.join() !== "0,1,2,3") {
    console.error(`progress was ${levels.join()}, expected 0,1,2,3`);
    ok = false;
}
console.log(`Pyodide ${py.version}: ${summary.towns} towns, ${summary.airports} airports in ${seconds.toFixed(1)} s`);
if (!ok) process.exit(1);
console.log("pyodide build matches");
```

- [ ] **Step 4: Run it**

```bash
mkdir -p $S/node && npm install --prefix $S/node pyodide@0.27.7 >/dev/null
node tools/sd_node_test.mjs $S/node/node_modules/pyodide/pyodide.mjs $S/mapdata $S/before-kfdk.pak
```
Expected: `Pyodide 0.27.7: 1959 towns, 456 airports in N s` and `pyodide build matches`. Report N: it is close to
what the browser will take. If N is over about 40 s, say so in the report; making the build faster is a separate
change (it must not change the bytes) and is not part of this task.

- [ ] **Step 5: Commit**

```bash
git add tools/sd_card.py tools/sd_node_test.mjs
git commit -m "sd_card: the SD card zip from map regions, and a Pyodide-under-Node parity test

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Site data script, workflows, ignores

**Files:**
- Create: `tools/sd_site.sh`
- Modify: `.github/workflows/pages.yml`, `.github/workflows/ci.yml`, `.gitignore`

- [ ] **Step 1: `tools/sd_site.sh`**

```bash
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
```
Then `chmod +x tools/sd_site.sh`.

- [ ] **Step 2: `.gitignore`**

Below `site/firmware/` add:
```
site/mapdata/
site/py/
```

- [ ] **Step 3: Pages workflow**

In `.github/workflows/pages.yml`, after the "Screenshots" step add:

```yaml
      # The map sources (about 106 MB) are kept a month: OurAirports and GeoNames change daily, Natural Earth is
      # pinned. A new month downloads them again.
      - name: Month
        id: month
        run: echo "month=$(date -u +%Y-%m)" >> "$GITHUB_OUTPUT"
      - name: Cache the map sources
        uses: actions/cache@v4
        with:
          path: tools/.cache/map
          key: map-sources-${{ steps.month.outputs.month }}
      - name: SD card page data
        run: tools/sd_site.sh site
```

In the "Latest release's install images" step, download `logos.pak` too:

```yaml
            gh release download "$tag" --repo "$GITHUB_REPOSITORY" --dir site/firmware \
              --pattern contrail-cyd2usb-install.bin --pattern contrail-cyd_microusb-install.bin --pattern logos.pak
            ls site/firmware/contrail-cyd2usb-install.bin site/firmware/contrail-cyd_microusb-install.bin site/firmware/logos.pak
```
and change the step's comment above it to say the install images and `logos.pak` are served from the site.
Change the final `ls -l site site/shots site/firmware` to `ls -l site site/shots site/firmware site/py && du -sh site/mapdata`.

- [ ] **Step 4: CI job**

Add to `.github/workflows/ci.yml` under `jobs:` (beside `test-and-build`):

```yaml
  pack-builder:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-python@v5
        with:
          python-version: "3.12"
      - uses: actions/setup-node@v4
        with:
          node-version: "22"
      - name: Month
        id: month
        run: echo "month=$(date -u +%Y-%m)" >> "$GITHUB_OUTPUT"
      - name: Cache the map sources
        uses: actions/cache@v4
        with:
          path: tools/.cache/map
          key: map-sources-${{ steps.month.outputs.month }}
      - name: Regions build the same packs as the sources
        run: python -I tools/map_regions.py --selftest
      - name: Pyodide builds the same pack
        run: |
          python -I tools/map_regions.py --out "$RUNNER_TEMP/mapdata"
          python -I tools/build_map.py --lat 39.4176 --lon -77.3743 --out "$RUNNER_TEMP/native.pak"
          npm install --prefix "$RUNNER_TEMP/node" pyodide@0.27.7
          node tools/sd_node_test.mjs "$RUNNER_TEMP/node/node_modules/pyodide/pyodide.mjs" "$RUNNER_TEMP/mapdata" "$RUNNER_TEMP/native.pak"
```

- [ ] **Step 5: Try the script locally**

```bash
tools/sd_site.sh site && ls site/py && du -sh site/mapdata && git status --short
```
Expected: three `.py` files; `git status` shows only the new/changed tracked files (no `site/mapdata` or `site/py`).

- [ ] **Step 6: Commit**

```bash
git add tools/sd_site.sh .gitignore .github/workflows/pages.yml .github/workflows/ci.yml
git commit -m "Workflows: SD card page data on Pages, logos.pak on the site, and the pack-builder CI job

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: The SD card page

**Files:**
- Create: `site/sd.html`, `site/sd.js`, `site/sd-worker.js`
- Modify: `site/style.css` (before `/* Footer */`, and a second `@media` block at the end)

- [ ] **Step 1: `site/sd-worker.js`**

```js
// SD card page worker (6c spec §3): runs tools/build_map.py (copied to py/) in Pyodide, fetches only the map
// regions near home, and hands back contrail-sd-card.zip.
// In: {lat, lon}. Out: {step: "python"} | {step: "data", got, total} | {step: "build", level} (level 3: zipping)
//     | {done: true, zip, summary} | {error: "load" | "build", what}.
const PYODIDE = "https://cdn.jsdelivr.net/pyodide/v0.27.7/full/";
const HOME = "/home/pyodide";

class LoadError extends Error {}

async function download(url, what) {
    let r;
    try {
        r = await fetch(url);
    } catch {
        throw new LoadError(what);
    }
    if (!r.ok) throw new LoadError(what);
    return new Uint8Array(await r.arrayBuffer());
}

let ready = null;  // Pyodide with the builder loaded, once per page

async function start() {
    try {
        importScripts(PYODIDE + "pyodide.js");
    } catch {
        throw new LoadError("Python (Pyodide)");
    }
    const py = await loadPyodide({ indexURL: PYODIDE }).catch(() => { throw new LoadError("Python (Pyodide)"); });
    for (const f of ["build_map.py", "gen_map.py", "sd_card.py"]) {
        py.FS.writeFile(`${HOME}/${f}`, await download(`py/${f}`, "the map builder"));
    }
    for (const d of ["/regions", "/regions/cells", "/regions/shared"]) py.FS.mkdir(d);
    py.FS.writeFile("/regions/index.json", await download("mapdata/index.json", "the map data"));
    return py;
}

const have = (py, path) => py.FS.analyzePath(path).exists;

onmessage = async ({ data: { lat, lon } }) => {
    try {
        postMessage({ step: "python" });
        ready ??= start();
        const py = await ready.catch((e) => { ready = null; throw e; });
        const sd = py.pyimport("sd_card");

        const files = sd.region_files("/regions", lat, lon).toJs();
        const total = files.reduce((n, [, bytes]) => n + bytes, 0);
        let got = 0;
        postMessage({ step: "data", got, total });
        for (const [path, bytes] of files) {
            if (!have(py, `/regions/${path}`)) {
                py.FS.writeFile(`/regions/${path}`, await download(`mapdata/${path}`, "the map data"));
            }
            got += bytes;
            postMessage({ step: "data", got, total });
        }

        let logos = null;  // no release yet: the map alone
        if (!have(py, `${HOME}/logos.pak`)) {
            try {
                const r = await fetch("firmware/logos.pak");
                if (r.ok) py.FS.writeFile(`${HOME}/logos.pak`, new Uint8Array(await r.arrayBuffer()));
            } catch { /* logos stay out */ }
        }
        if (have(py, `${HOME}/logos.pak`)) logos = `${HOME}/logos.pak`;

        postMessage({ step: "build", level: 0 });
        const summary = sd.make("/regions", lat, lon, logos, `${HOME}/sd.zip`, (level) => postMessage({ step: "build", level }))
            .toJs({ dict_converter: Object.fromEntries });
        const zip = py.FS.readFile(`${HOME}/sd.zip`);
        py.FS.unlink(`${HOME}/sd.zip`);
        postMessage({ done: true, zip, summary }, [zip.buffer]);
    } catch (e) {
        if (e instanceof LoadError) postMessage({ error: "load", what: e.message });
        else postMessage({ error: "build", what: String(e.message || e).trim().split("\n").pop() });
    }
};
```

- [ ] **Step 2: `site/sd.html`**

```html
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Contrail SD card</title>
<meta name="description" content="Build a microSD card for Contrail in the browser: the detailed Live Map for your home and real airline logos.">
<link rel="icon" href="favicon.svg" type="image/svg+xml">
<link rel="stylesheet" href="style.css">
<script type="module" src="sd.js"></script>
</head>
<body>
<header class="top">
  <div class="wrap">
    <a class="brand" href="./"><img src="favicon.svg" alt="">Contrail</a>
    <nav aria-label="Site">
      <a href="./">Showcase</a>
      <a href="install.html">Install</a>
      <a href="sd.html" aria-current="page">SD card</a>
      <a href="https://github.com/ChristopherJacob/contrail">GitHub</a>
    </nav>
  </div>
</header>

<main>
  <section>
    <div class="wrap">
      <h1>SD card</h1>
      <p class="lead">Make a microSD card with the detailed Live Map for your home and real airline logos. It's all
        built here in the browser; your location never leaves this computer.</p>

      <div id="builder">
        <div class="panel">
          <h3>1. Where is home?</h3>
          <label class="field" for="q">Search for an airport code or a town</label>
          <input class="input" id="q" type="search" autocomplete="off" spellcheck="false" placeholder="KFDK or Frederick"
                 aria-controls="results">
          <ul class="results" id="results" hidden></ul>
          <div class="row">
            <label class="field">Latitude <input class="input" id="lat" inputmode="decimal" autocomplete="off" placeholder="39.4176"></label>
            <label class="field">Longitude <input class="input" id="lon" inputmode="decimal" autocomplete="off" placeholder="-77.3743"></label>
            <button class="btn ghost" id="locate" type="button">Use my location</button>
          </div>
          <p class="muted near" id="near" aria-live="polite"></p>
          <p class="note warn" id="home-note" hidden></p>
        </div>

        <div class="panel">
          <h3>2. Build</h3>
          <button class="btn" id="build" type="button" disabled>Build the SD card</button>
          <div id="working" hidden>
            <progress id="bar" max="1" value="0"></progress>
            <p class="status" id="step" aria-live="polite"></p>
          </div>
          <p class="note warn" id="failed" hidden></p>
          <div id="result" hidden>
            <a class="btn" id="download" download="contrail-sd-card.zip">Download contrail-sd-card.zip</a>
            <p class="muted summary" id="summary"></p>
          </div>
        </div>
      </div>
      <p class="note warn" id="unsupported" hidden>This page needs a browser with Web Workers and WebAssembly, such as a
        current Chrome, Edge, Firefox or Safari.</p>

      <h2>3. Put it on the card</h2>
      <ol class="steps">
        <li><b>Use a FAT32 microSD card.</b> Most cards up to 32 GB come that way. Bigger cards are usually exFAT,
          which the board can't read: format them as FAT32 first (FAT16 works too).</li>
        <li><b>Unzip <code>contrail-sd-card.zip</code></b> onto the card, so the card has a <code>contrail</code>
          folder at the top holding <code>map.pak</code> and <code>logos.pak</code>.</li>
        <li><b>Put the card in the board and restart it.</b> The Live Map draws the detailed map while home and
          the radius lie inside the map's 250 nm; the Firmware card on the board's web page says which map it
          uses.</li>
        <li><b>Moved house?</b> Build the card again for the new home.</li>
      </ol>
      <p class="muted">Prefer the command line? <code>tools/build_map.py</code> in the
        <a href="https://github.com/ChristopherJacob/contrail#detailed-map">repository</a> makes the same map.</p>
    </div>
  </section>
</main>

<footer>
  <div class="wrap">
    <p>Contrail is free software under the <a href="https://github.com/ChristopherJacob/contrail/blob/main/LICENSE">GPL-3.0</a>.</p>
    <p>Airline logos are trademarks of their respective owners and are shown only to identify airlines; Contrail
      is not affiliated with any airline.</p>
    <p>Map data: <a href="https://www.naturalearthdata.com/">Natural Earth</a> (public domain),
      <a href="https://ourairports.com/data/">OurAirports</a> (public domain) and
      <a href="https://www.geonames.org/">GeoNames</a> (<a href="https://creativecommons.org/licenses/by/4.0/">CC BY 4.0</a>).
      Built with <a href="https://pyodide.org/">Pyodide</a>.</p>
  </div>
</footer>
</body>
</html>
```

- [ ] **Step 3: `site/sd.js`**

```js
// SD card page (6c spec §3): picks home (search, the browser's location, typed coordinates or ?lat=&lon=), checks
// the map can cover it, and runs sd-worker.js, which builds contrail-sd-card.zip with tools/build_map.py in
// Pyodide. Nothing about the location leaves the browser: every request is for a fixed file of this site or the CDN.
const COVER_NM = 250;  // build_map.COVER
const LEVELS = 3;      // build_map.LEVELS

const $ = (id) => document.getElementById(id);
const q = $("q"), results = $("results"), lat = $("lat"), lon = $("lon"), near = $("near"), homeNote = $("home-note");
const buildButton = $("build"), working = $("working"), bar = $("bar"), step = $("step"), failed = $("failed");
const result = $("result"), download = $("download"), summary = $("summary");

const mb = (n) => `${(n / 1e6).toFixed(1)} MB`;
let worker = null, busy = false, zipUrl = null;

if (!("Worker" in window) || typeof WebAssembly !== "object") {
    $("builder").hidden = true;
    $("unsupported").hidden = false;
}

// ---------------------------------------------------------------- home

let search = null;  // the promise of mapdata/search.json, once asked for
function loadSearch() {
    search ??= fetch("mapdata/search.json")
        .then((r) => { if (!r.ok) throw new Error(r.status); return r.json(); })
        .catch((e) => { search = null; throw e; });
    return search;
}

function home() {
    const a = Number(lat.value), b = Number(lon.value);
    if (!lat.value.trim() || !lon.value.trim() || !Number.isFinite(a) || !Number.isFinite(b)) return null;
    return Math.abs(a) <= 90 && Math.abs(b) <= 180 ? [a, b] : null;
}

// build_map.Box and check_box: the square COVER_NM from home to each side must stay inside +-180 and +-85.
function coverable([a, b]) {
    const dlat = COVER_NM / 60, dlon = dlat / Math.cos(a * Math.PI / 180);
    return a - dlat >= -85 && a + dlat <= 85 && b - dlon >= -180 && b + dlon <= 180;
}

const townLabel = (t) => [t[0], /^[A-Z]+$/.test(t[1]) ? t[1] : "", t[2]].filter(Boolean).join(", ");

async function nearest([a, b]) {
    const { towns } = await loadSearch();
    const k = Math.cos(a * Math.PI / 180);
    let best = null, bestD = Infinity;
    for (const t of towns) {
        const d = (t[3] - a) ** 2 + ((t[4] - b) * k) ** 2;
        if (d < bestD) best = t, bestD = d;
    }
    return best && { label: townLabel(best), nm: 60 * Math.sqrt(bestD) };
}

let checked = 0;  // ignores a slow "near" answer for a home that has changed since
async function update() {
    result.hidden = true;
    failed.hidden = true;
    const h = home();
    homeNote.hidden = true;
    near.textContent = "";
    if (!h) {
        buildButton.disabled = true;
        if (lat.value.trim() || lon.value.trim()) {
            homeNote.textContent = "Latitude is -90 to 90 and longitude -180 to 180, in decimal degrees.";
            homeNote.hidden = false;
        }
        return;
    }
    if (!coverable(h)) {
        buildButton.disabled = true;
        homeNote.textContent = "The map can't be built this close to the poles or the 180° meridian: its 250 nm square " +
            "would cross latitude ±85° or longitude ±180°.";
        homeNote.hidden = false;
        return;
    }
    buildButton.disabled = busy;
    const ticket = ++checked;
    try {
        const n = await nearest(h);
        if (ticket === checked && n) near.textContent = n.nm < 1 ? `In ${n.label}` : `Near ${n.label} (${Math.round(n.nm)} nm)`;
    } catch { /* the search index is a nicety; building works without it */ }
}

function setHome(a, b) {
    lat.value = a;
    lon.value = b;
    update();
}

for (const input of [lat, lon]) input.addEventListener("input", update);

// ---------------------------------------------------------------- search

function matches(text, { airports, towns }) {
    const s = text.trim().toLowerCase();
    if (s.length < 2) return [];
    const hits = [], seen = new Set();
    const add = (key, label, detail, a, b) => {
        if (hits.length < 8 && !seen.has(key)) seen.add(key), hits.push({ label, detail, a, b });
    };
    const airport = (x) => add(`a${x[0]}`, `${x[0]} · ${x[1]}`, [x[2], x[3]].filter(Boolean).join(", "), x[4], x[5]);
    const town = (t) => add(`t${t[0]}${t[3]}${t[4]}`, townLabel(t), "town", t[3], t[4]);
    for (const x of airports) if (x[0].toLowerCase() === s) airport(x);
    for (const x of airports) if (x[0].toLowerCase().startsWith(s)) airport(x);
    for (const t of towns) if (t[0].toLowerCase().startsWith(s)) town(t);
    for (const x of airports) if (x[1].toLowerCase().includes(s)) airport(x);
    return hits;
}

function showResults(hits) {
    results.replaceChildren(...hits.map((h) => {
        const b = Object.assign(document.createElement("button"), { type: "button" });
        b.append(h.label, Object.assign(document.createElement("small"), { textContent: h.detail }));
        b.addEventListener("click", () => {
            q.value = h.label;
            results.hidden = true;
            setHome(h.a, h.b);
        });
        const li = document.createElement("li");
        li.append(b);
        return li;
    }));
    results.hidden = hits.length === 0;
}

q.addEventListener("focus", () => loadSearch().catch(() => {}));
q.addEventListener("input", async () => {
    const text = q.value;
    try {
        const index = await loadSearch();
        if (q.value === text) showResults(matches(text, index));
    } catch {
        results.replaceChildren(Object.assign(document.createElement("li"), {
            className: "muted", textContent: "Search isn't available; type the coordinates instead." }));
        results.hidden = false;
    }
});
q.addEventListener("keydown", (e) => {
    if (e.key === "Enter") results.querySelector("button")?.click();
    if (e.key === "Escape") results.hidden = true;
});

// ---------------------------------------------------------------- location

$("locate").addEventListener("click", () => {
    if (!navigator.geolocation) return locationUnavailable();
    navigator.geolocation.getCurrentPosition(
        (p) => setHome(p.coords.latitude.toFixed(3), p.coords.longitude.toFixed(3)),
        locationUnavailable, { timeout: 15000 });
});

function locationUnavailable() {
    homeNote.textContent = "Location isn't available; search or type coordinates instead.";
    homeNote.hidden = false;
}

// ---------------------------------------------------------------- build

function show(fraction, text) {
    bar.value = fraction;
    step.textContent = text;
}

function onMessage({ data: m }) {
    if (m.step === "python") show(0.03, "Loading Python (about 8 MB the first time)…");
    else if (m.step === "data") show(0.05 + 0.25 * (m.total ? m.got / m.total : 1), `Downloading map data (${mb(m.got)} of ${mb(m.total)})…`);
    else if (m.step === "build" && m.level < LEVELS) show(0.3 + 0.65 * m.level / LEVELS, `Building level ${m.level + 1} of ${LEVELS}…`);
    else if (m.step === "build") show(0.97, "Making the zip…");
    else if (m.done) finished(m);
    else if (m.error) stopped(m);
}

function finished({ zip, summary: s }) {
    busy = false;
    working.hidden = true;
    buildButton.disabled = !home();
    buildButton.textContent = "Build the SD card";
    if (zipUrl) URL.revokeObjectURL(zipUrl);
    zipUrl = URL.createObjectURL(new Blob([zip], { type: "application/zip" }));
    download.href = zipUrl;
    download.textContent = `Download contrail-sd-card.zip (${mb(zip.byteLength)})`;
    summary.textContent = `map.pak: ${s.towns.toLocaleString()} towns and ${s.airports.toLocaleString()} airports within 250 nm. ` +
        (s.logos ? "logos.pak: included." : "logos.pak: not available yet (no release); the card works without it.");
    result.hidden = false;
}

function stopped({ error, what }) {
    busy = false;
    working.hidden = true;
    buildButton.disabled = !home();
    buildButton.textContent = "Try again";
    failed.replaceChildren();
    if (error === "load") {
        failed.textContent = `Couldn't download ${what}. Check the connection and try again.`;
    } else {
        failed.append(`The build failed: ${what}. `, Object.assign(document.createElement("a"), {
            href: "https://github.com/ChristopherJacob/contrail/issues", textContent: "Report it on GitHub" }), ".");
    }
    failed.hidden = false;
}

buildButton.addEventListener("click", () => {
    const h = home();
    if (!h || busy) return;
    busy = true;
    buildButton.disabled = true;
    result.hidden = failed.hidden = true;
    working.hidden = false;
    show(0, "Starting…");
    if (!worker) {
        worker = new Worker("sd-worker.js");
        worker.onmessage = onMessage;
        worker.onerror = () => { worker = null; stopped({ error: "load", what: "the builder" }); };
    }
    worker.postMessage({ lat: h[0], lon: h[1] });
});

// ---------------------------------------------------------------- ?lat=&lon= (the board's web page links here)

const params = new URLSearchParams(location.search);
if (params.has("lat") && params.has("lon")) setHome(params.get("lat"), params.get("lon"));
```

Keep the `?lat=` handling last, after every listener is in place.

- [ ] **Step 4: Styles**

In `site/style.css`, before `/* Footer */`:

```css
/* SD card */
.panel h3 { margin-bottom: 14px; }
.field { display: grid; gap: 6px; color: var(--soft); font-size: 15px; }
.input {
    font: inherit; color: var(--text); background: var(--bg-2); border: 1.5px solid var(--edge); border-radius: 10px;
    padding: 10px 12px; width: 100%; margin-top: 6px;
}
.input:focus { border-color: var(--accent); outline: none; }
.row { display: grid; grid-template-columns: 1fr 1fr auto; gap: 12px; align-items: end; margin-top: 16px; }
.results { list-style: none; margin: 6px 0 0; padding: 4px; background: var(--card-2); border: 1px solid var(--edge); border-radius: 10px; }
.results li.muted { padding: 8px 10px; }
.results button {
    display: block; width: 100%; text-align: left; font: inherit; color: var(--text); background: none; border: 0;
    padding: 8px 10px; border-radius: 8px; cursor: pointer;
}
.results button:hover, .results button:focus-visible { background: rgba(255, 210, 154, .12); }
.results small { color: var(--dim); margin-left: 8px; }
.near { margin: 12px 0 0; min-height: 1.55em; }
#home-note { margin: 12px 0 0; }
progress { display: block; width: 100%; height: 8px; accent-color: var(--accent); margin: 16px 0 8px; }
#working .status { margin: 0; }
.summary { margin: 12px 0 0; }
#failed { margin: 16px 0 0; }
```

At the very end of the file:

```css
@media (max-width: 760px) {
    .row { grid-template-columns: 1fr 1fr; }
    .row .btn { grid-column: 1 / -1; }
}
```

- [ ] **Step 5: Check the page locally in the browser pane**

```bash
cd site && python3 -m http.server 8765
```
(run in the background), then in the browser pane at `http://localhost:8765/sd.html`:
1. Type `KFDK`: the first result is `KFDK · Frederick Municipal Airport`; click it: the fields fill and the line
   says `Near Frederick, MD, US (…)` or `In Frederick, MD, US`.
2. `sd.html?lat=39.4176&lon=-77.3743`: fields filled on load.
3. Latitude `84`: the range message, Build disabled. `abc`: the format message.
4. Build for KFDK: the steps run through to the Download button; note the time. (Without `site/firmware/logos.pak`
   the summary says logos aren't available: also check that, then copy a `logos.pak` into `site/firmware/` from
   the latest release with `gh release download v0.2.0 -R ChristopherJacob/contrail -p logos.pak -D site/firmware`
   and build again.)
5. The zip: in the page's console, `const z = new Uint8Array(await (await fetch(document.getElementById("download").href)).arrayBuffer()); [z.length, String.fromCharCode(z[0], z[1])]`
   gives the button's size and `"PK"`. Run `python3 -I tools/sd_card.py --regions site/mapdata --lat 39.4176
   --lon -77.3743 --logos site/firmware/logos.pak --out $S/cli.zip`: its towns and airports counts and its zip
   size (within a few bytes: timestamps) match the page's. Byte parity of `map.pak` in Pyodide is the Node test's
   job (Task 3).
6. Phone width (`resize_window` preset mobile): no sideways scrolling; reset to desktop after.
Stop the server afterwards.

- [ ] **Step 6: Commit**

```bash
git add site/sd.html site/sd.js site/sd-worker.js site/style.css
git commit -m "Site: the SD card page, building contrail-sd-card.zip in the browser with Pyodide

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Links (site, README, the board's web page)

**Files:**
- Modify: `site/index.html`, `site/install.html`, `README.md`, `firmware/web/index.html`

- [ ] **Step 1: Site nav and text**

In the `<nav>` of `site/index.html` and `site/install.html`, after the Install link:
```html
      <a href="sd.html">SD card</a>
```
In `site/index.html`, replace the microSD paragraph's end so it reads:
```html
      <p>A <b>microSD card</b> is optional. With one, Contrail shows real airline logos instead of drawn tail fins,
        and a detailed Live Map with roads, rivers, towns and every airfield around you.
        <a href="sd.html">Build the card in your browser</a>.</p>
```
In `site/install.html`, replace step 5 ("Optional: an SD card.") with:
```html
        <li><b>Optional: an SD card.</b> For real airline logos and a detailed Live Map,
          <a href="sd.html">build the SD card</a> for your home in the browser and copy it onto a FAT32 microSD card.</li>
```
(`install.js` doesn't use the old `logos-link` element, so nothing else changes.)

- [ ] **Step 2: README**

In `README.md`'s "Detailed map" section, before "The pack is built for one home…", add:

```markdown
The easiest way to get it is the [SD card page](https://christopherjacob.github.io/contrail/sd.html): pick home,
and it builds `contrail-sd-card.zip` (`map.pak` and `logos.pak`) in the browser. Or build the pack yourself:
```
and keep the existing command-line text after it. In the "SD card and logos" section, add after its first
paragraph: `` The [SD card page](https://christopherjacob.github.io/contrail/sd.html) includes `logos.pak`. ``

- [ ] **Step 3: The board's web page**

In `firmware/web/index.html`, near the other constants (after the `HIDDEN` line):
```js
const SD_PAGE = "https://christopherjacob.github.io/contrail/sd.html";  // the site's SD card builder (6c)
```
In `locationCard()`, just before `return sec;`:
```js
  if (saved.loc_set)
    sec.append(el("p", {}, el("a", {href: `${SD_PAGE}?lat=${saved.lat}&lon=${saved.lon}`, target: "_blank", rel: "noopener"},
      "Build an SD card for this location")));
```

- [ ] **Step 4: Build and test the firmware**

```bash
cd firmware && python scripts/web_embed.py && pio test -e test && pio run -e cyd2usb && cd ..
```
Expected: all tests pass, the build succeeds.

- [ ] **Step 5: Check the link's format**

The board serves this page, so it can't be opened here. Confirm by reading that the link is
`…/sd.html?lat=<number>&lon=<number>`, the form `sd.js` reads at the bottom, and that it appears only when
`saved.loc_set` is true. Clicking it on the board is part of the developer's hardware pass (Task 7).

- [ ] **Step 6: Commit**

```bash
git add site/index.html site/install.html README.md firmware/web/index.html
git commit -m "Links to the SD card page: site nav, Install, showcase, README, and the board's location card

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: As built, full check

**Files:**
- Modify: `docs/superpowers/specs/2026-11-06-contrail-pack-builder-design.md`, `docs/HARDWARE_TESTING.md`

- [ ] **Step 1: Run everything**

```bash
python3 -I tools/build_map.py --selftest
python3 -I tools/map_regions.py --selftest
node tools/sd_node_test.mjs $S/node/node_modules/pyodide/pyodide.mjs $S/mapdata $S/before-kfdk.pak
cd firmware && pio test -e test && cd ..
```

- [ ] **Step 2: Spec §9 "As built"**

Change the spec's Status line to "Built on `pack-builder`; the board check waits for the developer." and add a
`## 9. As built` section recording: the measured region data size and per-home download, the Pyodide build time
(Node and browser), anything that differs from §1-§5 (file names, the records/`assemble` split), and anything left
for later.

- [ ] **Step 3: Hardware testing entry**

Add to `docs/HARDWARE_TESTING.md` a section "SD card from the site (6c)" with the steps: build the zip on the
site for the board's home, unzip it to the card, restart, check the Firmware card says the detailed map and the
logo count, and that the Live Map shows it; plus the board page's "Build an SD card for this location" link
opening the page with home filled in (needs a firmware build with Task 6). Mark both "not yet done".

- [ ] **Step 4: Commit**

```bash
git add docs/superpowers/specs/2026-11-06-contrail-pack-builder-design.md docs/HARDWARE_TESTING.md
git commit -m "6c: as built, and the board checks

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```
