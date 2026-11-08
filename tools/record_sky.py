#!/usr/bin/env python3
"""Recorded flights for the browser simulator (6b spec §4).

    tools/record_sky.py <places.json> <out-dir> [--replies N]   record every place, in parallel
    tools/record_sky.py --selftest

For each place, <out-dir>/<id>/ holds 000.json, 001.json ...: adsb.lol's replies, one every 20 s for its default
25 nm radius, unchanged. routes.json maps each callsign the board would look
up to adsbdb's reply (null when adsbdb doesn't know it), and index.json says what was recorded and when. A place
whose recording fails leaves no folder; the exit status is 1 only when no place was recorded.
"""
import argparse
import json
import math
import os
import shutil
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.parse
import urllib.request

REPLIES = 30      # 10 minutes
# adsb.lol allows about 20 requests a minute per IP (measured 2026-11-08: a burst of 10, then 429s), and five places
# polled every 10 s is 30. At 20 s it is 15; the simulator polls at index.json's interval (kPollComm is a setting).
INTERVAL_S = 20
MAX_RETRY_AFTER_S = 60
RADIUS_NM = 25    # CoreSettings.cpp: kRadius's default
NEAREST = 12      # the board looks routes up for its 8 nearest (CoreApp.h kEnrichCount); a margin for movement
FEED = "https://api.adsb.lol/v2/point/{lat:.4f}/{lon:.4f}/{radius}"
ROUTE = "https://api.adsbdb.com/v0/callsign/{cs}"
USER_AGENT = "Contrail-recorder (+https://github.com/ChristopherJacob/contrail)"


class Spacer:
    """At least `gap` seconds between requests to one host, across threads."""

    def __init__(self, gap, clock=time.monotonic, sleep=time.sleep):
        self.gap, self.clock, self.sleep = gap, clock, sleep
        self.lock = threading.Lock()
        self.last = -math.inf

    def wait(self):
        with self.lock:
            delay = self.last + self.gap - self.clock()
            if delay > 0:
                self.sleep(delay)
            self.last = self.clock()


def http_get(url, spacer, opener=urllib.request.urlopen, sleep=time.sleep):
    """(status, body); status -1 when the request could not be made. A 429 waits out its Retry-After (capped) and
    asks once more, so a place's reply isn't lost to a brief limit."""
    for attempt in range(2):
        spacer.wait()
        req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT, "Accept": "application/json"})
        try:
            with opener(req, timeout=20) as r:
                return r.status, r.read()
        except urllib.error.HTTPError as e:
            if e.code != 429 or attempt:
                return e.code, b""
            try:
                wait = float(e.headers.get("Retry-After", 10))
            except (TypeError, ValueError):
                wait = 10.0  # an HTTP-date or junk: a short guess
            sleep(min(max(wait, 0.0), MAX_RETRY_AFTER_S))
        except (urllib.error.URLError, OSError):
            return -1, b""


def callsign(ac):
    # Spaces only, case kept: ReadsbParser.cpp's copyTrimmed, so the key is what the board asks adsbdb for and
    # what ReplayHttp looks up.
    return (ac.get("flight") or "").strip(" ")


def nearest_callsigns(body, lat, lon, n=NEAREST):
    """The callsigns of the n flights nearest home in one feed reply, nearest first."""
    k = math.cos(math.radians(lat))
    seen = []
    for ac in json.loads(body).get("ac", []):
        cs = callsign(ac)
        if not cs or not isinstance(ac.get("lat"), (int, float)) or not isinstance(ac.get("lon"), (int, float)):
            continue
        seen.append((60 * math.hypot(ac["lat"] - lat, (ac["lon"] - lon) * k), cs))
    out = []
    for _, cs in sorted(seen):
        if cs not in out:
            out.append(cs)
    return out[:n]


class RecordError(Exception):
    pass


def record_place(place, out_dir, replies, get, feed, routes_spacer, clock=time.time, sleep=time.sleep):
    """Records one place into out_dir/<id> (built in <id>.tmp, then moved). Raises RecordError."""
    final = os.path.join(out_dir, place["id"])
    tmp = final + ".tmp"
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp)
    try:
        start = int(clock())
        url = FEED.format(lat=place["lat"], lon=place["lon"], radius=RADIUS_NM)
        wanted = []
        for k in range(replies):
            began = clock()
            status, body = get(url, feed)
            if status != 200:
                status, body = get(url, feed)  # one retry
            if status != 200:
                raise RecordError(f"{place['id']}: reply {k}: HTTP {status}")
            try:
                for cs in nearest_callsigns(body, place["lat"], place["lon"]):
                    if cs not in wanted:
                        wanted.append(cs)
            except (ValueError, AttributeError) as e:
                raise RecordError(f"{place['id']}: reply {k}: not a feed reply ({e})")
            with open(os.path.join(tmp, f"{k:03d}.json"), "wb") as f:
                f.write(body)
            if k + 1 < replies:
                sleep(max(0.0, INTERVAL_S - (clock() - began)))
        routes = {}
        for cs in wanted:  # after the replies, so the lookups don't delay them
            status, body = get(ROUTE.format(cs=urllib.parse.quote(cs)), routes_spacer)
            if status == 200:
                try:
                    routes[cs] = json.loads(body)
                except ValueError:
                    pass  # the board would get no route either
            elif status == 404:
                routes[cs] = None
        with open(os.path.join(tmp, "routes.json"), "w") as f:
            json.dump(routes, f, separators=(",", ":"), sort_keys=True)
        index = dict(place, start=start, interval=INTERVAL_S, replies=replies)
        with open(os.path.join(tmp, "index.json"), "w") as f:
            json.dump(index, f, indent=1)
        shutil.rmtree(final, ignore_errors=True)
        os.replace(tmp, final)
        return index
    except BaseException:
        shutil.rmtree(tmp, ignore_errors=True)
        raise


def record_all(places, out_dir, replies, get=http_get, sleep=time.sleep):
    """Every place at once, each in its own thread; returns the ids recorded."""
    os.makedirs(out_dir, exist_ok=True)
    feed, routes = Spacer(1.0, sleep=sleep), Spacer(0.5, sleep=sleep)
    done, threads = [], []

    def run(place):
        try:
            record_place(place, out_dir, replies, get, feed, routes, sleep=sleep)
            done.append(place["id"])
        except RecordError as e:
            print(f"warning: {e}", file=sys.stderr)

    for place in places:
        t = threading.Thread(target=run, args=(place,))
        t.start()
        threads.append(t)
    for t in threads:
        t.join()
    return [p["id"] for p in places if p["id"] in done]


def selftest():
    place = {"id": "kfdk", "name": "Frederick, Maryland", "airport": "KFDK", "lat": 39.4, "lon": -77.4,
             "zone": "America/New_York"}
    reply = json.dumps({"ac": [
        {"hex": "a", "flight": "UAL53   ", "lat": 39.5, "lon": -77.4},   # 6 nm
        {"hex": "b", "flight": "N1 ", "lat": 39.41, "lon": -77.4},       # nearest
        {"hex": "c", "flight": "", "lat": 39.4, "lon": -77.4},           # no callsign
        {"hex": "d", "flight": "DAL1", "lat": 41.0, "lon": -77.4},       # far
        {"hex": "e", "flight": "SWA2"},                                  # no position
    ]}).encode()
    assert nearest_callsigns(reply, 39.4, -77.4) == ["N1", "UAL53", "DAL1"], nearest_callsigns(reply, 39.4, -77.4)
    assert nearest_callsigns(reply, 39.4, -77.4, 1) == ["N1"]
    # The board keeps the case it is sent, so a lowercase callsign must stay lowercase to match ReplayHttp's key.
    assert callsign({"flight": " tst1a  "}) == "tst1a"

    calls = []
    t = [1000.0]
    clock = lambda: t[0]

    def sleep(s):
        t[0] += s

    def get(url, spacer):
        calls.append(url)
        if "adsb.lol" in url:
            return 200, reply
        if url.endswith("/UAL53"):
            return 200, b'{"response":{"flightroute":{"callsign":"UAL53"}}}'
        if url.endswith("/N1"):
            return 404, b""
        return 500, b""  # DAL1: adsbdb failing leaves the callsign out

    with tempfile.TemporaryDirectory() as d:
        sp = Spacer(0)
        index = record_place(place, d, 3, get, sp, sp, clock=clock, sleep=sleep)
        got = sorted(os.listdir(os.path.join(d, "kfdk")))
        assert got == ["000.json", "001.json", "002.json", "index.json", "routes.json"], got
        assert open(os.path.join(d, "kfdk", "001.json"), "rb").read() == reply
        routes = json.load(open(os.path.join(d, "kfdk", "routes.json")))
        assert routes == {"N1": None, "UAL53": {"response": {"flightroute": {"callsign": "UAL53"}}}}, routes
        assert index["start"] == 1000 and index["replies"] == 3 and index["interval"] == 20 and index["zone"]
        assert calls[0] == "https://api.adsb.lol/v2/point/39.4000/-77.4000/25", calls[0]
        assert sum("adsb.lol" in c for c in calls) == 3  # each callsign looked up once, after the replies
        assert calls[3:] == ["https://api.adsbdb.com/v0/callsign/N1", "https://api.adsbdb.com/v0/callsign/UAL53",
                             "https://api.adsbdb.com/v0/callsign/DAL1"], calls[3:]
        assert t[0] == 1040.0, t[0]  # two 20 s gaps, none after the last reply

        def failing(url, spacer):
            return (503, b"") if url.endswith("/25") and failing.n >= 1 else (200, reply)
        failing.n = 0

        def counting(url, spacer):
            r = failing(url, spacer)
            failing.n += "adsb.lol" in url
            return r
        try:
            record_place(dict(place, id="eham"), d, 3, counting, sp, sp, clock=clock, sleep=sleep)
            raise AssertionError("a failing feed must raise")
        except RecordError:
            pass
        assert sorted(os.listdir(d)) == ["kfdk"], os.listdir(d)  # no eham, no eham.tmp

        assert record_all([place, dict(place, id="egll")], d, 2, get=get, sleep=lambda s: None) == ["kfdk", "egll"]

    # 429: Retry-After is waited out (capped) and the request made once more; a second 429 is returned.
    class Resp:
        status = 200
        def __enter__(self): return self
        def __exit__(self, *a): return False
        def read(self): return b"{}"

    def opener_for(seq):
        seq = list(seq)
        def opener(req, timeout):
            assert req.get_header("User-agent") == USER_AGENT
            x = seq.pop(0)
            if x == "ok":
                return Resp()
            raise urllib.error.HTTPError(req.full_url, 429, "", {"Retry-After": x}, None)
        return opener
    waits = []
    assert http_get("https://x.test/", Spacer(0), opener_for(["7", "ok"]), waits.append) == (200, b"{}") and waits == [7.0], waits
    waits.clear()
    assert http_get("https://x.test/", Spacer(0), opener_for(["500", "9"]), waits.append) == (429, b"") and waits == [60.0], waits
    waits.clear()
    assert http_get("https://x.test/", Spacer(0), opener_for(["soon", "ok"]), waits.append) == (200, b"{}") and waits == [10.0]
    print("record_sky selftest: ok")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("places", nargs="?")
    ap.add_argument("out", nargs="?")
    ap.add_argument("--replies", type=int, default=REPLIES, help="replies per place (INTERVAL_S apart)")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not a.places or not a.out:
        ap.error("places and out are required")
    with open(a.places) as f:
        places = json.load(f)
    done = record_all(places, a.out, a.replies)
    print(f"recorded {len(done)} of {len(places)} places: {' '.join(done) or 'none'}")
    sys.exit(0 if done else 1)


if __name__ == "__main__":
    main()
