#!/usr/bin/env python3
"""Download the airline logo set into assets/logos/<ICAO>.png (one-time; the result is committed).

Source: github.com/Jxck-S/airline-logos at a pinned commit: flightaware_logos/<ICAO>.png (90 x 90), with
radarbox_logos/<ICAO>.png for codes the FlightAware set lacks. Standard library only.

  python3 -I tools/fetch_logos.py --out assets/logos

Exit codes: 0 done, 1 a download failed (re-run: files already present are skipped).
"""
import argparse
import concurrent.futures
import http.client
import json
import os
import sys
import urllib.request

REPO, COMMIT = "Jxck-S/airline-logos", "e0b218991caef4832e0db911136173e8eead942a"
SETS = ["flightaware_logos", "radarbox_logos"]  # first wins
PNG_MAGIC = b"\x89PNG\r\n\x1a\n"


def tree():
    url = f"https://api.github.com/repos/{REPO}/git/trees/{COMMIT}?recursive=1"
    with urllib.request.urlopen(url, timeout=60) as r:
        t = json.load(r)
    if t.get("truncated"):
        raise SystemExit("the GitHub tree listing came back truncated")
    return [e["path"] for e in t["tree"] if e["type"] == "blob"]


def fetch(out, code, path):
    """Downloads one logo; an error message, or None."""
    dest = os.path.join(out, code + ".png")
    url = f"https://raw.githubusercontent.com/{REPO}/{COMMIT}/{path}"
    try:
        with urllib.request.urlopen(url, timeout=30) as r:
            data = r.read()
    except (OSError, http.client.HTTPException) as e:  # IncompleteRead etc. are not OSErrors
        return f"{code}: {e}"
    if not data.startswith(PNG_MAGIC):
        return f"{code}: not a PNG"
    # Write then rename, so an interrupted run never leaves a partial file that a re-run would skip.
    with open(dest + ".part", "wb") as f:
        f.write(data)
    os.replace(dest + ".part", dest)
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    chosen = {}
    paths = tree()
    for s in reversed(SETS):  # later sets first, so the first set overwrites them
        for p in paths:
            folder, _, name = p.partition("/")
            code, ext = os.path.splitext(name)
            if folder == s and ext.lower() == ".png" and 3 <= len(code) <= 4 and code.isascii() and code.isalnum():
                chosen[code.upper()] = p
    todo = [(c, p) for c, p in sorted(chosen.items()) if not os.path.exists(os.path.join(a.out, c + ".png"))]
    # A few downloads at once: one at a time takes most of an hour, and a stalled connection blocks the rest.
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
        errors = [e for e in pool.map(lambda cp: fetch(a.out, *cp), todo) if e]
    for e in errors:
        print(e)
    print(f"{len(chosen)} logos, {len(errors)} failed")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
