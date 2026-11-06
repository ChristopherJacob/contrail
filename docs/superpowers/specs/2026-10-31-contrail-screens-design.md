# Contrail Sub-project 2 (pass 1) — Screens, Theme and Director Design

Date: 2026-10-31
Status: Approved in brainstorming (sections 1–3 reviewed with the user)
Parent: `2026-10-30-contrail-requirements.md`
Mockups: private canvas "Contrail screen mockups" (variants picked: 1B, 2A, 3B, 4B)

## Goal

Replace the core demo screen with the first four real screens, a screen system that
transitions between them, and a smart director that picks what to show, all in the
Cinematic Sky theme and structured so a second theme drops in later.

Sub-project 2 is split into two passes:

- **Pass 1 (this spec):** screen system, director, theme structure, and the screens
  Closest Flight, Radar, Departure Board and Look-Up Sky View.
- **Pass 2 (later spec):** Stats & Records, a basic Live Map, and the Bold Editorial theme.

Out of scope: the alert takeover screen (sub-project 3), the detailed SD-card map
(sub-project 5), on-device settings UI (sub-project 4).

**Done when:**
1. The four screens render on the CYD2USB and in the SDL desktop build, matching the
   chosen mockups in layout and spirit.
2. The director runs the rules below with no flicker; touch switches to explore mode
   and it drifts back to auto.
3. Every screen holds ≥ 25 fps with 40 aircraft in range on hardware (measured with
   the on-device benchmark), sun glow included.
4. `pio test -e test` passes, including golden images for every screen in each
   fixture scene.
5. A 30-minute hardware run cycling all screens: no resets; ≥ 55 KB free, ≥ 18 KB
   largest block (TLS).

**Decisions taken while planning and building** (see the plan's "Decisions" list):
the veil-dip transition (§2); the scene holds the nearest 32 aircraft, and fps targets are
still measured with 40+ in range; smaller data tables (~130 aircraft types, ~70 airlines);
the heap target is ≥ 55 KB free (the scene, snapshot buffer and screen tables are static
memory) and the largest block ≥ 18 KB (what a TLS handshake needs); the existing Inter
fonts, no condensed face in pass 1; stale planes are handled by `AircraftStore` (dead
reckoning for 30 s, dropped after 60 s), screens add only the offline badge; a selection
lasts as long as explore mode; with no location Sky View says to hold the screen at
power-on (the Wi-Fi portal is the only way to set it until the web app exists).

## 1. Use model

Ambient by default: the device sits on a desk or shelf and the director chooses the
screen. Touch switches to **explore mode**: swipes move between screens, taps select
planes. After 60 s without touch it returns to auto. The screen-mode setting also
offers **timed carousel** (fixed order, configurable interval, default 30 s) and
**stay put** (current screen only); alerts will interrupt every mode (sub-project 3).

## 2. Architecture

New code in `firmware/lib/core/screens/` and `firmware/lib/core/ui/`; data tables in
`firmware/lib/core/data/`; astronomy in `firmware/lib/core/services/`.

- **`Screen`**: a stack of `gfx::Layer`s plus hooks: `enter()`, `setScene()`,
  `setSelection()` and `onTap(x, y)`, which returns a `NavRequest` (select a flight and/or
  open a screen). *As built:* there is no `leave()` (nothing needed it), touch arrives as
  taps (swipes are handled by `ScreenApp`, not the screen) instead of
  `onTouch(TouchEvent)`, and there is no `interest()`: the director's rules read the
  scene directly (§4).
- **`Scene`**: an immutable snapshot handed to screens at 5 Hz: aircraft (copied from
  `AircraftStore`, sorted by distance), their routes, home location, time, sun and
  moon state, connectivity. Screens read only the scene, never the store.
- **`ScreenHost`**: runs the active screen as one composite layer, routes taps, draws the
  overlays (page dots, offline badge) and runs transitions. Every switch is the same
  *veil dip*: 0.2 s fade out to the theme's veil colour, swap, 0.2 s fade in. Only one
  screen is drawn at a time, so no screen is rendered twice per frame. (The shared-sky
  cross-fades first planned don't fit: only Closest Flight and the Board show the vertical
  sky, Sky View's dome is radial and Radar has its own background. `SkyLayer` is reused by
  Closest Flight and the Board, each limited to its own area.)
- **`Director`**: chooses the active screen (rules in §4) with a 20 s minimum dwell and
  explore mode. *As built:* the alert `interrupt()` hook is deferred to sub-project 3,
  which defines what an alert is. The director holds the screen it last asked for (not
  the host's current one), so a switch is not undone while the host is still fading out.
- **`Theme`**: named colours, gradient stops, fonts and per-screen palettes (Radar's
  phosphor green lives here). Screens never hard-code colours. Cinematic Sky is the
  only theme in pass 1.
- **Shared UI pieces (`ui/`)**: card, stat tile, progress bar, airline stripe,
  compass, label with background, page dots. Built on `Canvas` and `Raster`.
- **Data tables (flash, `constexpr`)**:
  - aircraft type names: ~250 common ICAO type codes → display names ("B789" →
    "Boeing 787-9");
  - airline brand colours: ~80 ICAO airline codes → stripe colour;
  - bright stars: ~150 brightest stars (RA, Dec, magnitude), ~2 KB.
- **`Moon`**: low-precision position and phase (same style and accuracy target as
  `Sun`: within 1° of reference values).
- **Removed**: `DemoScreen`. `CoreApp` keeps the data pipeline and builds the `Scene`.

## 3. Screens

### 3.1 Closest Flight — Instrument card (mockup 1B)

- Top 45 %: the shared live sky; plane glyph at its real heading with a growing
  contrail; a small compass in the corner showing the plane's bearing from home.
- Card: callsign; airline · type · registration; route with origin/destination codes
  and cities, a progress bar and "lands ~HH:MM".
  - Progress = distance flown along the origin→destination great circle ÷ total;
    uses route airport coordinates. Landing time = remaining distance ÷ ground speed,
    shown with "~". Hidden when the plane is below 1,000 ft or speed < 60 kt.
- Three stat tiles: altitude + vertical rate; speed + heading; distance + bearing +
  elevation.
- No route: progress row becomes "Route unknown"; airline from callsign prefix when
  the table knows it.
- Tap the card: next-closest plane (wraps). Selection is kept for 60 s in explore mode.

### 3.2 Radar — Classic sweep (mockup 2A)

- Phosphor scope: rings at ⅓, ⅔ and full radius (radius from settings), crosshair,
  N marker, range labels.
- Sweep rotates once per 4 s. A blip lights when the sweep passes it, then decays
  over one rotation (phosphor fade). Labels (callsign, altitude in hundreds of feet,
  climb arrow) for the nearest five.
- Bottom strip: the closest plane, or the selected one.
- Tap a blip (nearest within 20 px): select it. Tap the strip: open it in Closest Flight.

### 3.3 Departure Board — Sky cards (mockup 3B)

- Dusk-sky header (from the shared sky): "N flights", time.
- Up to six cards, nearest first: airline stripe, callsign, route, airline · type,
  distance, altitude coloured by vertical trend (climbing green, descending orange,
  level grey). The nearest card is highlighted.
- When the order changes, rows glide to their new slots over 0.3 s; new rows fade in.
- Tap a card: open it in Closest Flight.

### 3.4 Look-Up Sky View — Full-screen night (mockup 4B)

- Whole screen is the sky dome: zenith at centre, horizon at the edge, treeline
  silhouette around the border. North up, **east on the left** (looking up).
  Projection: azimuthal equidistant, elevation 90° → centre, 0° → the ellipse that
  fits the screen.
- Real sky for the current time and home location: the sky gradient from the sun's
  elevation, the sun glow when up or near the horizon, real bright stars when the sun
  is below −6° (brightness by magnitude, fading in through twilight), and the moon
  with its correct phase.
- Planes at their real azimuth and elevation, with blinking red beacon and contrail.
  The highest plane gets a label at the bottom; with nothing in range: time, moon
  phase and "Quiet skies".

### 3.5 Common

- Swipe left/right: previous/next screen (explore mode). Page dots at the bottom while
  in explore mode.
- Touch targets ≥ 32 px, kept clear of the top-right corner.
- Offline (no fresh poll for 60 s): small "offline" badge; planes keep dead-reckoning
  and fade out after 2 minutes.

## 4. Director rules

Evaluated each time a new scene arrives; a change needs the current screen to have
been shown ≥ 20 s (except the first screen after boot).

| Situation | Screen |
|---|---|
| A plane within 3 nm and below 15,000 ft (climbing or descending overhead) | Closest Flight |
| A plane with elevation ≥ 45° and the sky is "interesting" (sun elevation < 6°, or moon up) | Look-Up Sky View |
| One or more planes in range, none of the above | Radar, then Departure Board, alternating every 45 s |
| No planes in range | Look-Up Sky View (idle) |
| No home location or no valid time | Radar |

Hysteresis: a plane must leave a rule's threshold by 20 % before that rule stops
applying, so a plane hovering at 3 nm does not toggle screens.

## 5. Performance

Target ≥ 25 fps, 40 aircraft. Per-frame damage stays small on every screen:

- **Radar**: each frame damages the wedge the sweep moved through plus fading blips
  and labels. Rings and crosshair are drawn analytically only inside damaged spans.
- **Sky View**: static parts are cheap to redraw per strip without a pixel cache:
  stars are kept in a list sorted by screen y; the treeline is a precomputed radius
  profile (*as built:* a 2 KB const table in flash, generated by `tools/gen_trees.py`);
  the moon is a small disc. Static parts are recomputed once a
  minute (full redraw then). Per frame, only planes, contrails and beacons damage.
- **Closest Flight, Departure Board**: text-hash damage tracking (existing); the
  plane animates only inside the sky band; board row glides damage only moving rows.
- **Sun glow** (current worst case): limit its damage to the strips it covers and
  only when the sun moves (once a minute); planes crossing it redraw only their own
  rects.
- **Memory**: no large pixel caches. Screen-owned tables total < 8 KB and are allocated
  at boot, not on screen switches, so the heap does not fragment.

## 6. Data handling and errors

- Unknown type code: show the raw code. Unknown airline: neutral stripe colour.
- Route implausible (`routePlausible` false): treat as no route.
- No location or clock: director falls back to Radar; Sky View shows "Set location" /
  "Hold screen at power-on".
- All scene fields are value copies; the render task never touches network-task data.

## 7. Testing

- Golden images (native build): each screen in fixture scenes — busy (40 aircraft),
  quiet (1), empty, dusk, night with moon, no route.
- Unit tests: director rules and hysteresis with a fake clock; progress and landing
  estimate; moon position/phase against reference values; star projection (a known
  star at a known time lands at the expected azimuth/elevation); type-name and airline
  lookups; transition alpha timing.
- Hardware: `cyd_bench` gains a per-screen benchmark; 30-minute cycling run as in
  "Done when".

## 8. Build order

1. Screen system: `Scene`, `Screen`, `ScreenHost`, transitions, `Theme`, `ui/` pieces;
   `DemoScreen` removed.
2. Radar.
3. Closest Flight.
4. Departure Board.
5. Look-Up Sky View (with `Moon` and stars).
6. Director and explore mode; benchmark and soak.

Each step ends with a desktop preview and a hardware check.
