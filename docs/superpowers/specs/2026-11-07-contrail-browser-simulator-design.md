# Contrail Sub-project 6b — Browser Simulator

Date: 2026-11-07
Status: Design approved; not built.
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
| Pages workflow | `.github/workflows/pages.yml` | hourly schedule, emscripten, the recorder, `sim_site.sh` |

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
| `IHttp` | synchronous XHR. An adsb.lol URL (the community provider, `Providers.cpp`) is answered with the recording's next reply; an adsbdb URL (`Routes.cpp`) with the recorded route or a 404. Any other URL: transport error. The real parsers read the real reply bodies |
| `IStorage` / `IFile` | `/contrail/logos.pak` and `/contrail/map.pak` read by HTTP `Range` requests (`readAt` → `bytes=o-(o+n-1)`), so only the bytes drawn are downloaded; `writeAt` goes to an in-memory overlay (the map's saved chart lasts one loop of the recording; the reload starts a fresh card) and `writable` is true, because `MapPack` needs it |
| `IKeyValueStore` | in memory; the worker sets home, the time zone and the theme before `begin` |
| LED, backlight, light sensor | the LED colour is posted to the page (a dot beside the screen); the others are no-ops |

Emscripten's `localtime_r` takes the zone from JS `Date` (the visitor's) and ignores `TZ`, so the build replaces its
`_localtime_js` with one using `Intl.DateTimeFormat` in the place's IANA zone (`Module.timeZone`, set by the worker;
"UTC" for the parity test). Checked 2026-11-07: with it, all 14 site screenshots are byte-identical to the desktop's.

## 4. Recordings

`tools/record_sky.py <places.json> <out-dir>`:

- For each place, every 10 s for 10 minutes (the board's default community poll), GETs adsb.lol's
  `/v2/point/<lat>/<lon>/<radius>` with the radius the firmware asks for (its default range setting), and stores
  the body unchanged: `<place>/000.json` … `059.json`.
- Collects the callsigns of the 12 nearest flights in each reply (the board looks up its 8 nearest) and, after the
  replies, looks each up once on adsbdb, storing `<place>/routes.json` (callsign → the
  reply body, or null for a 404). Requests are spaced (at least 1 s apart per host) and carry the project's
  User-Agent.
- Writes `<place>/index.json`: the place, the start epoch (UTC), the interval, the reply count.
- Places are recorded in parallel (one thread each), so a run takes about 10 minutes. A place whose recording
  fails keeps no files; the page lists only places with an `index.json`.
- Playback: the HAL serves reply k at the k-th poll. After the last, the place is loaded again from its start (same
  screen, same settings), so trails and stats start afresh instead of planes jumping back. The page says when the
  recording was made.

## 5. Site data and workflow

`tools/sim_site.py <site> <recordings>` puts into `site/sim/`: `contrail.js`, `contrail.wasm`, `places.json` (the recorded
places), each place's recording, and `map-<place>.pak` built with `build_map.py --regions site/mapdata --lat …
--lon …` (6c's region data, already on the site). `logos.pak` is the one in `site/firmware/`; without a release the
simulator runs with drawn tail fins, as a board without a card.

The Pages workflow gains `schedule: cron "17 * * * *"` (hourly), installs emscripten (cached), builds `-e wasm`,
records, and runs `sim_site.py`. A failed recording run deploys the site without the simulator's places rather than
failing the deploy; a failed WASM build fails it. Load: about 300 requests an hour to adsb.lol across the five
places, and adsbdb lookups for the callsigns seen.

## 6. The page (`sim.html`)

- **Screen:** a canvas at 480×640 CSS px (2×, `image-rendering: pixelated`), framed like the showcase's device, with
  the LED dot beside it. Pointer events on the canvas are the touch screen.
- **Controls:** the place (five buttons or a select), the theme (Cinematic Sky / Bold Editorial), the seven screens
  (each shows that screen as a tap on the device would), and "Show an alert" (an emergency takeover, as
  `CONTRAIL_ALERT=emerg` in the desktop simulator).
- **Recorded line:** "Recorded at 14:05 local time, 32 minutes ago. A board shows live flights over your home."
- **Loading:** "Loading the simulator (1.4 MB)…" until the first frame.
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
- **HAL (CI, Node):** the adsb.lol and adsbdb URL mapping, the loop, `Range` reads, against a small recording in
  `tools/fixtures/sim/`.
- **Recorder (CI):** unit tests with the HTTP layer stubbed: file layout, routes, a failed place leaving nothing.
- **Page:** checked in the browser pane locally and on Pages: each place, both themes, every screen, taps and
  swipes, the quick-settings panel, the alert button, the loop seam, the bytes downloaded.
