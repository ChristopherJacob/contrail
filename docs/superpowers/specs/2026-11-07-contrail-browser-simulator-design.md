# Contrail Sub-project 6b — Browser Simulator

Date: 2026-11-07
Status: Built on `simulator`; checked in the browser.
Parent: `2026-10-30-contrail-requirements.md` ("Browser simulator: the real renderer compiled to WebAssembly, fed
live flights"). Builds on the desktop simulator (`firmware/src/main_sim.cpp`, `hal_native`), 6a (the site, the
Pages workflow, the screenshots) and 6c (`site/mapdata/`, `build_map.py --regions`, `logos.pak` on the site).
6d (animated previews) is separate.

## Goal

A visitor can try Contrail without a board: a page of the site runs the firmware itself, compiled to WebAssembly,
on recorded flights around a few real places, and they tap, swipe and switch screens and themes as on the device.

**Why recorded flights:** no flight feed lets a web page on another site read its replies (checked 2026-11-06 with
an `Origin` header: adsb.lol, adsb.fi and airplanes.live send no `Access-Control-Allow-Origin`; OpenSky allows only
its own site; adsbdb allows any). A proxy would make the flights live but is a public service to run; it can come
later behind the same page (out of scope here). A board on the visitor's network can't be reached either: an https
page may not load plain http.

Out of scope: live flights (a proxy), the visitor's own home or receiver, saving settings between visits, the
board's web settings page, Wi-Fi setup, OTA.

**Done when:**
1. The site has a **Simulator** page (`sim.html`, in the nav) running the firmware's `CoreApp` and `ScreenApp` in
   the browser, on a canvas showing the 240×320 screen at 2×.
2. It plays recorded flights for five places (Frederick KFDK, New York JFK, London Heathrow, Amsterdam Schiphol,
   San Francisco SFO), refreshed hourly by the Pages workflow, with their routes, logos and detailed map.
3. Clicks and drags stand in for touch: taps, swipes between screens and the quick-settings panel work.
4. The page fetches only files of this site: no request carries anything about the visitor.
5. CI proves the WebAssembly build renders the demo scenes pixel-identical to the desktop simulator.
6. The showcase and the README point to the page.

## 1. Pieces

| Piece | Where | What |
|---|---|---|
| WASM entry point | `firmware/src/main_wasm.cpp` | the app loop for the worker; `--render`-style demo frames for the test |
| WASM HAL | `firmware/lib/hal_wasm/` | display, touch, clock, HTTP and storage over the worker's JS |
| Build | `tools/build_wasm.sh` | emscripten build to `contrail.js` + `contrail.wasm` (PlatformIO's native platform uses the host compiler, so not an env) |
| Recorder | `tools/record_sky.py` | 10 min of adsb.lol replies and their adsbdb routes per place |
| Places | `tools/sim_places.json` | name, ICAO, lat, lon, time zone (IANA name) |
| Page | `site/sim.html`, `site/sim.js`, `site/sim-worker.js` | canvas, controls, the worker |
| Site data | `tools/sim_site.py` | recordings, per-place `map.pak`, the WASM build into `site/sim/` |
| Parity test | `tools/sim_node_test.mjs`, CI | WASM demo frames under Node equal the desktop `--render` frames |
| Pages workflow | `.github/workflows/pages.yml` | hourly schedule, emscripten, the recorder, `sim_site.py` |

## 2. Running in the browser

The firmware makes blocking requests (`IHttp::get` returns when the body has been handled) and the desktop
simulator runs `networkTick` on its own thread. In the browser:

- **Everything runs in one Web Worker** (`sim-worker.js` loads `contrail.js`). In a worker, synchronous
  `XMLHttpRequest` is allowed, so `IHttp` and `IFile::readAt` block as on the device. Every request is for a file
  of this site, so the waits are short.
- **One loop, no threads:** the worker calls the loop with `setTimeout`/`requestAnimationFrame`-like pacing at
  30 fps; each step runs `networkTick` when 100 ms have passed (as the desktop thread), then `renderTick`,
  `ScreenApp::tick`, touch, setting edits and `StripRenderer::frame`, as `main_sim.cpp`'s loop does.
- Rejected: Asyncify on the main page (bigger, slower code, hard to debug); pthreads (need SharedArrayBuffer, so
  COOP/COEP headers, which GitHub Pages cannot send).
- **Build:** emscripten (pinned version, `emsdk` in CI), `-O2`, `-ffp-contract=off` as the `sim` and `test` envs,
  ArduinoJson as there, no SDL, no curl. `MODULARIZE` so the worker controls start-up. Size budget: under 2 MB of
  WASM before compression.

## 3. The WASM HAL (`hal_wasm`)

| Interface | Browser side |
|---|---|
| `IDisplay` | `pushRect` copies strips into a 240×320 RGB565 frame; `endFrame` posts it to the page (transferred, not copied) when anything changed |
| `ITouch` | the page posts pointer down/move/up in screen pixels; `read` returns the latest |
| `IClock` | `millis` from `performance.now()`; `epochSeconds` = the recording's start + time since the place was loaded, so the sky matches the place's time of day |
| `IHttp` | synchronous XHR. An adsb.lol URL (the community provider, `Providers.cpp`) is answered with the recording's reply for the elapsed time (reply = elapsed / interval; a repeat after an early poll has its `seen`/`seen_pos` aged by the time since that reply's slot, so planes do not jump back); an adsbdb URL (`Routes.cpp`) with the recorded route or a 404. Any other URL: transport error. The real parsers read the real reply bodies |
| `IStorage` / `IFile` | `/contrail/logos.pak` and `/contrail/map.pak` read by HTTP `Range` requests (`readAt` → `bytes=o-(o+n-1)`), so only the bytes drawn are downloaded; `writeAt` goes to an in-memory overlay (the map's saved chart lasts one loop of the recording; the reload starts a fresh card) and `writable` is true, because `MapPack` needs it |
| `IKeyValueStore` | in memory; the worker sets home, the time zone and the theme before `begin` |
| LED, backlight, light sensor | the LED colour is posted to the page (a dot beside the screen); the others are no-ops |

Emscripten's `localtime_r` takes the zone from JS `Date` (the visitor's) and ignores `TZ`, so the build replaces its
`_localtime_js` with one using `Intl.DateTimeFormat` in the place's IANA zone (`Module.timeZone`, set by the worker;
"UTC" for the parity test). Checked 2026-11-07: with it, all 14 site screenshots are byte-identical to the desktop's.

## 4. Recordings

`tools/record_sky.py <places.json> <out-dir>`:

- For each place, every 30 s for 10 minutes, GETs adsb.lol's
  `/v2/point/<lat>/<lon>/<radius>` with the radius the firmware asks for (its default range setting), and stores
  the body unchanged: `<place>/000.json` … `019.json`.
  30 s, not the board's default 10 s, because adsb.lol allows about 20 requests a minute per IP (measured
  2026-11-08: a burst of 10, then 429) and GitHub's runners get less: five places at 20 s (15 a minute) came back
  with 40-60 s gaps and overran the 15-minute limit, publishing 2 of 5. At 30 s it is 10 a minute. Each 429 is
  logged with its Retry-After, so a run's log shows how often the feed refused. A 429 waits out
  its Retry-After (at most 60 s), for every place at once, and asks once more. A reply that lands more than an
  interval late is the new start of the schedule, so a long Retry-After leaves one longer gap in that recording;
  one more than 90 s late, or still failing, fails the place.
- Collects the callsigns of the 12 nearest flights in each reply (the board looks up its 8 nearest) and, after the
  replies, looks each up once on adsbdb, storing `<place>/routes.json` (callsign → the
  reply body, or null for a 404). Requests are spaced (one every interval ÷ places to adsb.lol, 6 s for five places, so no burst; 0.5 s to adsbdb, across the
  threads) and carry the project's
  User-Agent.
- Writes `<place>/index.json`: the place, the start epoch (UTC), the interval (30), the reply count (20).
- Places are recorded in parallel (one thread each), so a run takes about 10 minutes. A place whose recording
  fails keeps no files; the page lists only places with an `index.json`.
- Playback: the simulator sets the community poll interval (`poll_comm_s`, a setting of 2-60 s) to the recording's
  interval, as a board set to 30 s would run, so the replay is in real time. The HAL serves replies by elapsed time since the place started: reply k = elapsed / interval, and a poll the firmware makes early repeats the current reply. After the last (the first poll at replies × interval), the place is loaded again from its start (same
  screen, same settings), so trails and stats start afresh instead of planes jumping back. The page says when the
  recording was made.

## 5. Site data and workflow

`tools/sim_site.py <site> <recordings>` puts into `site/sim/`: `contrail.js`, `contrail.wasm`, `places.json` (the recorded
places), each place's recording, and `map-<place>.pak` built with `build_map.py --regions site/mapdata --lat …
--lon …` (6c's region data, already on the site). `logos.pak` is the one in `site/firmware/`; without a release the
simulator runs with drawn tail fins, as a board without a card.

The Pages workflow gains `schedule: cron "17 * * * *"` (hourly), installs emscripten (cached), records (under `timeout 15m`, so a
hung lookup cannot hold up the deploy), and runs `sim_site.py`, which builds with `tools/build_wasm.sh`. A failed recording run deploys the site without the simulator's places rather than
failing the deploy; a failed WASM build fails it. Load: about 100 requests an hour to adsb.lol across the five
places, and adsbdb lookups for the callsigns seen.

## 6. The page (`sim.html`)

- **Screen:** a canvas at 480×640 CSS px (2×, `image-rendering: pixelated`), framed like the showcase's device, with
  the LED dot beside it. Pointer events on the canvas are the touch screen.
- **Controls:** the place (five buttons or a select), the theme (Cinematic Sky / Bold Editorial), the six screens
  (each shows that screen as a tap on the device would), and "Show an alert" (an emergency takeover, as
  `CONTRAIL_ALERT=emerg` in the desktop simulator). The theme switch follows the firmware's theme, so a change in
  the on-device quick settings shows there too (`sim_theme_name`), and the screen button of the screen shown is
  marked pressed (`sim_screen`). A hidden tab pauses the simulator.
- **Alert defaults:** a place's first start turns the low-pass and military alerts off (25 nm around a big airport
  they are constant, and a first-time visitor would see only takeovers; "Show an alert" demonstrates them).
  Emergency and watchlist alerts stay on, and a visitor's own change in the quick settings survives the loop.
- **Recorded line:** "Recorded at 14:05 local time, 32 minutes ago. A board shows live flights over your home."
- **Loading:** "Loading the simulator (about 1 MB)…" over the screen until the first frame.
- **Links:** "Simulator" in the nav of every page; on the showcase hero "Try it in your browser"; the README.
- **Look:** the site's style; at phone width the canvas scales to fit.

## 7. Errors

| Case | What the page does |
|---|---|
| The WASM or a recording fails to load | "Couldn't load <what>. Check the connection and try again." with Retry |
| No recorded places (the recorder failed) | "No recording is available right now; try again in an hour." |
| A place with no planes recorded | the firmware's own empty screens |
| The worker crashes | "The simulator stopped: <message>" and a link to report it on GitHub, with Restart |
| A browser without Web Workers or WebAssembly | a short message instead of the simulator |

## 8. Testing

- **Pixel parity (CI):** `main_wasm.cpp` exposes the same demo render as `main_sim --render <scene> <screen>
  <theme>` (demo scenes, golden call order, no network). `tools/sim_node_test.mjs` runs the WASM build under Node
  for every screen and theme in `screenshots.sh` and compares each frame with the desktop simulator's PPM: they
  must be identical.
- **HAL (CI, native Unity tests):** `test_replay` (the adsb.lol and adsbdb URL mapping, the replies in order, then the end) and
  `test_range_storage` (`Range` reads in cached blocks, the write overlay, `MapPack` over it), in `pio test -e test`; `hal_wasm` has no emscripten in
  those files, so they build natively.
- **Recorder (CI):** `record_sky.py --selftest`, with the HTTP layer stubbed: file layout, routes, a failed place
  leaving nothing, 429s and late replies.
- **Page:** checked in the browser pane locally and on Pages: each place, both themes, every screen, taps and
  swipes, the quick-settings panel, the alert button, the loop seam, the bytes downloaded.

## 9. As built

Checked 2026-11-08 in the browser pane against `http-server` (which serves `Range` as GitHub Pages does), with a
3-reply recording (a 60 s loop): every place loads and its planes move, with the recorded time in the place's own
zone; both themes; all six screen buttons; a tap selects a plane; a sideways swipe changes screens; press and hold
opens the quick-settings panel, and a theme change there moves the page's theme switch; "Show an alert" brings up
the emergency takeover and the LED dot pulses red; at the loop the screen and theme stay; at 375 px the canvas fits
and the controls stack below it. Nothing red in the console. Every request is for `localhost`; the `.pak` reads are
`206` responses for 16 KB blocks (cached in the worker), never whole files.

- **Build:** `tools/build_wasm.sh`, not a PlatformIO env: PlatformIO's native platform uses the host compiler.
  `-I lib/core` only (a folder-level `-I` shadows system headers on macOS's case-insensitive file system).
- **Site script:** `tools/sim_site.py`, not `.sh`; it builds the WASM itself and skips a place whose `index.json`
  is missing or unreadable.
- **Time zones:** the places carry IANA zones, and `firmware/lib/hal_wasm/localtime.js` replaces emscripten's
  `_localtime_js` with one using `Intl.DateTimeFormat` in `Module.timeZone`, in place of `TZ` (which emscripten
  ignores).
- **Clocks:** `millis`/`micros` go through 64 bits before 32, so they wrap as the device's do.
- **Storage:** `RangeStorage` is writable, with an in-memory overlay, because `MapPack` refuses storage that isn't
  and the map's chart cache writes the chart; the saved chart lasts one loop.
- **Pace:** adsb.lol allows about 20 requests a minute per IP, and GitHub's runners get less, so recordings are 20 replies 30 s apart and the
  simulator sets `poll_comm_s` to the recording's interval (§4 has the 429 and late-reply rules).
- **Loop seam:** the place is loaded again at the loop, so trails and stats start afresh rather than planes jumping.
  Only the keys `Settings` owns are kept (stats start afresh); the screen stays, and a touch hold in progress
  carries over so the director doesn't move off it; an Alert screen isn't restored.
- **Alerts:** a place's first start turns the low-pass and military alerts off (§6).
- **Page:** the theme radios and the pressed screen button follow the device (`sim_theme_name`, `sim_screen`).
  Touch samples are queued in the worker, one per step, with a tap given two pressed samples however short the
  click; moves between are coalesced. A hidden tab pauses the worker. Frames are copied out of the WASM memory
  before they are transferred.
- **Routes:** looked up on adsbdb for the 12 nearest flights per reply (the board looks up its 8 nearest), once
  each, after the replies.
- **Numbers (2026-11-08, Chrome, localhost):** `contrail.wasm` 844,926 bytes (gzipped was 508 KB at 839,291 bytes), `contrail.js`
  14 KB. The worker is ready and draws its first frame in well under a second on localhost (the WASM's download is
  the real cost on a slow link). One place, about 25 s cycling all six screens: 0.32-0.49 MB downloaded (Frederick
  0.32, JFK 0.42, Heathrow 0.49, Schiphol 0.40, SFO 0.37), of which 190-240 KB of its `map.pak` in 12-15 Range
  reads, 50-165 KB of `logos.pak` and the rest replies and routes; the 5 MB `logos.pak` and 0.3-0.6 MB maps are
  never fetched whole.
- **Emscripten:** 7.0.0, pinned in CI with `mymindstorm/setup-emsdk@v16` (cached).
- **Replies by elapsed time:** a settings change (the theme switch, a quick-settings edit) makes the firmware poll
  at once (`CoreApp::applySettings` → `runSoon`), so `ReplayHttp` serves reply = elapsed / interval, not the next one;
  an early poll repeats the current reply (cached, no refetch) and the recording keeps its pace. A repeated reply
  counts as a new poll for the alert engine's two-poll squawk rule (rare; accepted).
