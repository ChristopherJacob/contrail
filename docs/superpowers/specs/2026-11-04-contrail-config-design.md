# Contrail Sub-project 4 — Configuration Design

Date: 2026-11-04
Status: Built (plan docs/superpowers/plans/2026-11-04-contrail-config.md); touch panel not yet checked by eye
Parent: `2026-10-30-contrail-core-design.md`, `2026-11-03-contrail-alerts-design.md`
Mockups: private canvas "Contrail Settings Mockups" (web page, touch panel in both themes, hold
ring, Updating screen)

## Goal

Change every setting without the setup portal: a web page at `http://contrail.local`, a small
touch panel on the device for the settings people change most, and firmware updates by browser
upload.

Out of scope: a password or login (the home network is trusted), a watchlist editor beyond one
text field, alert-test buttons (the serial `alert` command stays), a status page, settings
backup/restore, the GitHub Releases update check (sub-project 6), automatic rollback of a build
that crashes after it boots.

**Done when:**
1. The page loads from `http://contrail.local` (and the bare IP) on a phone and a laptop, shows
   every setting in `kCoreSettings` grouped as in the mockup, and saves changes.
2. Home can be set by clicking a map; the radius ring follows the radius setting.
3. A time-zone picker sets `tz`; blank means automatic, and a change applies without a restart.
4. Holding anywhere on the screen for 1 s opens the touch panel (brightness, theme, radius,
   screen mode) in each theme, per the mockups.
5. A `firmware.bin` uploaded from the page installs and boots with settings kept; a corrupt or
   truncated file is rejected and the old firmware keeps running.
6. `pio test -e test` passes, including the tests in §8.
7. With the page open and polling, the render loop holds its current frame times, minimum free
   heap stays ≥ 30 KB, and the feature adds ≤ 40 KB of flash.

## 1. Pieces

| Piece | Where | Thread | Job |
|---|---|---|---|
| `SettingsApi` | `lib/core/services/` | any | Pure JSON in/out over `Settings`: schema, values with secrets masked, all-or-nothing apply, restart detection |
| `WebUi` | `src/web/` (ESP32 only) | own task, core 0 | `WebServer` routes, the embedded page, OTA upload |
| Page | `firmware/web/` | browser | One HTML file with inline CSS/JS, built from `/api/schema` |
| `web_embed.py` | `firmware/scripts/` | build | Gzips the page into a generated `WebPage.h` byte array |
| Hold gesture | `lib/core/screens/Gesture.*` | render | Long-press detection with progress for the ring |
| `HoldRing` | `lib/core/screens/Overlays.*` | render | The ring drawn over the current screen while a hold fills |
| `SettingsScreen` | `lib/core/screens/` | render | The touch panel: a full-screen `ScreenId::Settings`, outside the swipe order |
| `UpdateScreen` | `lib/core/screens/` | render | `ScreenId::Update`, full-screen progress while an upload runs |
| `PanelBridge` | `lib/core/app/` | any | Settings → panel values, panel edits → settings |

## 2. HTTP API

All JSON. The server listens on port 80; mDNS already advertises `contrail`.

| Method, path | Body / reply |
|---|---|
| `GET /` | The gzipped page (`Content-Encoding: gzip`), cached by ETag = build id |
| `GET /api/info` | `{"version","built","ip","uptimeS","heap","autoOffsetS"}` (the last only while the zone is automatic and known; the picker's "Automatic" option shows it) |
| `GET /api/schema` | `Settings::toJsonSchema()` output |
| `GET /api/settings` | Values; each secret is replaced by `{"set": true\|false}` |
| `POST /api/settings` | `{"key": "value", ...}` (only changed keys) → `{"ok": true, "restart": bool}` or 400 `{"ok": false, "errors": {"key": "reason"}}` |
| `POST /api/update` | Multipart file upload → `{"ok": true}` then restart, or 400 with a reason |

**Guard.** Both POSTs require the header `X-Contrail: 1`; the settings POST also requires
`Content-Type: application/json`. The server answers no CORS preflight, so another website open
in the same browser cannot send either request. This is not authentication; anyone on the home
network can still use the page.

## 3. Saving settings

- `SettingsApi::apply(json)` parses every value with the existing `Settings::parse` rules first;
  if any fails, nothing is stored and each bad key gets a reason (`"out of range 5–100"`,
  `"not an option"`, `"unknown key"`). Only then does it call `Settings::set` for each.
- An empty string for a secret key means "unchanged". To clear a secret, send `null`.
- `restart` is true when any key in the `hardware` group changed. `WebUi` replies, then
  restarts after 1 s.
- **Locking.** `Settings` already guards its values with a `svc::Mutex`, so the web task can write
  while the render and network threads read. `onChange` listeners run on the writer's thread, so
  they may only set atomics or flags. Saves never run on the render thread (an NVS write can take
  tens of milliseconds): the web task does them, including the touch panel's. Theme, brightness and screen mode get
  `gDisplayDirty` (like the existing `gAlertsDirty`), applied by the render thread in `loop()`.
  `tz` sets `gTzDirty` (below).
- **Time zone.** `tz` holds a POSIX rule (`EST5EDT,M3.2.0,M11.1.0`). The page carries about 30
  common zones (label → rule) and an "Automatic" option, which stores blank. On `gTzDirty` the
  render thread re-reads `tz`: non-blank applies it at once with `setenv`/`tzset`; blank applies
  the last automatic offset again and lets the zone lookup resume. A `tz` that is not in the
  list (typed on the serial console) shows as "Custom: <rule>".
- **Setup portal.** It shrinks to Wi-Fi plus the optional latitude/longitude. The alert fields
  added in sub-project 3 are removed; the web page covers them.

## 4. The page

- Source `firmware/web/index.html`, one file, no framework, target ≤ 25 KB gzipped. `web_embed.py`
  runs as a PlatformIO pre-build script and regenerates `src/web/WebPage.h` when the source
  changes; the generated header is not committed.
- The page fetches `/api/info`, `/api/schema` and `/api/settings`, then draws one card per group
  in this order: Location, Display, Alerts, Flight data, Hardware, Firmware. Controls come from
  the schema type: Bool → checkbox, Enum → select (a segmented control for ≤ 3 short options),
  Int/Float with a range → number field (a slider for radius and brightness), String → text,
  secret → password field with "Saved — type to replace". A setting added to `kCoreSettings`
  appears with no page change.
- Custom widgets replace the generic control for: `lat`/`lon` (the map), `tz` (the picker),
  `quiet_from`/`quiet_to` (time inputs), `loc_set` (hidden; set true whenever home is set).
- Edits are kept locally; the header shows "N unsaved changes" with Save and Discard. Leaving the
  page with unsaved changes asks first.
- **Map.** Leaflet and OpenStreetMap tiles load from the internet in the browser (pinned Leaflet
  version from unpkg). Click moves home; the ring is drawn at the current radius. If Leaflet
  fails to load, the map area says "Map unavailable — enter coordinates below".
- Works at phone width: the side menu stacks above the cards. Colours follow the Cinematic theme
  (mockup).

## 5. Touch panel

- **Hold.** `Gesture` gains `HoldStart`, `HoldProgress` (0–1) and `Hold` events: a touch that
  stays within 12 px of where it began for 1 s. The ring appears after 0.3 s so ordinary taps
  never flash it; a release once the ring shows delivers no tap. Moving away cancels.
- **Panel.** `SettingsScreen` is a full-screen `ScreenId::Settings`, reached only by the hold;
  closing returns to the screen the hold started on.
  Rows: brightness slider (sets manual mode), theme (two options), radius (−/+ in steps of 5 nm,
  5–100), screen mode (three options). Footer: "More at contrail.local".
- A tap applies and saves at once; the slider saves on release. Done, or 30 s with no touch,
  closes the panel. While it is open, swipes do nothing and the director pauses; an emergency
  alert closes it and takes over as usual. A non-emergency alert that starts meanwhile shows when
  the panel closes, for whatever is left of its 30 s.
- While the brightness slider is dragged the backlight follows it before anything is saved.
- Drawn per mockup in each theme, through the strip renderer like any screen.

## 6. Firmware update

- `POST /api/update` streams the upload into the spare OTA slot with `Update.begin`/`write`;
  `Update.end(true)` checks the image. Partitions are already `min_spiffs.csv` (two 1.9 MB app
  slots).
- During the upload the network task pauses provider polls (frees the TLS heap and CPU),
  `ScreenApp` shows `UpdateScreen` with the percentage, and touch is ignored.
- A file larger than the slot, a wrong magic byte, an interrupted upload or a failed check aborts:
  the page shows the reason, the screen returns to normal, polls resume, and the running firmware
  is untouched. On success the board restarts into the new image; settings live in NVS and are
  kept.
- Limit: the stock bootloader has no rollback, so a build that passes the checks but crashes at
  boot needs a USB re-flash.

## 7. Errors

| Case | Behaviour |
|---|---|
| Bad value in a save | 400 with per-key reasons; nothing stored; fields marked on the page |
| Board unreachable mid-save | Page keeps the edits and shows "Couldn't reach Contrail — try again" |
| Web task out of heap for a request | 503; the render loop is unaffected |
| Two browsers saving at once | Last write wins per key; each page refetches values after a save |
| Map library offline | Coordinates fields still work (§4) |
| Upload fails | §6 |

## 8. Tests

- `test_settings_api`: schema matches the registry; secrets are masked and never echoed; a
  mixed good/bad save stores nothing; empty secret leaves it unchanged, `null` clears it; a
  hardware key sets `restart`; unknown keys are rejected.
- `test_gesture`: hold fires at 1 s; ring progress starts at 0.3 s; movement past 12 px cancels;
  a cancelled hold produces no tap; taps and swipes are unchanged.
- Goldens: `SettingsPanel` in Cinematic and Editorial, the hold ring at 60%, `UpdateScreen` at
  62%.
- On hardware (recorded in `docs/HARDWARE_TESTING.md`): save from a phone and a laptop; map and
  time-zone picker; touch panel in both themes; upload the current build; upload a truncated file;
  frame times and minimum heap with the page open; flash size before and after.
