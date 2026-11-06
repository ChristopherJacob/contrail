# Contrail Sub-project 1 — Core Platform Design

Date: 2026-10-30
Status: Approved (sections 1–2 reviewed with user; sections 3–5 decided autonomously
per user instruction "make simple decisions on your own")
Parent: `2026-10-30-contrail-requirements.md`

## Goal

A foundation every later sub-project builds on: hardware abstraction, a graphics
engine that can deliver "breathtaking" on a no-PSRAM ESP32, a desktop build for
fast iteration and testing, and a live, memory-safe flight data pipeline.

**Done when:**
1. `pio run -e cyd2usb` builds; the CYD2USB boots, runs the Wi-Fi portal on first
   boot, then shows the **core demo screen** (dithered cinematic sky for the current
   sun position, a glowing plane gliding with a contrail, a radar sweep inset, an
   FPS counter, and the 5 nearest live flights) at ≥ 25 fps in animated regions.
2. `pio run -e native` builds an SDL desktop app showing the same demo with live data.
3. `pio test -e native` passes: parsers (all 4 providers + adsbdb), geo math, dead
   reckoning, store merge/eviction, settings, fallback logic, sun position, renderer
   golden images.
4. Free heap never drops below 40 KB during a 30-minute soak on hardware.

## 1. Architecture

```
firmware/
  platformio.ini               envs: cyd2usb, cyd_microusb, native
  src/main_esp32.cpp           ESP32 entry: HAL wiring, tasks
  src/main_native.cpp          SDL entry
  lib/core/                    platform-agnostic C++17, no Arduino headers
    gfx/        Canvas, StripRenderer, gradients+dither, AA shapes, fonts, colour
    model/      Aircraft, AircraftStore, DeadReckoning, Geo
    providers/  IProvider, Community, LocalReceiver, OpenSky, AeroApi, Adsbdb,
                ProviderChain
    services/   Settings (typed registry + JSON schema), EventBus, Scheduler,
                Sun (solar position), Clock/Location
    app/        CoreDemoScreen (temporary; replaced by sub-project 2 screens)
  lib/hal/      interfaces: IDisplay, ITouch, ILed, IBacklight, ILightSensor,
                IHttp, IKeyValueStore, IClock
  lib/hal_esp32/   LovyanGFX ILI9341 (+ invert for cyd2usb), XPT2046 on its own
                   pins, LEDC backlight, RGB LED, LDR ADC, NVS, WiFiClientSecure
  lib/hal_native/  LovyanGFX Panel_sdl, mouse-as-touch, libcurl, JSON-file KV
  test/          Unity tests + fixtures (recorded API responses, golden PNGs)
```

Rule: `lib/core` depends only on `lib/hal` interfaces, ArduinoJson and the C++
standard library. This is what makes the native and (later) WASM builds possible.

### Pin map (from schematic)

| Function | Pins |
|---|---|
| TFT HSPI | SCLK 14, MOSI 13, MISO 12, CS 15, DC 2, BL 21 (PWM) |
| Touch XPT2046 | CLK 25, MOSI 32, MISO 39, CS 33, IRQ 36 |
| SD VSPI | SCK 18, MOSI 23, MISO 19, CS 5 (used in sub-project 5) |
| RGB LED (active low) | R 4, G 16, B 17 |
| LDR | GPIO 34 (ADC) |

## 2. Renderer

- Two 240×32 RGB565 strip buffers (15 KB each, DMA-capable). The CPU renders strip
  N+1 while DMA pushes strip N. 10 strips per frame.
- `Layer`s draw into a `Canvas` that is clipped to the current strip; the canvas
  translates global coordinates so layer code never thinks about strips.
- Dirty-rect tracking: layers report damage; only strips intersecting damage are
  rendered. A static screen costs ~0.
- Time-based animation (`dt` seconds), never frame-count based.
- Primitives: solid fill, vertical / radial gradients with 4×4 Bayer ordered
  dithering (removes RGB565 banding), anti-aliased lines, circles, arcs, filled
  polygons, alpha blending, glows (radial falloff), sprites with 4-bit alpha,
  rotated plane glyph, anti-aliased fonts.
- Sky colour is a per-row LUT recomputed only when sun elevation changes by > 0.25°.
- Golden-image tests: the native build renders layers to an in-memory canvas and
  compares to approved PNGs (tolerance for ±1 LSB).
- SPI at 40 MHz; full-frame ceiling ~30 fps.

## 3. Data flow

```
 Network task (core 0)                         Render task (core 1)
 ┌──────────────────────────┐                 ┌───────────────────────┐
 │ Scheduler tick            │                 │ every frame:          │
 │  ProviderChain.poll()  ───┼─► AircraftStore ◄┼─ snapshot() (mutex,   │
 │  Adsbdb enrich (1 / tick) │   (mutex)       │   copies ≤64 records) │
 │  AeroApi enrich (budget)  │                 │ dead-reckon positions │
 └──────────────────────────┘                 │ draw layers           │
                                               └───────────────────────┘
```

- **One network task**: all HTTP(S) is serialised, so only one TLS session
  (~45 KB) exists at a time.
- **Poll intervals (defaults, settable):** community 5 s, local receiver 1 s,
  OpenSky 30 s (credit budget), AeroAPI on demand only.
- **Community provider** rotates hosts (airplanes.live → adsb.lol → adsb.fi) on
  failure; all use the readsb v2 `/v2/point/{lat}/{lon}/{radius_nm}` shape.
- **Streaming parse** with an ArduinoJson filter so only needed fields are
  materialised; the response is parsed from the socket, never buffered whole.
- **AircraftStore**: fixed 64 slots keyed by ICAO hex; keeps the 64 nearest within
  the configured radius; merges updates; evicts records unseen for 60 s.
- **Aircraft record**: hex, callsign, registration, type, lat, lon, alt_baro_ft,
  gs_kt, track_deg, vrate_fpm, squawk, flags (military, emergency, on_ground),
  category, last_seen_ms, source, plus derived distance_nm, bearing_deg,
  elevation_deg.
- **Dead reckoning**: positions extrapolated from gs/track since last_seen (capped
  at 30 s) so motion is smooth between 1–30 s polls.
- **Enrichment**: adsbdb `/v0/callsign/{cs}` for route (origin/destination ICAO,
  IATA, names); 48-entry LRU cache. AeroAPI only when configured, only for the
  featured aircraft, cached 24 h, daily cap (default 20).
- **ProviderChain fallback**: primary provider; after 3 consecutive failures switch
  to the next enabled provider; retry the primary every 60 s.
- **EventBus** (synchronous, small fixed subscriber list): `AircraftUpdated`,
  `ProviderStatusChanged`, `SettingsChanged`, `NetworkChanged`.

## 4. Settings, Wi-Fi, location, time

- **Typed registry**: each setting declared once (key, type, default, min/max or
  enum, group, label, secret flag). Backed by `IKeyValueStore` (NVS on ESP32, JSON
  file on native). `toJsonSchema()` will drive the web and touch UIs (sub-project 4).
- Core settings: lat, lon, radius_nm (default 25), provider_primary,
  provider_fallback_enabled, local_receiver_url, opensky_client_id/secret,
  aeroapi_key, aeroapi_daily_cap, poll intervals, units, brightness_mode
  (auto/manual), brightness, tz (POSIX string), variant overrides (invert, rotation).
- **Wi-Fi**: WiFiManager portal `Contrail-setup` with custom fields for lat/lon.
  mDNS `contrail.local`. Reconnect with exponential backoff (1 s → 60 s).
- **Location fallback**: if lat/lon unset, geolocate via ip-api.com once; store
  result.
- **Time**: SNTP; ip-api returns the IANA zone, mapped to POSIX via a compact
  bundled table of common zones; unknown → UTC with a settings hint.
- **Sun position**: NOAA solar algorithm (elevation/azimuth), accurate to ~0.1°.

## 5. Error handling & resilience

- Provider failure → keep last data, mark it stale (age shown on demo screen),
  fallback per §3.
- Heap guard: skip a TLS fetch if largest free block < 50 KB; log and retry next
  tick.
- Wi-Fi drop → `NetworkChanged` event; screens show an offline badge; data ages.
- Crash-loop guard: RTC-memory boot counter; 3 crashes within 60 s of boot →
  safe mode (providers disabled, portal reachable) until next clean boot.
- Serial health line every 30 s: heap, largest block, fps, provider, aircraft count.
- Bad settings values are clamped to schema ranges on load.

## 6. Testing

- Unity tests in `native` env (no hardware): parsers against recorded fixtures for
  every provider (including malformed and empty responses), geo math against known
  values, dead reckoning, store merge/eviction/ordering, settings defaults/clamping/
  schema, ProviderChain fallback with a fake clock, sun position against NOAA
  reference values, renderer golden images.
- Hardware smoke checklist (manual, documented in `docs/HARDWARE_TESTING.md`):
  boot, portal, touch corners, LED colours, auto-brightness, 30-min soak heap.

## Out of scope for this sub-project

Final screens, themes, alerts, web UI, OTA, SD assets, flasher site, WASM build.
The `CoreDemoScreen` is a temporary showcase and will be deleted in sub-project 2.
