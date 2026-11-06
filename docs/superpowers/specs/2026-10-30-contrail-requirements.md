# Contrail — Product Requirements

Date: 2026-10-30
Status: Approved in brainstorming (decomposed into sub-projects below)

Contrail is ESP32 firmware that shows flights overhead on a "Cheap Yellow Display",
in portrait, with the most striking graphics the hardware allows. It is inspired by
[Overhead](https://github.com/JamesDavid/Overhead) but focuses only on flights.

## Hardware

- Target: ESP32-2432S028R ("CYD 2.8"), 240×320 portrait, ILI9341, XPT2046 resistive
  touch, no PSRAM, 4 MB flash, microSD slot, RGB LED, LDR on GPIO34.
- Two firmware variants: `cyd2usb` (USB-C + micro-USB, inverted colors; the
  developer's board) and `cyd_microusb` (original single micro-USB).
- No audio. The board has an amp but no speaker; sound is out of scope.

## Flight data

Four pluggable providers, selectable in settings, with automatic fallback:

1. Community ADS-B APIs (airplanes.live, adsb.lol, adsb.fi) — free, no key. Default.
2. Local receiver (dump1090 / readsb / tar1090 `aircraft.json` on the LAN).
3. OpenSky Network (OAuth2 client credentials).
4. FlightAware AeroAPI (paid key) — used for enrichment (route/schedule), not polling.

Route enrichment (origin/destination) via adsbdb.com when the provider lacks it.

## Screens

1. Closest Flight (hero card)
2. Radar
3. Live Map (on-device coastlines/roads/airports)
4. Departure Board (split-flap)
5. Look-Up Sky View (fisheye sky dome with sun/moon)
6. Stats & Records (persisted)
7. Military / Emergency Alert (takeover)

## Screen modes (configurable)

- Smart director (default): picks the screen from what is happening.
- Timed carousel.
- Stay put.
Alerts interrupt in every mode.

## Alerts

Each rule has its own enable toggle: emergency squawks (7500/7600/7700 and ADS-B
emergency status), military aircraft, user watchlist (tail, callsign, hex, type),
low/close pass (altitude + radius). Alert output: full-screen takeover, flashing
border, RGB LED colour (red emergency, amber military, blue watchlist), backlight
pulse.

## Look

- Default theme: Cinematic Sky (sun-position-driven gradients, glow, contrails, stars).
- Second theme: Bold Editorial (flat airline-brand colours, large type).

## Configuration

- First boot: Wi-Fi captive portal (`Contrail-setup`).
- Full web UI at `http://contrail.local` (settings, watchlist, alert tests, OTA).
- Quick on-device touch settings (brightness, theme, radius, mode).
- Auto-brightness from the LDR.
- Default units: aviation (ft, kt, nm); switchable to imperial and metric.

## Storage

Works with no SD card (common logos, coarse map in flash). An SD card adds all
airline logos, a detailed local map and aircraft silhouettes.

## Web flasher site (GitHub Pages)

- ESP Web Tools install with CYD variant picker.
- Showcase page with animated screen previews, feature list, hardware links.
- Browser simulator: the real renderer compiled to WebAssembly, fed live flights.
- SD card pack builder (map for your area, logos) downloaded as a zip.

## Project

- Name: Contrail. Hostname `contrail.local`. Setup AP `Contrail-setup`.
- Public GitHub repo, GPL-3.0, GitHub Actions builds release firmware.

## Sub-projects (each gets its own spec → plan → implementation)

1. **Core platform** — HAL, strip renderer, native/SDL build, Wi-Fi, settings
   registry, providers, flight model, scheduler. *(spec: `2026-10-30-contrail-core-design.md`)*
2. Screens & themes + director.
3. Alerts engine.
4. Configuration: web UI, touch settings, OTA.
5. SD card content formats + pack builder.
6. Flasher site, WASM simulator, CI releases.
