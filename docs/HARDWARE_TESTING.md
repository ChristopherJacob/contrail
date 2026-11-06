# Hardware test checklist (CYD)

Run after flashing a new build: `pio run -e cyd2usb -t upload && pio device monitor`.

| # | Check | Pass when |
|---|---|---|
| 1 | Boot log | `[boot] Contrail …` then `[boot] ready` with heap > 100 KB |
| 2 | Colours | Sky is blue/orange (not yellow/green); text is white. If inverted, toggle `disp_invert`; if red/blue swapped, `disp_bgr` |
| 3 | Orientation | Portrait, text upright. Otherwise set `disp_rotate` to 180 |
| 4 | Wi-Fi portal | Fresh board (or finger held at boot) shows "Wi-Fi setup"; `Contrail-setup` network appears; saving joins home Wi-Fi |
| 5 | Location | Without lat/lon: log shows `[geo] located … by IP` |
| 6 | Live data | Within ~10 s: `[poll] adsb.lol ok, N aircraft`; nearest flights listed, routes appear within a minute |
| 7 | Frame rate | `[health] … drawn=` ≥ 25/s while the radar sweeps (frames that drew something; an idle screen draws few). `busy=` is the share of time spent rendering, `stack=` the loop task's unused stack in bytes |
| 8 | Touch | Flash `pio run -e cyd_touch -t upload` and drag around: the cyan dot must stay under your finger. If an axis is mirrored, swap that axis's `kTouch*Min`/`Max` in `board_cyd.h` |
| 9 | Auto-brightness | Cover the LDR (front, near the screen corner): backlight dims within a few seconds |
| 10 | Offline | Turn Wi-Fi off at the router: status line shows OFFLINE, data ages; recovers when back |
| 11 | Soak | See "Soak" below: no resets, `heap=` ≥ 55 KB, `largest=` ≥ 18 KB (TLS) with no downward trend |
| 12 | Safe mode | (Optional) three crash resets in a row show "Safe mode" |
| 13 | Logbook | After 10 minutes: "[stats] saved N keys"; after a reset: "[stats] loaded: …" with the same counts |

## Screens

Flash `pio run -e cyd2usb -t upload && pio device monitor` and check each screen (swipe to
reach one; it then stays for a minute):

| Screen | Pass when |
|---|---|
| Radar | The sweep turns smoothly every 4 s; blips light as it passes and fade; the nearest five carry readable labels. Tap a blip: it gets an amber ring and the bottom strip shows that flight. Tap the strip: the screen dips to dark and comes back as Closest Flight for that plane |
| Closest Flight | The plane and contrail sit in the sky band, the card shows callsign, altitude and distance, and with a route its endpoints and progress. Tap the card: it steps to the next plane |
| Departure Board | Rows for the nearest flights; when the order changes, rows glide to their new places (no jump or flicker) |
| Sky View | At night: stars, the moon at its phase, planes drawn over the treeline right down to the rim, blinking beacons. By day: no dark moon, sky colour follows the sun |
| Live Map | The chart (land, water, coastline, lakes) appears within one veil dip; rings at ½ and full radius, the scale bar and "N ↑"; airports labelled without overlaps, runways for large airports up to 50 nm. Planes move smoothly, coloured by altitude, with trails that grow a point every 10 s. Tap a plane: amber ring, the strip shows it; tap the strip: Closest Flight for that plane |
| Stats | Today's count, busiest hour, the current hour highlighted, top airlines with brand stripes, the types card. Swipe up or down: All-time (top types, airline totals, "Since …") and back. Before the clock is set: the all-time logbook with a "No clock" note. After a reset the counts come back (`[stats] loaded: …`) |
| Bold Editorial | Flash `pio run -e cyd2usb_editorial -t upload`: all six screens flat, display text condensed (capitals); Closest Flight and the Board in airline livery colours with readable (black on light) text |
| Explore mode | Swipe left/right: the six page dots show which screen is up and the screen holds for a minute; after a minute without touch the director takes over again (dots go, the tapped selection is dropped) |

## Benchmark (`cyd_bench`)

`pio run -e cyd_bench -t upload && pio device monitor` (about 4 minutes, until `[bench] done`).
It times the drawing primitives, the radar per-primitive breakdown, the Live Map's mask
rebuild and airport query at two places (`[map]`), then every screen in both themes with the
demo scenes on 16-row strips: `[screen] <name> full …` (a full redraw, and `enter`, the
screen's `enter()`), a `full` line with the per-layer split, `idle`, `steady` (scene rebuilt
at 5 Hz the way live data moves it, 32 aircraft in `sceneBusy`), the Board's `glide`, the Sky
View's minute `recompute`, the Map's `trailed` (every plane with a full trail), and a screen switch: `leave` (this screen fades out through the
veil and another fades in) and `arrive` (back to this one). Every veil frame redraws the
whole screen. Times are CPU only (update + draw, pixels discarded); on the device a full
screen also takes ~30 ms of SPI transfer (76,800 px at ~2.5 Mpx/s), which overlaps with the
drawing.

Pass 1, measured 2026-10-31 (CPU ms per frame; 33 ms is 30 fps, 40 ms is 25 fps):

| Screen | Full redraw | Steady avg | Steady worst | Other |
|---|---|---|---|---|
| Closest Flight | 19.6 | 1.6 | 9.8 | |
| Radar | 30.5 | 13.1 | 24.8 | |
| Departure Board | 19.5 | 0.4 | 17.5 | glide avg 4.6 / worst 10.8 |
| Sky View (dusk) | 48.2 | 4.4 | 26.1 | |
| Sky View (night) | 31.0 | 1.0 | 9.0 | minute recompute 35.3 |

Pass 2, measured 2026-11-02 (build 6be5ff9, CPU ms per frame; switch = avg / worst over the
13 frames of a veil dip, leaving the screen and arriving at it):

| Screen | Theme | Full redraw | Steady avg | Steady worst | Other |
|---|---|---|---|---|---|
| Closest Flight (busy) | Cinematic | 20.9 | 1.7 | 10.4 | switch leave 31.5 / 38.4, arrive 29.6 / 36.4 |
| Closest Flight (busy) | Editorial | 19.7 | 1.1 | 8.7 | switch leave 23.5 / 27.4, arrive 23.3 / 27.1 |
| Closest Flight (night) | Cinematic | 18.0 | 1.7 | 10.7 | switch 26.5–27.7 / 32.6 |
| Closest Flight (night) | Editorial | 19.5 | 1.1 | 8.4 | switch 22.1–22.6 / 27.3 |
| Radar | Cinematic | 29.8 | 13.6 | 24.6 | switch leave 29.0 / 37.8, arrive 31.5 / 39.1 |
| Radar | Editorial | 17.1 | 10.3 | 18.3 | switch 23.2–23.7 / 26.6 |
| Live Map | Cinematic | 44.1 (chart 23.3, planes 19.1, strip 1.4) | 4.7 | 34.1 (2 of 120 over 33) | with full trails across a trail sample (`trailed`, build cec835e) 3.9 / 29.4; switch leave 34.7 / 52.9, arrive 38.8 / 58.0 |
| Live Map | Editorial | 43.8 (chart 23.2, planes 19.1, strip 1.2) | 4.8 | 34.6 (2 of 120 over 33) | switch leave 33.9 / 52.6, arrive 38.2 / 58.0 |
| Departure Board (busy) | Cinematic | 21.3 | 0.5 | 19.2 | glide 5.1 / 12.6; switch 25.0 / 28.3 |
| Departure Board (busy) | Editorial | 11.6 | 0.5 | 12.5 | glide 3.0 / 8.6; switch 19.4–20.8 / 26.7 |
| Departure Board (night) | Cinematic | 14.6 | 0.3 | 10.5 | glide 4.2 / 9.4; switch 19.5–20.8 / 25.9 |
| Departure Board (night) | Editorial | 8.3 | 0.3 | 6.9 | glide 2.6 / 6.3; switch 16.9–19.1 / 26.8 |
| Sky View (dusk) | Cinematic | 53.7 | 5.4 | 31.1 | recompute 4.2–9.8; switch leave 37.8 / 60.6, arrive 42.9 / 60.7 |
| Sky View (dusk) | Editorial | 49.3 | 4.4 | 25.7 | recompute 4.2–8.2; switch leave 35.5 / 56.6, arrive 40.0 / 56.0 |
| Sky View (night) | Cinematic | 33.8 | 1.3 | 11.3 | recompute 38.1–38.7; switch 28.7–31.1 / 42.1 |
| Sky View (night) | Editorial | 36.1 | 1.2 | 10.2 | recompute 40.4–40.6; switch 30.1–32.7 / 43.7 |
| Stats | Cinematic | 13.2 | 0.1 | 0.9 | switch 20.6–22.2 / 28.2 |
| Stats | Editorial | 10.2 | 0.1 | 0.8 | switch 18.6–20.3 / 26.8 |

Live Map mask rebuild and airport query (`[map]`, µs; on entering the map or a new home or
radius; target ≤ 60 ms):

| Place | Radius | Land tiles | Mask | Airports | Query + labels |
|---|---|---|---|---|---|
| SFO | 5 nm | 1 | 756 | 1 | 460 |
| SFO | 25 nm | 4 | 676 | 7 | 445 |
| SFO | 50 nm | 4 | 623 | 10 | 480 |
| SFO | 100 nm | 6 | 728 | 20 | 1,029 |
| JFK | 5 nm | 1 | 556 | 1 | 206 |
| JFK | 25 nm | 2 | 685 | 6 | 439 |
| JFK | 50 nm | 3 | 913 | 15 | 726 |
| JFK | 100 nm | 10 | 1,860 | 20 | 1,422 |

Pass when every steady worst stays under 40 ms and every `[map]` mask under 60 ms (both
hold in both themes). A full redraw only happens on a screen switch (every frame of the
0.4 s veil dip) or the Sky View's once-a-minute recompute. Screen switches into or out of
the Live Map and the dusk Sky View run at ~17–20 fps for those 0.4 s (their full redraws
are 44–54 ms of CPU: the Map's per-pixel chart and haloed plane labels, the Sky View's
gradients and glows); on the device the SPI transfer adds to the frame, so a live log's
"over 33 ms" frames are almost all switch frames (the `[render]` line counts them apart).
The runways on the Map cost ~1.4 ms of a full redraw and ~0.4 ms of a steady frame
(measured by turning them off), so projecting them once per rebuild was not worth the
arena space.

## Soak (`cyd2usb_soak`)

`pio run -e cyd2usb_soak -t upload`, then log the serial monitor for 30 minutes
(`pio device monitor | tee soak.log`). This build cycles all six screens every 20 s and switches theme after every cycle (`[soak] theme …` every 2 minutes).

Pass when: no `rst:`, `Guru`, `panic` or `WDT` lines after boot; every `[health]` line has
`heap=` ≥ 55 KB and `largest=` ≥ 18 KB (a TLS handshake needs it), with no downward trend
over the run; every `[stats] saved` is followed by a steady `[nvs] free=`. Then report the true
minimum too, which the `[health]` sample can miss: `minheap=` (ESP-IDF's lowest free heap since
boot) and each `[heap] low= largest= during …` line (the lowest free heap of a 30 s window when
under 56 KB, sampled on both threads, with what the network thread was doing). An allocation
failure, a fetch or TLS connection refused for low memory, or `minheap=` under ~30 KB fails the
soak. The `[render]` line counts frames over 33 ms, how many of them fell in screen switches,
and the window's slowest other frame with its screen (`ScreenId`: 0 Closest, 1 Radar, 2 Board,
3 Sky, 4 Map, 5 Stats).

Soak result (2026-10-31, CYD2USB, live adsb.lol traffic, 3–12 aircraft, build 39fd322): **pass**. 30 min cycling all four screens every 20 s: no resets or crashes; 176/176 polls and every route lookup succeeded; free heap ≥ 59,740 B; largest block steady at 26,612 B; loop stack high-water ≥ 3,324 B free; slowest frames ~56 ms (Sky View full redraws on entering the screen).

Soak result (2026-11-02, CYD2USB, live adsb.lol traffic, 20–37 aircraft, build 18f4e7a, log
`soak-pass2.log`): **no resets or failures, but the heap minimum is a finding.** 30.5 min
cycling all six screens every 20 s, theme flipping every 2 minutes (15 flips):
- No `Guru`, `panic`, `WDT` or reset after boot; 181/181 adsb.lol polls ok; 54 route lookups
  (37 found, 17 404 = unknown route); no fetch refused, no allocation failure logged.
- Steady heap (`[health]`, 61 lines): `heap=` 52,072–59,940 B (60 of 61 lines ≥ 55,964 B;
  the one 52,072 B line, at 300 s, caught a poll in progress), `largest=` ≥ 19,444 B (usually
  22,516 B); no downward trend (59,764 B at the end). Loop stack ≥ 3,260 B free.
- True minimum: `minheap=` 35,648 B (reached by 480 s, never lower after). All 61 `[heap]`
  windows dipped under 56 KB: 49 during `GET api.adsb.lol`, 8 during `GET api.adsbdb.com +tls`
  (new handshakes), 4 with no request or save; all seen by the render thread. The worst:
  **35,816 B free with a 1,780 B largest block** during an adsb.lol poll (Stats up); others
  38,196 / 3,444, 40,104 / 5,620, 41,152 / 7,668 (all adsb.lol). The adsb.lol response
  handling (parse and store, ~33 aircraft) fragments the heap down to a few KB of contiguous
  memory for a moment; an allocation of more than ~1.7 KB then would fail.
- NVS: `[stats] saved 5 keys` three times (~10 min apart); `[nvs] free=` 421 → 421 → 420 → 419.
- Frames: 33,707 drawn, 1,059 over 33 ms, 918 of them inside screen switches (veil frames,
  max 77.7 ms). Of the 141 others, the Live Map has the slowest: its windows' worst steady frame
  is 56–62 ms (bench, CPU only: 34 ms), about eight such frames per 20 s on the Map, most
  likely the frames where most of the 32 planes' boxes (glyph, label and trail) change at once
  and the merged damage covers much of the chart, plus the SPI transfer and the network task.
  The other screens' worst non-switch frames: Radar 45–52 ms, Board ≤ 40 ms, Sky ≤ 35 ms,
  Closest 39 ms, Stats 38 ms (the full redraw when the soak flips the theme).
- Live Map slow frames, measured on the board (2026-11-03, Map pinned, a per-frame split): they
  are the frames where nearly every plane changes at once, a poll's position corrections or the
  trail sample every 10 s (every trail redraws). Then the damage covers 50–75% of the chart and
  the frame was ~7 ms sync, ~9 ms chart, ~15 ms trails, ~12 ms glyphs, ~6 ms labels, ~7 ms push
  and DMA wait. Fixed without changing a pixel: blocks are drawn and pushed over only the rows
  their damage covers (not the whole 16-row strip), a plane whose trail is unchanged damages
  only its glyph, label and trail's last leg, and the chart's open water and land, polygon
  edges, line end caps and text got cheaper. With 32 synthetic planes at 250–500 kt and full
  trails (10 min each): frames over 40 ms 459 → 51 of 2,775, window worst 61–69 → 42–49 ms
  (what is left over 40 ms is mostly the trail sample, 40–49 ms, and now and then a poll). Live, 6–18 aircraft:
  window worst 26–43 → 21–29 ms.

## Alerts

Send `alert emerg`, `alert watch`, `alert mil` or `alert low` (with a newline) over the USB serial port at
115200 baud to show a made-up alert of that kind. Each should show the takeover in its colour (red, blue,
amber, green), pulse the border, LED and backlight once a second, end after 30 s or on a tap, and log
`[alert] <KIND> N512TS`. During quiet hours (default 22:00-07:00) only an emergency pulses.

Results (2026-11-03, CYD2USB, cinematic theme, live feed of 14-20 aircraft):
- All four kinds fired from serial and logged `[alert] <KIND> N512TS`; a real low pass (N12704) also
  fired, and the queued test low pass waited its turn behind it.
- Emergency pulsing (`[render]`, a 30 s window inside the alert): 1,366 frames, 17 over 33 ms (12 in
  screen switches), steady max 36 ms. Before the Alert screen's draw work was trimmed: about 600-1,000
  frames, ~270 over 33 ms, steady max 61 ms.
- Heap (`[health]`): `heap=` 57.3-57.9 KB, `largest=` 20,468 B, within the pass-2 range.
- The board's clock runs on UTC (no time zone is set yet), so quiet hours began at 22:00 UTC and the
  non-emergency test alerts showed softened. LED colours and the tap-to-dismiss were not yet checked by eye.

## Configuration

`http://contrail.local` (or the board's IP) serves the settings page. Hold anywhere on the screen
for 1 s to open the touch panel. `curl` checks of the API:
`curl -X POST -H 'Content-Type: application/json' -H 'X-Contrail: 1' -d '{"radius_nm":30}' http://contrail.local/api/settings`.

Results (2026-11-04, CYD2USB, cinematic theme, live feed of 17-29 aircraft):
- Flash: 1,802,753 → 1,840,641 bytes (+37.9 KB, budget +40 KB). Page: 17.8 KB, 6.8 KB gzipped.
- API: page, `/api/info`, `/api/schema` (3.75 KB), `/api/settings` all 200 in 0.05-0.3 s. Secrets come
  back as `{"set":…}` only. Out of range → 400 `out of range 5–100`, nothing stored; no `X-Contrail`
  → 403.
- Live changes: tz rule → `[tz] set to CST6CDT,...` at once; back to Automatic → `[tz] now <-05>5`;
  theme → `[display] theme editorial` at once; `disp_bgr` → reply then restart ~1 s later.
- Firmware upload: the current build (1.85 MB) installed in 19-23 s, restarted, settings kept. A
  truncated file → 400 `file incomplete`, old firmware kept running, polls resumed. A guard-less
  upload → 400, board stays up (it reset on the task watchdog before the upload loop yielded per chunk).
- Fixed on hardware: `/api/schema` never answered while TLS polls fragmented the heap (largest block
  3-10 KB) because the reply was built as one String; replies now stream through a 512 B buffer.
- Heap: steady 43-49 KB (57 KB before this feature), minheap 36.9 KB in normal use. The web task's
  stack peaks at ~2.6 KB (upload included) and is now 5 KB, not 8 KB.
- Stress: 900 back-to-back requests in 90 s all succeeded (minheap 44 KB) with OpenSky as the source.
  An earlier run with two TLS connections open drove the largest free block to 84 B and the Wi-Fi
  link died with `WiFi.status()` still connected (no ping, DNS failing) until reset. A link watchdog
  now reconnects after 3 min with no HTTP reply and restarts 3 min after that; it has not yet been
  seen to fire on a real dead link.
- Not yet checked by eye (needs someone at the board): the hold ring, the touch panel in both themes,
  the brightness slider's live preview and any snap-back on release, fast radius taps, Done and the
  30 s close, an emergency closing the panel, the Update screen during an upload, and the trimmed setup
  portal.

## SD card and logos (5a)

Put `logos.pak` (`tools/.venv/bin/python -I tools/build_pack.py --logos assets/logos --out logos.pak`) in
`/contrail/` on a FAT16 or FAT32 microSD card, either from a computer or over USB without removing the card:
flash the helper (`pio run -e cyd_sdload -t upload`) and run
`tools/.venv/bin/python -I tools/sd_upload.py --port <port> --file logos.pak` (460,800 baud; it checks the CRC32 of the
file read back before renaming it into place). Then flash `cyd2usb` again.

| Check | Pass when |
|---|---|
| Card at boot | `[sd] mounted in N ms (FAT32, SDHC, 20 MHz)`, `[sd] logos.pak: N logos`; `/api/info` has `"sd":{"mounted":true,"logos":N,"error":null}` and the Firmware card shows the count |
| No card | `[sd] not mounted (N ms): no card`; fins everywhere; `/api/info` `"mounted":false` |
| Card pulled while running | `[sd] read failed: logos off until restart` the next time a badge loads; fins from then on; no crash |
| Logos by eye | Closest Flight, the Board and an alert show real logos (Bold Editorial on a white tile), fins for airlines without one, the shape tile for a registration, in both themes |
| Heap | `minheap=` ≥ 30 KB over a soak; no TLS or allocation failures |

Results (2026-11-04/05, CYD2USB, live adsb.lol traffic of 2-8 aircraft at night, build 6cfaf2a):
- **Card:** SDHC, about 128 GB, first partition a 117 MB FAT32 volume with 512-byte clusters. `logos.pak`
  (1,628 logos, 5,299,358 B, 10,400 clusters) was written with `cyd_sdload` + `tools/sd_upload.py` at
  460,800 baud in about 165 s.
- **Two SD stacks measured.** The Arduino SD library (FATFS under the VFS): flash 1,907,349 B (97.0 %),
  ~17 KB of heap with the card mounted, `minheap` 28,024 B and mbedTLS BIGNUM allocation failures during
  TLS handshakes, so it was replaced. Our own reader (`SdSpi` + `FatReader`): flash 1,872,329 B (95.2 %),
  static RAM +1.5 KB, no heap; the numbers below are all with it.
- **Mount and load:** mounted in 4-5 ms (FAT32, SDHC, 20 MHz) over three boots; opening `logos.pak`
  (header, index check and the loop-checked walk of its whole cluster chain) about 0.2 s more (log
  timestamps). (A `[sd] first badges in 0 ms` boot line, since removed, only timed the screens' re-entry: at
  boot the scene is still empty.) One badge load (index binary search, palette, pixels, CRC; timed with a temporary
  log line around `LogoStore::load`, since removed): 25 px 6.3-8.9 ms, 30 px 9.2-9.6 ms, 40 px 8.9-9.3 ms
  (the spec estimated ~2 ms). The Board loads its new airlines on entering, 2-3 here, so 13-24 ms in one
  frame, which lands inside the screen switch's veil.
- **Normal build, 5.5 min** (`cyd2usb`, director mode): no resets, TLS or HTTP errors; every adsb.lol poll
  and adsbdb lookup answered (404s are unknown routes). `[health]`: `heap=` 45,260-91,624 B (about 46 KB
  while a TLS connection is held, about 91 KB when none is), `minheap=` 33,916 B, `largest=` 10,228-59,380
  B; loop stack ≥ 2,844 B free, web stack 4,168 B free. `[heap]` windows: the worst 34,556 B free with a
  4,340 B largest block, during an `api.adsbdb.com` TLS handshake. Frames: steady max 22-38 ms (Radar
  30-38 ms; the Board's window 37.4 ms once and 23.2 ms); the frames over 33 ms were nearly all in screen
  switches.
- **Soak, 11 min** (`cyd2usb_soak`, every screen every 20 s, theme flipped five times; `alert emerg` and
  `alert watch` sent from serial, with the made-up flight given an airline callsign in a temporary build so
  the Alert loaded the UAL logo): no `Guru`, `panic`, `WDT` or reset; no TLS, HTTP or SD errors; no
  `[sd] some logos failed their checks`. Real badges loaded on Closest (APZ, ASH, KII, SCX at 40 and 30 px),
  the Board (25 px, up to three at once) and the Alert (UAL 40 px).
  - Heap: `minheap=` 30,612 B (reached by 300 s), just above the 30 KB floor; `[heap]` worst 30,704 B free
    with a **660 B** largest block, and 31,640 / 1,076, during `api.adsbdb.com` TLS handshakes. Steady
    `heap=` 41,464-91,648 B.
  - Steady frame times per 20 s visit (temporary per-screen log, since removed; avg / max ms):

    | Screen | Cinematic | Bold Editorial |
    |---|---|---|
    | Closest Flight | 7.6-8.6 / 9.9-40.4 | 5.0-8.4 / 7.4-22.0 |
    | Radar | 11.8-12.9 / 26.6-53.9 | 10.4-11.7 / 22.6-25.5 |
    | Departure Board | 8.6-10.3 / 9.4-13.5 | 9.9-10.0 / 11.6-12.4 |
    | Sky View (night) | 3.9-5.4 / 45.1-46.7 | 3.9-4.3 / 48.5-51.7 |
    | Live Map | 6.7-7.9 / 13.0-13.5 (40.6-41.2 in the visits where the theme flipped) | 7.1 / 13.7 |
    | Stats | 1-2 frames a visit (nothing moves) | the same |
    | Alert | | emergency 7.3 / 11.3; watchlist (quiet hours, softened) 1.1 / 1.7 |

    The Board drew only 3-14 steady frames per visit with 2-3 aircraft (nothing moved), so its numbers are
    for a quiet board; it needs a recheck in busy daytime traffic. A screen's one or two slow frames are its
    first frame, a theme flip's full redraw or the Sky View's recompute. Over the soak 362 of the 372 frames
    over 33 ms were in screen switches.
- **Flash:** 1,840,641 B (93.6 %) at the start of 5a (0675565 on main) → 1,872,329 B (95.2 %): **+31,688 B
  (30.9 KB), inside the 45 KB budget.** Static RAM 89,816 → 91,808 B (+1,992 B). After the review fixes
  (below): 1,872,113 B flash (+31,472 B for 5a) and 90,688 B static RAM (+872 B).
- Worth watching: every new TLS connection (adsb.lol after boot, each adsbdb.com reconnect) took 11.4-12.4 s
  to connect, though nothing failed; and the soak's 660 B largest block during a handshake leaves little
  room.
- **Review fixes (2026-11-05, build 6ba69ce):** static RAM 91,808 → 90,688 B (−1,120 B: FatReader's data and
  FAT caches merged into one 520 B cache with checkpoints 32 → 8, −600 B; one file slot instead of two,
  −192 B; the engine ellipse's unit circle as constexpr tables in flash, −248 B; LogoStore's status as
  literals, −64 B; nm: `sd` 444 → 164 B, `logos` 92 → 28 B). Flash 1,872,329 → 1,872,113 B.
  - **Contiguous-file reads:** `open()` found the card's `logos.pak` contiguous, so reads skip the FAT. One
    badge load, 20 airlines × 3 sizes loaded at boot with a temporary log (since removed), the same two runs
    each before and after: average 7.1 / 8.0 / 8.4 ms → **5.0 / 5.9 / 6.4 ms** at 25 / 30 / 40 px (range
    5.5-10.2 → 3.7-7.7 ms); all 60 loads 476 → 352 ms, **26 % faster**.
  - **2 minutes, normal build** (`cyd2usb`, 1 aircraft at 00:37 local): `[sd] mounted in 4 ms (FAT32, SDHC,
    20 MHz)`, `[sd] logos.pak: 1628 logos`; `[health]` `minheap=` **35,092 B** (by 30 s; the worst `[heap]`
    window, 35,716 B free with a 5,364 B largest block, was during an `api.adsbdb.com` TLS handshake), `heap=` 47,844-92,364 B,
    `largest=` 11,764-57,332 B, loop stack ≥ 2,908 B free, web stack 4,168 B free; no resets, TLS, HTTP or
    SD errors. A soak with the savings is still to be run (the soak's 30,612 B was before them).
- **Pending, needs someone at the board** (not yet done): boot with no card (expect
  `[sd] not mounted (N ms): no card` and fins everywhere); pull the card while running (expect
  `[sd] read failed: logos off until restart` the next time a badge loads, fins, no crash); check by eye that
  logos, fins and shapes look right on the panel in both themes.

## Detailed map (5b)

Build `map.pak` for the board's home (`tools/.venv/bin/python -I tools/build_map.py --lat … --lon … --out
map.pak`), put it in `/contrail/` from a computer or with `cyd_sdload` +
`tools/sd_upload.py --file map.pak --dest /contrail/map.pak` (which replaces an existing `map.pak`), then flash
`cyd2usb` again. Set the screen mode to stay on the Live Map, and compare against the built-in map by setting a
home outside the pack (latitude 45 on the web page) or by renaming `map.pak` away.

| Check | Pass when |
|---|---|
| Card at boot | `[sd] map.pak: 250 nm around <lat>, <lon>`; the Firmware card shows "Map: detailed · 250 nm around …" and the credit line |
| First view of a home, radius or theme | "Drawing map…" in the strip, the chart filling top to bottom, then `[map] chart drawn in N ms (level L): layout, paint, card read and write times and blocks, F frames`; ≤ 1.5 s |
| Restart, same view | `[map] saved chart (level L, N names): no rebuild`; the map is up at once |
| Radius change | the old chart stays up for 1.5 s after the last change, then one rebuild |
| Home outside the pack | built-in map; "Map: built-in (home is outside the map pack; rebuild it)" |
| No location set | "Set location" on the map; "Map: built-in (no location set)" |
| No `map.pak` | built-in map; "Map: built-in" |
| Card pulled with the map up | `[map] card error: the built-in map until restart`; built-in map within a frame or two; no crash; "Map: built-in (card error)" |
| A damaged tile (a byte flipped in a level 0 tile) | detailed map, that tile water (or land without its lines if it is all land); "(1 damaged tiles)" |
| Frames | `[render]` and `[health]` with 25-30 planes, against the built-in map at the same radius |
| Heap | `minheap=` ≥ 30 KB over 10 minutes with the map up and busy traffic |

Results (2026-11-05, CYD2USB, SDHC card, FAT32; a 250 nm pack, 0.55 MB, for the developer's home; mock traffic
from a local feed of 23-30 aircraft, and real adsb.lol traffic). Frame figures are 30 s `[health]` / `[render]`
windows; windows that contain a screen switch or a rebuild are left out.

- **Card writes:** the saved chart is written into `map.pak` and survives restarts: every boot onto a view
  already drawn reopened it without a rebuild (`[map] saved chart (level 0, 16 names): no rebuild` in pass 3).
  Multi-block CMD18 and CMD25 ran through all of passes 2 and 3 with no card errors; `[sd] logos.pak: 1628
  logos` at every boot, and no SD errors in passes 2-3.
- **Pass 1** (build 33c0e9c: rows of 480 B, single-block reads and writes, 2 bands a frame):
  - Rebuild 1,192 ms at 10 nm (276 block reads), 1,430-1,552 ms at 25 nm (477-507), 2,967 ms at 50 nm (764),
    4,724 ms at 100 nm (943) with real traffic and TLS polling; 1.3-1.9 s with the mock feed. Over budget,
    mostly because the job did 2 bands a frame at about 5 frames a second.
  - Frames with 26-30 planes at 25 nm: typical 80-100 ms, max 194-234 ms, busy 35-46 %, against the built-in
    map's 17-21 ms, 50-65 ms and 10-13 %. At 100 nm 20-60 ms against 10-18 ms.
  - With 64 aircraft on the real feed at 100 nm, `largest=` fell to 236 B and TLS connects failed
    ("SSL - Memory allocation failed") every ~31 s until a reboot (see the heap comparison below).
- **Pass 2** (build 3d26351: the chart in 16 × 16 tiles, multi-block transfers, the job paced at ~60 ms a frame):
  - Rebuild 505-509 / 747-751 / 940-959 / 1,090-1,142 ms at 10 / 25 / 50 / 100 nm (two runs each). Card
    reads 218-219 / 412-414 / 542-549 / 638-641 ms (354 / 689 / 912 / 1,080 blocks), writes ~140 ms (260 blocks), paint 61-185 ms, layout 3-13 ms,
    7-12 frames.
  - Frames with 24-30 mock planes, 30 s windows: detailed 25 nm busy 22-33 %, typical 13-45 ms, max
    147-210 ms, 109-130 of 150 frames over 33 ms; built-in 25 nm 10-11 %, 15-22 ms, 53-61 ms, 4-8 over.
    Detailed 100 nm 9.6-9.9 %, 11-63 ms, max 145-187 ms, 7-14 over; built-in 100 nm ~5 %, 9-12 ms, 43-47 ms,
    3 over.
  - **Heap comparison** at 100 nm, real traffic, 5.5 minutes each: detailed map with 63-64 aircraft
    `minheap=` 31,736-34,084 B, the worst `[heap]` windows 31,860 B free with a 1,012 B largest block and
    32,348 / 692; the same 63-64 aircraft with the pack present but the Radar up (no map) `minheap=`
    31,368 B, worst windows 32,184 / 1,012 and 32,880 / 1,012. (The built-in map run, at latitude 45, saw only
    25-29 aircraft: `minheap=` 31,812-31,916 B, worst 31,864 / 4,084.) The small largest blocks come with
    the 64-aircraft feed whatever the screen; the map path allocates nothing. No TLS failures in pass 2.
- **Pass 3** (build 45f1d4b: trail points staggered across the 10 s, planes on whole pixels over the
  detailed chart), 20 minutes:
  - Rebuild 709 ms at 25 nm (layout 13 ms, paint 86 ms, card read 382 ms / 689 blocks, write 153 ms /
    260 blocks, 7 frames) and 1,052 ms at 100 nm (8 / 187 / 592 ms / 1,080 / 148 ms / 260, 12 frames).
  - Frames, mock 24-30 planes: detailed 25 nm busy 12.4-16 %, typical 13-33 ms, max 75-113 ms, 34-64 of 150
    frames over 33 ms; built-in 25 nm 11.0-14.3 %, max 35-60 ms. Detailed 100 nm 5.5-8.2 %, max 44-94 ms;
    built-in 100 nm 4.4-5.1 %, max 22-32 ms. Real traffic, detailed 25 nm: busy 9.7-11.1 %, max 73-115 ms.
  - `minheap=` 72,504 B over the 12 minutes on the mock feed; 30,452 B once real traffic and its TLS
    connections came back; loop stack ≥ 2,892 B free, web stack ≥ 2,184 B. No resets, panics or SD errors.
- **40 MHz card clock:** tried in a separate build and refused. Arduino routes VSPI's pins through the GPIO
  matrix, whose input delay is expected to limit reads to about 26 MHz, so that build also moved SCK, MOSI and
  MISO onto the IO_MUX and switched the card to high speed. As reported from that board run (no log was
  saved), the card then would not mount: `[sd] not mounted (4 ms): read refused (CMD17)`. The default build
  stays at 20 MHz (stepping down to 10 MHz on errors), and the code for 40 MHz was removed.
- **Soak build:** `cyd2usb_soak` flips the theme after every screen cycle; each flip makes the next visit to
  the Live Map draw and save its ~130 KB chart again (the card holds only the last one).
- **Pending, needs someone at the board:** boot with no card (expect the built-in map and "Map: built-in");
  pull the card with the map up (expect `[map] card error`, the built-in map, no crash); a damaged tile
  ("(1 damaged tiles)", that tile left as water); the detailed map by eye on the panel at 10, 25, 50 and
  100 nm in both themes; touch on the detailed map (selecting a plane, the strip opening Closest Flight).

## Release and install (6a)

Proven without the board (2026-11-05, on the private repo):

- **CI** (`ci.yml`): native tests (624), tool self-tests and all nine board and simulator builds pass on
  `ubuntu-latest`. The goldens match the Mac byte for byte since the test and sim envs build with
  `-ffp-contract=off`. A run takes about 15 minutes cold and about 3 with the build cache warm.
- **Release** (`release.yml`), with the test tag `v0.0.0-test` (deleted afterwards): a pre-release with
  `contrail-cyd2usb-install.bin` (1,967,296 B), `contrail-cyd2usb-app.bin` (1,901,760 B), the same pair for
  `cyd_microusb`, the versioned copies, `logos.pak` (5,299,358 B, 1628 logos) and `SHA256SUMS`, in about
  5 minutes. Checks on the downloaded install image: its line in `SHA256SUMS` verifies; `esptool image_info`
  finds a valid bootloader at 0x1000 (DIO, 40 MHz, 4 MB) and a valid app at 0x10000; the partition table is
  at 0x8000; the app holds the version string `0.0.0-test`.
- **Pages** (`pages.yml`): the build job makes the site with the 14 simulator screenshots (pixel-identical to
  the Mac's). Deploying needs the public repo and Pages enabled, so it is proven only after going public.
- **Site**, served locally: both pages at desktop and phone width, no console errors; the board picker
  switches the manifest; "No release yet" without a release; with one, "Latest version" and the Install
  button, whose manifest's image resolves on the site. A browser `fetch()` of a GitHub release download fails
  (no `Access-Control-Allow-Origin`), which is why the site serves its own copies of the install images.

Needs the developer at the board (Web Serial needs a person to pick the port):

- **Install from the site** onto the CYD2USB after the first public release: Install, pick the port, erase
  off; the board boots the new version (`[boot] Contrail <version>` in the log, the version on the web page)
  with its settings kept. Then once with erase on: it boots into `Contrail-setup`.
- **cyd_microusb** is untested on hardware: the Install page says so.
