#!/usr/bin/env python3
"""Recorded flights for the browser simulator (6b spec §4).

    tools/record_sky.py <places.json> <out-dir> [--replies N]   record every place, in parallel
    tools/record_sky.py --selftest

For each place, <out-dir>/<id>/ holds 000.json, 001.json ...: adsb.lol's replies, one every 30 s for its default
25 nm radius, unchanged. routes.json maps each callsign the board would look
up to adsbdb's reply (null when adsbdb doesn't know it), and index.json says what was recorded and when. A place
whose recording fails leaves no folder; the exit status is 1 only when no place was recorded.
"""
import argparse
import contextlib
import io
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

REPLIES = 20      # 10 minutes
# adsb.lol allows about 20 requests a minute per IP from here (measured 2026-11-08: a burst of 10, then 429s), and
# GitHub's runners get less: five places at 20 s (15 a minute) came back with 40-60 s gaps and overran the workflow's
# 15 minutes. At 30 s it is 10 a minute; the simulator polls at index.json's interval (kPollComm is a setting).
INTERVAL_S = 30
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

    def hold(self, seconds):
        """Nothing goes out for `seconds` from now: a host's rate limit applies to every thread's requests."""
        with self.lock:
            self.last = max(self.last, self.clock() + seconds - self.gap)


def http_get(url, spacer, opener=urllib.request.urlopen):
    """(status, body); status -1 when the request could not be made. A 429 holds the host's spacer for its
    Retry-After (capped), so every thread backs off together, and asks once more."""
    for attempt in range(2):
        spacer.wait()
        req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT, "Accept": "application/json"})
        try:
            with opener(req, timeout=20) as r:
                return r.status, r.read()
        except urllib.error.HTTPError as e:
            if e.code == 429:
                host = urllib.parse.urlsplit(url).hostname
                print(f"429 from {host}: Retry-After {e.headers.get('Retry-After')}", file=sys.stderr, flush=True)
            if e.code != 429 or attempt:
                return e.code, b""
            try:
                wait = float(e.headers.get("Retry-After", 10))
            except (TypeError, ValueError):
                wait = 10.0  # an HTTP-date or junk: a short guess
            spacer.hold(min(max(wait, 0.0), MAX_RETRY_AFTER_S))
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
        la, lo = ac.get("lat"), ac.get("lon")
        if la is None:  # ReadsbParser.cpp: an aircraft without a fix reports its last position
            last = ac.get("lastPosition") or {}
            la, lo = last.get("lat"), last.get("lon")
        if not cs or not isinstance(la, (int, float)) or not isinstance(lo, (int, float)):
            continue
        seen.append((60 * math.hypot(la - lat, (lo - lon) * k), cs))
    out = []
    for _, cs in sorted(seen):
        if cs not in out:
            out.append(cs)
    return out[:n]


class RecordError(Exception):
    pass


def record_place(place, out_dir, replies, get, feed, routes_spacer, clock=time.monotonic, sleep=time.sleep,
                 wall=time.time):
    """Records one place into out_dir/<id> (built in <id>.tmp, then moved). Raises RecordError. The cadence runs on
    `clock`: reply k is requested when due, INTERVAL_S after the one before, so a slow request doesn't push the rest.
    A reply that lands more than an interval late (a rate limit's wait) is rebased on: the later replies are again
    INTERVAL_S apart from it, and that one gap in the recording is longer. Lateness beyond MAX_RETRY_AFTER_S +
    INTERVAL_S fails the place."""
    final = os.path.join(out_dir, place["id"])
    tmp = final + ".tmp"
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp)
    try:
        url = FEED.format(lat=place["lat"], lon=place["lon"], radius=RADIUS_NM)
        wanted = []
        start, due = 0, 0.0
        for k in range(replies):
            if k:
                due += INTERVAL_S
                sleep(max(0.0, due - clock()))
            status, body = get(url, feed)
            if status == -1 or status >= 500:
                status, body = get(url, feed)  # one retry; a 429 was already retried by http_get
            if status != 200:
                raise RecordError(f"{place['id']}: reply {k}: HTTP {status}")
            now = clock()
            if k == 0:
                start, due = int(wall()), now
            elif now - due > MAX_RETRY_AFTER_S + INTERVAL_S:
                raise RecordError(f"{place['id']}: reply {k}: {now - due:.0f} s late")
            elif now - due > INTERVAL_S:
                due = now  # a long gap in the recording, then the cadence again, not a rush to catch up
            try:
                for cs in nearest_callsigns(body, place["lat"], place["lon"]):
                    if cs not in wanted:
                        wanted.append(cs)
            except (ValueError, AttributeError, TypeError) as e:
                raise RecordError(f"{place['id']}: reply {k}: not a feed reply ({e})")
            with open(os.path.join(tmp, f"{k:03d}.json"), "wb") as f:
                f.write(body)
        routes = {}
        for cs in wanted:  # after the replies, so the lookups don't delay them
            status, body = get(ROUTE.format(cs=urllib.parse.quote(cs, safe="")), routes_spacer)
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
        shutil.rmtree(final, ignore_errors=True)  # an earlier run's recording must not pass for this one's
        raise


def record_all(places, out_dir, replies, get=http_get, sleep=time.sleep):
    """Every place at once, each in its own thread; returns the ids recorded."""
    os.makedirs(out_dir, exist_ok=True)
    # One feed request every INTERVAL_S / places, not a burst per interval: measured at seven replies, a burst left
    # gaps of 11-49 s and this left most near 30 s (adsb.lol still refuses a few, which a recording has to survive).
    feed, routes = Spacer(INTERVAL_S / max(len(places), 1), sleep=sleep), Spacer(0.5, sleep=sleep)
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
        calls.append((url, t[0]))
        t[0] += 3  # a request takes time, so the cadence has to be scheduled, not slept
        if "adsb.lol" in url:
            return 200, reply
        if url.endswith("/UAL53"):
            return 200, b'{"response":{"flightroute":{"callsign":"UAL53"}}}'
        if url.endswith("/N1"):
            return 404, b""
        return 500, b""  # DAL1: a failed adsbdb lookup is left out, and replays as unknown (404)

    with tempfile.TemporaryDirectory() as d:
        sp = Spacer(0)
        index = record_place(place, d, 3, get, sp, sp, clock=clock, sleep=sleep, wall=clock)
        got = sorted(os.listdir(os.path.join(d, "kfdk")))
        assert got == ["000.json", "001.json", "002.json", "index.json", "routes.json"], got
        assert open(os.path.join(d, "kfdk", "001.json"), "rb").read() == reply
        routes = json.load(open(os.path.join(d, "kfdk", "routes.json")))
        assert routes == {"N1": None, "UAL53": {"response": {"flightroute": {"callsign": "UAL53"}}}}, routes
        # start is taken when reply 0 is in, not before the request
        assert index["start"] == 1003 and index["replies"] == 3 and index["interval"] == 30 and index["zone"]
        assert calls[0][0] == "https://api.adsb.lol/v2/point/39.4000/-77.4000/25", calls[0]
        assert sum("adsb.lol" in c for c, _ in calls) == 3
        # each callsign is looked up once, after the replies; a failed lookup (DAL1) is left out of routes
        assert [c for c, _ in calls[3:]] == ["https://api.adsbdb.com/v0/callsign/N1",
                                             "https://api.adsbdb.com/v0/callsign/UAL53",
                                             "https://api.adsbdb.com/v0/callsign/DAL1"], calls[3:]
        # reply k is requested at t0 + 30 k, t0 being when reply 0 came in, whatever each request took
        assert [at for _, at in calls[:3]] == [1000.0, 1033.0, 1063.0], calls[:3]

        # a failing feed (5xx, retried once) raises and leaves no folder, not even an earlier run's
        os.makedirs(os.path.join(d, "eham"))
        n = [0]

        def failing(url, spacer):
            n[0] += 1
            return (503, b"") if n[0] > 1 else (200, reply)
        try:
            record_place(dict(place, id="eham"), d, 3, failing, sp, sp, clock=clock, sleep=sleep, wall=clock)
            raise AssertionError("a failing feed must raise")
        except RecordError:
            pass
        assert n[0] == 3  # reply 1 and its one retry
        assert sorted(os.listdir(d)) == ["kfdk"], os.listdir(d)  # no eham, no eham.tmp

        # a 429 reaching record_place was already retried by http_get: asked once per reply, not again
        n[0] = 0

        def limited(url, spacer):
            n[0] += 1
            return 429, b""
        try:
            record_place(place, d, 3, limited, sp, sp, clock=clock, sleep=sleep, wall=clock)
            raise AssertionError("a limited feed must raise")
        except RecordError:
            pass
        assert n[0] == 1, n[0]
        assert sorted(os.listdir(d)) == [], os.listdir(d)  # the failed place's earlier folder is gone too

        # a 429 with Retry-After 30 (as the stub: the hold and the retry make reply 1 take 33 s) stretches that gap
        # once, then the cadence is 30 s again from that reply
        times = []

        def limited_once(url, spacer):
            times.append(t[0])
            t[0] += 33 if len(times) == 2 else 3
            return 200, reply
        record_place(place, d, 4, limited_once, sp, sp, clock=clock, sleep=sleep, wall=clock)
        gaps = [b - a for a, b in zip(times, times[1:4])]
        assert gaps == [33.0, 63.0, 30.0], gaps

        # lateness beyond MAX_RETRY_AFTER_S + INTERVAL_S fails the place
        def late(url, spacer):
            late.n += 1
            t[0] += 95 if late.n == 2 else 0
            return 200, reply
        late.n = 0
        try:
            record_place(place, d, 3, late, sp, sp, clock=clock, sleep=sleep, wall=clock)
            raise AssertionError("a late reply must raise")
        except RecordError as e:
            assert "late" in str(e), e

        # not feed replies: "ac" null, a body that isn't an object
        for bad in (b'{"ac":null}', b"[]", b"not json"):
            try:
                record_place(place, d, 1, lambda u, s_, bad=bad: (200, bad), sp, sp, clock=clock, sleep=sleep)
                raise AssertionError(bad)
            except RecordError:
                pass

        assert record_all([place, dict(place, id="egll")], d, 2, get=get, sleep=lambda s: None) == ["kfdk", "egll"]

        # the feed's requests are spread over the interval, whatever the number of places
        feed_gaps = set()

        def spaced(url, spacer):
            if "adsb.lol" in url:
                feed_gaps.add(spacer.gap)
            return (404, b"") if "adsbdb" in url else (200, reply)
        record_all([place, dict(place, id="egll")], d, 1, get=spaced, sleep=lambda s: None)
        assert feed_gaps == {INTERVAL_S / 2}, feed_gaps

    # a flight without a fix reports lastPosition (ReadsbParser.cpp)
    lp = json.dumps({"ac": [{"flight": "LP1", "lastPosition": {"lat": 39.4, "lon": -77.4}},
                            {"flight": "NOPOS", "lastPosition": {}}]}).encode()
    assert nearest_callsigns(lp, 39.4, -77.4) == ["LP1"]

    # Spacer: gap between requests, and hold() delays the next one for every thread
    ft = [0.0]
    slept = []

    def fsleep(s):
        slept.append(s)
        ft[0] += s
    spc = Spacer(1.0, clock=lambda: ft[0], sleep=fsleep)
    spc.wait()
    spc.wait()
    assert slept == [1.0], slept
    slept.clear()
    spc.hold(30)
    spc.wait()
    assert slept == [30.0], slept

    # 429: the spacer is held for Retry-After (capped) and the request made once more; a second 429 is returned.
    class Resp:
        status = 200

        def __enter__(self):
            return self

        def __exit__(self, *a):
            return False

        def read(self):
            return b"{}"

    def opener_for(seq):
        seq = list(seq)

        def opener(req, timeout):
            assert req.get_header("User-agent") == USER_AGENT
            x = seq.pop(0)
            if x == "ok":
                return Resp()
            raise urllib.error.HTTPError(req.full_url, 429, "", {"Retry-After": x}, None)
        return opener

    def held(seq):
        ft[0] = 0.0
        slept.clear()
        spc = Spacer(1.0, clock=lambda: ft[0], sleep=fsleep)
        return http_get("https://x.test/", spc, opener_for(seq)), sum(slept)
    log = io.StringIO()
    with contextlib.redirect_stderr(log):
        assert held(["7", "ok"]) == ((200, b"{}"), 7.0)
    assert log.getvalue() == "429 from x.test: Retry-After 7\n", log.getvalue()  # each refusal shows in the run's log
    log = io.StringIO()
    with contextlib.redirect_stderr(log):
        assert held(["500", "9"]) == ((429, b""), 60.0)
        assert held(["soon", "ok"]) == ((200, b"{}"), 10.0)
    assert log.getvalue().count("429 from x.test") == 3, log.getvalue()
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
