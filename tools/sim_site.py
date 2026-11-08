#!/usr/bin/env python3
"""The browser simulator's files on the site (6b spec §5), after tools/sd_site.sh (it reads <site>/mapdata):

    tools/sim_site.py <site> <recordings>

<site>/sim/ gets contrail.js and contrail.wasm (tools/build_wasm.sh), each recorded place's folder, its map
(map-<id>.pak, from the site's map regions) and places.json: the recorded places in tools/sim_places.json's order,
and whether the site has logos.pak (from the latest release). Places without a recording are left out; with none,
the page says no recording is available. Each place entry is its recording's index.json (id, name, lat, lon, zone,
start, interval, replies), so the page has everything sim_start takes.
"""
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    site, recordings = sys.argv[1:]
    out = os.path.join(site, "sim")
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    subprocess.run([os.path.join(HERE, "build_wasm.sh"), out], check=True)
    with open(os.path.join(HERE, "sim_places.json")) as f:
        order = [p["id"] for p in json.load(f)]
    places = []
    for pid in order:
        index = os.path.join(recordings, pid, "index.json")
        if not os.path.exists(index):
            print(f"{pid}: no recording", file=sys.stderr)
            continue
        with open(index) as f:
            place = json.load(f)
        shutil.copytree(os.path.join(recordings, pid), os.path.join(out, pid))
        subprocess.run([sys.executable, "-I", os.path.join(HERE, "build_map.py"), "--regions",
                        os.path.join(site, "mapdata"), "--lat", str(place["lat"]), "--lon", str(place["lon"]),
                        "--out", os.path.join(out, f"map-{pid}.pak")], check=True)
        places.append(place)
    # Without a release logos.pak the simulator draws the tail fins; the page reads this flag instead of probing.
    logos = os.path.exists(os.path.join(site, "firmware", "logos.pak"))
    with open(os.path.join(out, "places.json"), "w") as f:
        json.dump({"logos": logos, "places": places}, f, indent=1)
    print(f"sim: {len(places)} places, logos {'yes' if logos else 'no'}")


if __name__ == "__main__":
    main()
