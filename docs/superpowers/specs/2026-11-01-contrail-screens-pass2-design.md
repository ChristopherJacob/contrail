# Contrail Sub-project 2 (pass 2) — Live Map, Stats and Bold Editorial Design

Date: 2026-11-01
Status: Built (plan `docs/superpowers/plans/2026-11-01-contrail-screens-pass2.md`)
Parent: `2026-10-31-contrail-screens-design.md` (pass 1)
Mockups: private canvas "Contrail screen mockups" (variants picked: 5A, 6A, 7A)

## Goal

Add the last two regular screens, Live Map and Stats, and the second theme, Bold
Editorial, on top of the pass 1 screen system.

Out of scope: the alert takeover (sub-project 3), the detailed SD-card map (sub-project 5),
live theme switching and on-device settings UI (sub-project 4), all-time records (highest,
fastest, closest; not wanted).

**Done when:**
1. Live Map and Stats render on the CYD2USB and in the SDL build, matching mockups 5A and
   6A in layout and spirit; all six screens render in Bold Editorial matching 7A in spirit.
2. Every screen, in both themes, holds ≥ 25 fps with 40 aircraft in range on hardware
   (on-device benchmark).
3. Stats survive a reboot and reset at local midnight.
4. `pio test -e test` passes, including the goldens and unit tests in §7.
5. A 30-minute hardware run cycling all six screens in both themes: no resets; ≥ 55 KB
   free, ≥ 18 KB largest block.
6. Firmware leaves ≥ 100 KB free in the app partition.

**Decisions taken while planning and building** (the plan's Decisions list has the reasons;
this is what was built where it differs from or settles the sections below):

- *Arena:* one 4,096 B `ScreenArena` per `ScreenApp`, owned by whichever screen entered last.
  Besides the Map's mask (2,025 B), 20 airport marks and 32 plane states, it took over the
  Radar's blips, the Board's rows and the Sky View's stars and tracks from static memory, to
  pay for pass 2. **Map trails stay outside the arena** (`MapTrails`, 32 planes × 8 points,
  ~1.3 KB static): they sample every scene rebuild whatever screen is up, and in the arena
  they would be lost at every switch.
- *Map format:* 2,048 units per 2° tile side (~108 m of latitude, not ~25 m), steps as int8
  deltas with a 4-byte int12-pair "medium" escape and a 5-byte int16-pair long escape (1:50m
  vertices are mostly too far apart for int8 at finer units). **Antarctica is left out**
  (tiles south of 60° S read as water). Tile index: full-land and has-rings bitmaps, a per-row
  rank and one offset per ring tile (~15 KB). Tile cuts are recorded (a prefix byte, a ring
  header bit) but the mask-based coastline never needs them; they are there for sub-project
  5's strokes. **Airports are packed 10-byte records** (24-bit latitude/longitude fractions,
  the ICAO ident in base 37, size class, runway count; all large and medium airports kept),
  runways 8-byte records offset from their airport. Budget for map + airport tables 360 KB;
  as generated at Douglas–Peucker tolerance 1: **342,326 B** (rings 262,442, index 15,004,
  airports 50,810 + 318 runway starts, runways 13,752).
- *Mask and coastline:* one view-wide fixed-point affine for every tile (no water hairlines
  along tile seams); the chart reads the mask as a bilinear field, so the coastline is a
  smooth ~1 px anti-aliased curve rather than the mask's 2 px steps. Map scale: the radius
  spans half the map's width.
- *Map labels:* airport and plane labels are placed largest/nearest first and **dropped
  rather than overlapping** anything already placed (airport labels move up to 12 px to keep
  clear of home, the header, the north arrow and the scale bar first). Five plane labels at
  most, haloed.
- *Seen-today filter:* a 32,768-bit (4 KB) Bloom filter with **three probes** (~0.5 % false
  positives at 2,000 planes a day, against ~6 % for one bit), **in RTC slow memory**
  (`RTC_NOINIT_ATTR`): no heap, and a crash reset on the same day does not recount planes.
- *Counted types:* **any valid ICAO type code counts** (2–4 characters, A–Z/0–9, starting
  with a letter; not `ZZZZ`, `GRND`, `SERV`), including codes missing from the type table,
  which show as the raw code. The set ever seen is capped at **400 codes**, as two sorted
  parallel arrays (4-byte packed code, 2-byte first-sighting day: 6 B a code, 2,400 B).
  Callsign and time are kept for the newest four first sightings only.
- *Persistence:* eight `payload#fnv1a` string keys (`st_day`, `st_air`, `st_typ`, `st_new`,
  `st_first0`…`3`), each written only when its text changed; any unreadable key means start
  empty and log once. Counting runs on the render thread (5 Hz), saving on the network thread.
- *Stats without a clock:* before the time is valid the screen shows the **all-time logbook
  with a "No clock" note** (so a saved day's counts never read as today's), and nothing saves.
  The All-time header is all-time flights and "Since <first day>".
- *Director:* Radar → Map → Board **rotations resume** after an interruption (each keeps its
  place and banks on-screen time; a screen's clock starts when it is actually up). The idle
  Sky ↔ Stats rule waits until planes have been **gone 15 s** (hysteresis). First-sighting
  takeover: after the no-planes and Closest Flight rules, Stats for 20 s from when it
  actually appears, **at most one takeover every 5 minutes**, and a sighting not shown within
  60 s is dropped. Swipe and carousel order: Closest, Radar, Map, Board, Sky, Stats.
- *Gestures:* vertical swipes are a new gesture routed to the current screen; on Stats
  either direction toggles Today / All-time.
- *Theme and fonts:* `Theme` is a value owned by `ScreenApp`, chosen at boot from the
  `theme` setting (or `-DCONTRAIL_FORCE_THEME` in the `cyd2usb_editorial` env; there is no
  on-device settings UI yet). Only the three display roles are theme fonts (Barlow Condensed
  ExtraBold at 40, 28 and 18 px, 34.9 KB of flash); body and small text are Inter in both
  themes. The **40 px hero face is capitals only** (plus digits and a few symbols), so
  callsigns are **upper-cased once at scene build** (`app::fillFlight`) and Editorial headings
  are capitals ("6 FLIGHTS").
- *Bold Editorial colours:* fills use real **livery fill colours** (a field of
  `data::Airline`; the stripe colour where an operator has no single livery). Text on a fill
  is white unless white's WCAG contrast is under 3:1, then black. **Sky View dusk in
  Editorial:** the four elevation bands stay in the sky's cool colours and the sunset is two
  **flat discs** toward the sun, fading into the band as the horizon cools (a warm horizon
  band read as an orange ring round the treeline).
- *Heap measurement:* the soak's targets (≥ 55 KB free, ≥ 18 KB largest block) are defined,
  as in pass 1, on the steady `[health]` line (`heap=`, `largest=`, sampled by the render
  loop every 30 s). Two further figures report the true minimum: `minheap=` (ESP-IDF's
  lowest free heap since boot) and `[heap] low= largest= during …`, the lowest free heap in
  each 30 s window when under 56 KB, sampled on both threads and naming what the network
  thread was doing.
- *Measured:* see §5 and `docs/HARDWARE_TESTING.md` (bench and soak).

## 1. Decisions

- **Map base:** coastlines, lakes and airports, built into the firmware (~300 KB flash).
- **Map drawing:** a 1-bit land mask at half resolution, drawn once when the map opens or the
  location or radius changes; frames read it (approach 1 of 3).
- **Theme scope:** a new palette, fonts and fill style; every screen keeps its layout.
- **Stats content:** today's counts, top airlines and types (today and all-time), and first
  sightings of aircraft types. No all-time records.
- **Theme selection:** a `theme` setting read at boot (`cinematic` default, `editorial`).

## 2. Architecture

New code in `firmware/lib/core/screens/` (screens), `firmware/lib/core/map/` (map data and
mask), `firmware/lib/core/stats/` (counting and persistence); generators in `tools/`.

- **`ScreenArena`**: one static scratch buffer shared by the screens. Only one screen draws
  at a time (pass 1 veil dip), so a screen may use the arena between `enter()` and the next
  screen's `enter()`. It holds the Map's land mask (~2 KB), airport marks and plane states (trails are kept
  outside the arena, see Decisions). Size is set by the largest user (4,096 B), static.
- **Map data (flash, `constexpr`)**, generated by `tools/gen_map.py`:
  - Natural Earth 1:50m land and lakes, clipped into 2°×2° tiles. Each tile holds closed
    rings as delta-coded int16 points in tile-local units (~25 m), plus a flag per edge so
    tile-cut edges are filled but not stroked. A tile index maps (lat, lon) cell → offset.
  - OurAirports large and medium airports: position, ICAO ident, size class, and runways
    (ends as int16 offsets) for large airports.
- **`LandMask`**: projects the tiles around home (equirectangular, scaled by cos(lat) at
  home) into a 120×135 1-bit mask with an even-odd scanline fill; lakes cut holes. Rebuilt
  on map `enter()` and when home or radius changes. Target ≤ 60 ms.
- **`StatsBook`**: counts sightings from the scene (5 Hz), owns the today/all-time tables,
  and saves through `IKeyValueStore` every 10 minutes and at local midnight.
  - Seen-today filter: a 4 KB Bloom filter with three probes keyed by a hash of the ICAO hex
    (~0.5 % false positives at 2,000 planes a day, which only undercounts), in RTC memory.
  - Saved: 24 hourly counts and the date they belong to; top 16 airlines and top 16 types
    with counts, today and all-time; the set of type codes ever seen with the date and
    callsign of each first sighting (cap 400). About 3 KB, as a few string keys.
  - Scene gains a value copy of the stats summary the Stats screen needs.
- **`Theme`** grows beyond colours: font roles (display, title, body, small), `flat` (no
  gradients, glow or dithering), `brandFill` (airline colour fills areas, not only
  stripes), and Map/Stats colours. Screens draw fills through `ui::` helpers that honour
  `flat`, so screen changes stay small. `editorial()` sits beside `cinematicSky()`.
- **Font:** one heavy condensed display face (open licence) at 3 sizes, ~40 KB flash, used
  by Editorial's display roles. Other text stays Inter.

## 3. Screens

### 3.1 Live Map — Night chart (mockup 5A)

- Map in the top 270 px, north up, centred on home, radius from the `radius_nm` setting;
  bottom 50 px strip like Radar's.
- Base: flat water and land colours, a thin coastline drawn where the (smoothed) mask
  changes, lakes as water. Range rings at ½ and full radius, scale bar bottom-left, "N ↑".
- Airports: dot plus ICAO ident, runway lines for large airports when the radius is
  ≤ 50 nm. Labels placed largest first and skipped if they overlap one already placed.
- Planes: heading-rotated glyphs coloured by altitude band (below 10,000 ft warm, 10,000–
  25,000 ft green, above pale blue); trail of the last 8 positions, one every 10 s, in the
  arena; labels (callsign, altitude in hundreds of feet, trend arrow) for the nearest five.
- Strip: the closest plane, or the selected one (callsign, route, airline · type, distance,
  altitude with trend colour).
- Touch: tap a plane (nearest within 20 px) to select it; tap the strip to open it in
  Closest Flight.
- No location: "Set location". No tiles near home: water, rings and planes only.

### 3.2 Stats — Spotter's logbook (mockup 6A)

- Header: "TODAY", clock, flight count (display font), busiest hour and its count.
- 24-hour bar chart: past hours accent, current hour highlighted, future hours faint.
- Top five airlines today: brand stripe, name, bar scaled to the leader, count.
- "Types seen" card: all-time count, "+N today", newest first sighting (type name,
  callsign, time).
- Vertical swipe toggles Today / All-time; All-time shows top types and all-time airline
  totals in place of the chart and today's airlines.
- Counting: once per plane per local day, when first seen within the radius. Airline from
  the callsign prefix (callsigns that are registrations are not counted as airlines).
  Unknown type codes are not counted as types. A first sighting is the first time a type
  code is ever seen.

### 3.3 Bold Editorial on each screen (mockup 7A, brand fields)

- Closest Flight: the sky band becomes a flat field in the plane's airline colour (neutral
  grey when unknown), large white plane, a pale band for the contrail, white compass disc.
  Card near-black with display-font callsign and route, a flat progress bar in the airline
  colour, tiles separated by thin rules instead of rounded boxes.
- Departure Board: each row filled with its airline colour; text black on light colours by
  a contrast check (WCAG luminance); header "N FLIGHTS" in the display font over a white
  rule.
- Radar: same layout, flat: black scope, white rings, single-colour sweep without glow.
- Sky View: the gradient becomes four flat bands by elevation; treeline silhouette and white
  star dots stay; sun and moon flat discs.
- Live Map and Stats: flat greys; airline colours for planes, stripes and bars.

## 4. Director

- Planes in range, no other rule: Radar → Map → Board, 45 s each (was Radar ↔ Board).
- No planes in range: Sky View and Stats alternate, 45 s each.
- A first sighting of a new type: Stats for 20 s (respecting the 20 s dwell), once per type.
- Carousel includes all six screens. Other pass 1 rules and hysteresis are unchanged.

## 5. Performance and memory

- Map frames: the base is a mask lookup per pixel, so damage cost is close to Radar's;
  planes, trails and labels damage only their rects. Mask rebuild only on enter or change.
- Stats: text-hash damage like Closest Flight and the Board; the chart redraws on the hour.
- Editorial is cheaper than Cinematic Sky (flat fills).
- RAM (as built): 87,620 B of static DRAM (`cyd2usb`); idle free heap ~59.8 KB with
  a 22.5 KB largest block on the device. In the 30-minute soak in both themes the steady
  `[health]` heap stayed ≥ 55,964 B in 60 of 61 samples (one 52,072 B sample during a poll),
  largest block ≥ 19,444 B, no trend; the true minimum (`minheap=`) was 35,648 B, and during
  adsb.lol polls the largest block fell as low as 1,780 B (see `docs/HARDWARE_TESTING.md`).
  The 4 KB seen filter is in RTC memory. Two fixes after the soak: Wi-Fi modem sleep is off
  (the access point's multicast bursts landed on top of a poll; the worst largest block during
  a poll rose from 5.6 to 12.3 KB over 10 minutes), and enrichment's TLS connections (~44 KB
  each) close after a minute with no lookup, which frees them in lulls but rarely under steady
  traffic. The rest of the poll-time dip is the prebuilt Wi-Fi and lwIP receive buffering.
- Flash (as built): map + airport tables 342,326 B, display font 34.9 KB; the `cyd2usb` firmware is
  1,785,421 B of the 1,966,080 B app partition, leaving 180,659 B (≥ 100 KB).
- Frames (as built, `cyd_bench`, CPU only): every screen's steady worst is under 35 ms in both
  themes (Live Map 34.6 ms, the busiest; Radar 24.6 ms); the mask rebuild is 0.6–1.9 ms at SFO
  and JFK from 5 to 100 nm. Full redraws (screen switches, 0.4 s under the veil) are 44 ms on
  the Live Map and 49–54 ms on the dusk Sky View, so those switches run at ~17–20 fps. On the
  device (soak) the Live Map's worst non-switch frames reached 56–62 ms, a few per 20 s: the
  frames where nearly every plane changes at once (a trail sample, a poll's corrections). After
  83b35b7 (rows-only strip blocks, trail-aware damage, faster fills, polygons, lines and text)
  live traffic's worst is 21–29 ms per 30 s window; a synthetic 32 fast planes with full trails
  still reaches 42–49 ms on the trail sample (1.8% of frames over 40 ms, from 16.5%).

## 6. Data handling and errors

- Missing or corrupt stats in flash: start empty, log once; never block boot.
- Clock not valid: Stats shows counts without the hour chart and does not save; counting
  starts once the time is valid.
- Unknown airline: neutral stripe or fill. Unknown type: raw code, not counted as a type.
- Map with no location: message, as in §3.1.

## 7. Testing

- Goldens: Map busy / quiet / ocean / no location; Stats today / all-time / empty; all six
  screens in Editorial (busy and empty fixtures).
- Unit tests: tile decoder; land mask at known points (Boston Harbor water, Cambridge land);
  airport label overlap; stats de-duplication, midnight reset, first sightings,
  save/reload round trip, corrupt data; director rules for Map and Stats; Editorial text
  contrast choice.
- Hardware: per-screen benchmark in both themes; 30-minute soak per "Done when".

## 8. Build order

1. `ScreenArena`, `Theme` extension and `ui::` fill helpers; Editorial palette stub.
2. Map data generator and tables; tile decoder; `LandMask`.
3. Live Map screen.
4. `StatsBook` and persistence; Scene stats summary.
5. Stats screen.
6. Display font; Bold Editorial across all six screens.
7. Director changes; benchmark and soak.
