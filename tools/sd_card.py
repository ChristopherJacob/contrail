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
    """[(path, size in bytes)] under regions that a build for this home reads (index.json must be there already)."""
    with open(os.path.join(regions, "index.json"), encoding="utf-8") as f:
        return build_map.region_files(json.load(f), home_box(lat, lon))


ZIP_TIME = (2020, 1, 1, 0, 0, 0)  # fixed, so the zip doesn't depend on the clock and is reproducible


def make(regions, lat, lon, logos, out, progress=None):
    """Writes the zip; progress(level) before each map level, then progress(len(LEVELS)) before zipping.
    A home out of range raises build_map.RangeError (the page shows its message); anything else is a build failure."""
    if logos and not os.path.isfile(logos):
        raise FileNotFoundError(f"{logos}: not found")
    box = home_box(lat, lon)
    src = build_map.load_regions(regions, box)
    pak = out + ".map.pak"
    try:
        _, size = build_map.write_pack(pak, lat, lon, build_map.COVER, src, progress)
        if progress:
            progress(len(build_map.LEVELS))
        try:
            with zipfile.ZipFile(out, "w", zipfile.ZIP_STORED) as z:  # the packs barely compress
                for arcname, path in (("contrail/map.pak", pak), ("contrail/logos.pak", logos)):
                    if not path:
                        continue
                    info = zipfile.ZipInfo(arcname, date_time=ZIP_TIME)
                    info.compress_type = zipfile.ZIP_STORED
                    info.external_attr = 0o644 << 16
                    with open(path, "rb") as f:
                        z.writestr(info, f.read())
        except BaseException:
            if os.path.exists(out):
                os.remove(out)
            raise
    finally:
        if os.path.exists(pak):
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
    except (ValueError, OSError) as e:
        sys.exit(f"error: {e}")
    print(f"{a.out}: map.pak {s['map_bytes'] / 1e6:.2f} MB ({s['towns']} towns, {s['airports']} airports)"
          + (", logos.pak" if s["logos"] else ""))


if __name__ == "__main__":
    main()
