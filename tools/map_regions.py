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
