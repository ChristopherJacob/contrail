# Contrail Sub-project 3 — Alerts Design

Date: 2026-11-03
Status: Built (plan docs/superpowers/plans/2026-11-03-contrail-alerts.md)
Parent: `2026-10-30-contrail-core-design.md`, `2026-11-01-contrail-screens-pass2-design.md`
Mockups: private canvas "Contrail screen mockups", row 8 (picked: Cinematic **8A Emergency
beacon**, Editorial **8A Colour field**; 8B Radar lock and Editorial 8B Black + band not built)

## Goal

Notice the planes worth looking up for and say so loudly: a full-screen takeover, a pulsing
border, the RGB LED and a backlight pulse, on every screen and in every director mode.

Out of scope: an on-device or web settings page for alerts (sub-project 4 reuses the keys in
§5), sounds (no speaker), push notifications, alert history.

**Done when:**
1. All four kinds (§2) raise a takeover on the CYD2USB and in the SDL build, drawn per
   mockup 8A in each theme, with the colours in §6.
2. Each plane alerts once per kind; a plane can alert again for a higher kind.
3. The takeover lasts 30 s, a tap ends it early, and the interrupted screen comes back.
4. Quiet hours (§6) soften everything but emergencies.
5. `pio test -e test` passes, including the tests in §8.
6. The Alert screen holds ≥ 25 fps on hardware while pulsing; free heap and largest block
   are unchanged (within 1 KB) from before this sub-project.

## 1. Pieces

All new code is in `lib/core` and runs on the render thread.

| Unit | Files | Job |
|---|---|---|
| Alert rules | `alerts/AlertRules.{h,cpp}` | Pure: one `SceneFlight` + `AlertConfig` → the set of kinds it matches. Also parses the watchlist. |
| Alert engine | `alerts/AlertEngine.{h,cpp}` | Remembers alerted planes, debounces squawks, queues and prioritises, runs the 30 s timer, takes the dismiss tap. Writes `Scene::alert`. |
| Alert lights | `alerts/AlertLights.{h,cpp}` | Pure: (kind, ms into the alert, quiet?, base brightness) → LED colour, backlight level, border strength. Plus `inQuietHours(...)`. |
| Alert screen | `screens/AlertScreen.{h,cpp}` | The seventh screen, `ScreenId::Alert`. Draws `Scene::alert` in the theme's design. |
| Director | `screens/Director.{h,cpp}` | An active alert outranks every rule, rotation and explore mode. |

Memory: about 700 B of static RAM in the engine (16 remembered planes, a queue of 4 by hex,
one held copy of a queued emergency's plane, the alert config), plus the active alert in the scene. The Alert screen needs no arena.

## 2. Rules

`AlertKind` in priority order: `Emergency`, `Watchlist`, `Military`, `LowPass`.

- **Emergency:** an emergency status in the ADS-B data (`emergency` not `"none"`), or squawk
  7500, 7600 or 7700. The squawk only counts once two polls in a row carry it (§4); the
  status counts at once.
- **Watchlist:** any entry matches the plane's registration, callsign, hex (6 hex digits) or
  type code. Case-insensitive; an entry ending in `*` matches anything starting with the
  rest (`N512*`, `RCH*`, `C17` exact).
- **Military:** the feed's military flag (`dbFlags` bit 0, already `SceneFlight::military`).
- **Low pass:** altitude under `alert_low_ft` and distance under `alert_low_nm`. The scene
  holds airborne planes only, so ground traffic never matches.

A kind switched off in settings never matches.

### Data the scene needs

- `SceneFlight` gains `char squawk[5]` and `bool emergencyStatus` (the ADS-B status alone).
  `model::AcFlags` gains `AC_EMERG_STATUS = 16`, set only from the `emergency` field;
  `AC_EMERGENCY` keeps its current meaning for the other screens.
- `Scene` gains `uint32_t pollSeq` (bumped on each successful poll) for the two-poll rule,
  and `SceneAlert alert` (§3).

## 3. Engine

```cpp
struct SceneAlert {
    bool active = false;
    alerts::AlertKind kind;
    char matched[11] = "";   // the watchlist entry that matched, else ""
    SceneFlight flight;      // a copy, so the alert survives the plane leaving the feed
    bool live = false;       // the plane is in this scene (its numbers are current)
    uint32_t startMs = 0, endsMs = 0;
};
```

`AlertEngine::update(Scene&, uint32_t nowMs)` runs after each scene rebuild (5 Hz):

1. For each plane, work out its kinds (§2) and drop the ones already alerted for it.
2. Remember: 16 entries of {hex, kinds alerted, pending emergency squawk at `pollSeq`}. A new
   plane takes the oldest slot when full.
3. Queue new matches (at most 4, highest kind first; when full the lowest is dropped). One
   plane matching several kinds is one alert of its highest kind, and all its kinds are
   marked alerted.
4. If nothing is showing, start the first queued alert that can: 30 s from now. One whose
   plane is out of this scene waits for it (those behind go ahead) and is dropped once it has
   been out of the feed > 30 s, unless it is an emergency.
5. An emergency arriving while another kind shows replaces it at once; the replaced alert
   is not shown again. Anything else waits.
6. While showing, refresh `alert.flight` from the scene when the plane is in it, set `live`.

`dismiss(scene, nowMs)` ends the current alert and starts the next queued one at once.
Turning every kind off clears the queue and the active alert.

## 4. Debounce

Pilots changing code can pass through 7xxx for a moment. An emergency squawk is noted
against the plane with the scene's `pollSeq`; it counts when a scene with a later `pollSeq`
still carries an emergency squawk. A plane whose squawk goes back to normal first clears its
note. The ADS-B emergency status skips this.

## 5. Settings

A new table `kAlertSettings` appended to the core settings, all in the WiFiManager setup
portal as extra fields:

| Key | Type | Default | Portal field |
|---|---|---|---|
| `alert_emerg` | bool | true | checkbox "Emergency alerts" |
| `alert_watch` | bool | true | checkbox "Watchlist alerts" |
| `alert_mil` | bool | true | checkbox "Military alerts" |
| `alert_low` | bool | true | checkbox "Low pass alerts" |
| `watchlist` | string ≤ 160 | "" | text "Watchlist (tails, callsigns, hex or types, comma-separated; * at the end matches a prefix)" |
| `alert_low_ft` | int | 3000 | number |
| `alert_low_nm` | float | 2 | number |
| `quiet_from` | "HH:MM" | "22:00" | text |
| `quiet_to` | "HH:MM" | "07:00" | text |

Watchlist parsing: split on commas, trim spaces, upper-case; skip empty entries and entries
over 10 characters. At most 24 entries kept. Equal `quiet_from` and `quiet_to` turn quiet
hours off. Bad times fall back to the defaults. Settings apply on boot and whenever
`applySettings()` runs.

## 6. Look and lights

Colours by kind (both themes): emergency red `#FF3B3B`, watchlist blue `#4A8BFF`, military
amber `#FFB020`, low pass green `#3DDC84`. The LED uses the same colour at full saturation.

**Screen (mockup 8A).** Cinematic *Emergency beacon*: glowing border in the kind colour,
the plane at its real track inside rings, callsign as hero, a pill with the reason
(`7700 · GENERAL EMERGENCY`, `MILITARY`, `WATCHLIST · N512*`, `LOW PASS`), a stats row
(distance, altitude, speed), a 30 s countdown bar. Editorial *Colour field*: the top half
solid kind colour with the reason huge (the squawk for emergencies, else the kind name),
the black lower half with callsign, type and stats, the same countdown bar. A plane no
longer `live` keeps its last numbers, dimmed.

**Pulse.** `p(t) = 0.5 − 0.5·cos(2πt / 1000 ms)`, one soft cycle a second, far below the
3 flashes/s photosensitivity limit. Border strength, LED brightness and backlight all follow
it: border and LED from 25 % to 100 %, backlight from 60 % to 100 % of its current level (so
it still works with the deferred auto-dimming). Only the border's strips are redrawn for the
pulse.

**Quiet hours** (local time, wrapping past midnight): border steady at full, LED steady at
20 %, no backlight pulse. The takeover itself still shows for 30 s. **Emergencies ignore
quiet hours.** With the clock not yet set, quiet hours do not apply.

After an alert: LED off, backlight back to its level.

## 7. Director and touch

- `Director::decide` returns `ScreenId::Alert` while `scene.alert.active`, before every
  other rule, in Auto, Carousel and Stay modes, and while exploring.
- When the alert ends, the director returns the screen it would have chosen; rotations keep
  their place (the alert counts as an interruption, as a rule does today). Explore mode, if
  it was running, continues with its remaining time.
- A tap on the Alert screen calls `AlertEngine::dismiss` and is not passed on. It does not
  count as a touch for explore mode. No other gestures on the Alert screen.
- Alerts do not run in portal mode or safe mode (no scene).

## 8. Testing

Native unit tests (`pio test -e test`):

- `test_alert_rules`: each kind; watchlist exact and `*`, case, hex, type, long and empty
  entries; low pass limits; kinds switched off.
- `test_alert_engine`: once per plane per kind; upgrade to a higher kind; two-poll squawk
  rule and its reset; ADS-B status immediate; priority order; emergency replaces; queue
  limit; stale queued alerts skipped (emergencies kept); memory eviction; 30 s timeout;
  dismiss; all kinds off.
- `test_alert_lights`: pulse values at 0, 250, 500 ms; quiet vs not; emergency in quiet
  hours; `inQuietHours` across midnight, equal times, clock not set.
- `test_director`: alert outranks rules and explore mode in every mode; resume afterwards.

Goldens: the Alert screen in both themes × four kinds, plus `assertNoStale` across several
pulse frames.

On hardware: a serial command (always built in; plan decision 8) `alert <emerg|watch|mil|low>` injects a fake
alert to check the LED, backlight and border by eye; check frame times and heap per Done
when 6.
