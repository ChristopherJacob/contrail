# Contrail Sub-project 6d — Animated Previews

Date: 2026-11-09
Status: Built on `claude/elastic-napier-7775d5`; checked in the browser pane.
Parent: `2026-10-30-contrail-requirements.md` ("Showcase page with animated screen previews"). Builds on 6a (the
showcase page, `--render`, the Pages workflow) and sits beside 6b (the simulator page, which stays the way to *try*
Contrail; 6d only shows it moving).

## Goal

The showcase page shows Contrail moving, without the visitor doing anything: the hero plays a tour of the screens
and each card in the grid loops its own screen, every frame drawn by the firmware's renderer.

Out of scope: audio, a scrubber or per-card controls, animating Stats, logos or a detailed map in the clips (the
screenshots have neither in CI), running WebAssembly on the showcase page.

**Done when:**
1. `tools/previews.sh <sim-binary> <out-dir>` writes `<screen>-<theme>.mp4` for Closest, Radar, Map, Board, Sky and
   Alert in both themes, and `tour.mp4`.
2. The Pages workflow builds them after the screenshots and deploys them in `site/shots/`.
3. On the showcase page the hero plays the tour and the 12 cards loop, playing only near the viewport. Stats stays a
   still.
4. With no JavaScript, `prefers-reduced-motion`, Save-Data or refused autoplay, the page is today's stills.
5. A visitor can pause all motion.
6. `renderDemo` output is unchanged: the goldens and the WASM parity test still pass.
7. The clips total at most 5 MB and none exceeds 600 KB (the tour: 2 MB); the Pages workflow fails otherwise.

## 1. Pieces

| Piece | Where | What |
|---|---|---|
| Clips | `firmware/lib/core/app/DemoClip.{h,cpp}` | a screen's frames, or the tour's, handed to a callback |
| Demo helpers | `firmware/lib/core/app/DemoRender.{h,cpp}` | `addDemoEmergency` (split out of `sceneBusyEmergency`) and `backInTime` become public for `DemoClip`; `renderDemo` itself does not change |
| Simulator modes | `firmware/src/main_sim.cpp` | `--clip <screen> <theme>` and `--tour`: raw RGB24 frames on stdout |
| Encoder | `tools/previews.sh` | sim into ffmpeg, size limits |
| Page | `site/index.html`, `site/previews.js`, `site/style.css` | stills upgraded to videos, the pause button, the sped-up note |
| Pages workflow | `.github/workflows/pages.yml` | ffmpeg installed, the Previews step |
| Test | `firmware/test/test_demo_clip/` | frame counts, determinism, loop closure, the tour's solid ends |

## 2. Clips

`app::renderClip(screen, theme, sink, bufA, bufB, stripH)` returns the number of frames, or -1 for an unknown name or
for Stats. `sink` is called once per finished 240×320 RGB565 frame, in order. The scene comes from the screen, as in
`tools/screenshots.sh`: Sky uses `night`, the others `busy` (Alert: the busy scene with the emergency takeover). All
clips are 30 fps (`dt` = 1/30 s), drawn after a 150-frame warm-up that is not captured. The warm-up covers a full radar
turn, so blip levels are in their steady state, and the 2.5 s Closest contrail, so it has finished growing.

| Screen | Frames | Moves | Why this length | Seam |
|---|---|---|---|---|
| Radar | 120 (4 s) | sweep, blips | one turn (`kTurnS`) | none needed |
| Closest | 145 (4.83 s) | the plane's bob | one bob: 2π/1.3 s = 145.0 frames | none needed |
| Sky | 72 (2.4 s) | beacons | two 1.2 s beacon periods | none needed |
| Alert | 90 (3 s) | rings, border | 1.5 s rings and 1 s pulse meet at 3 s | 15-frame crossfade (the countdown bar shrinks) |
| Map | 240 (8 s) | planes, trails | long enough to see them travel | 15-frame crossfade |
| Board | 240 (8 s) | distances, rows | long enough for rows to swap | 15-frame crossfade |

In Bold Editorial some of these move less (no bob on some layouts, no rings on Alert). They are the same length; a
near-static clip just compresses to very little.

**Time-lapse (Map, Board, the tour):** 10× real time. The flights are stepped along their tracks by the scene helper
`backInTime` with a negative time, then sorted by distance, nearest first, as `SceneBuilder` does on the device. Without
the sort the demo list keeps its original order and the Board's rows never glide. `sceneUpdated` follows each step. Map
and the tour step every 3 frames (1 s of flight time, 0.6 px of plane movement). The Board steps every 15 frames (5 s),
so its numbers can be read. Map starts with the 70 s of trails that `renderDemo` gives it.

**Crossfade:** 15 more frames are drawn than are kept. The first 15 drawn frames are held back and output starts at
drawn frame 15. The clip's last 15 frames blend the clip's continuation into those held frames: output `N - 15 + j` is
drawn frame `N + j` weighted `(14 - j + 1)/16` and held frame `j` weighted `(j + 1)/16`, channel by channel in RGB565.
So the end runs into the start, and the sink still gets frames in order. `DemoClip` holds only the 15 frames in memory
(2.3 MB, simulator only).

## 3. The tour

`app::renderTour(theme, sink, bufA, bufB, stripH)`, Cinematic Sky in the page's use. Closest → Radar → Map → Board →
Sky → Stats → Alert, then back to Closest, each held 3.5 s (105 frames) with the firmware's own fade between them
(`ScreenHost::show`: 0.2 s out, 0.2 s in; no `ScreenApp::tick`, so no director). 820 frames (7 × 117, plus the closing veil frame), about 27 s, flights at 10×
throughout on the `sceneStats` data (busy plus a logbook; the sky stays dusk).

The file starts and ends on the fade's solid veil colour (the theme's near-black): capture starts at the veil point of
a transition into Closest, and stops at the veil point of the last transition back to it. So the loop point falls inside a fade and needs no crossfade. The
emergency is added to the scene from the moment the fade to Alert starts; no screen before Alert shows it, and capture
stops at the fade's veil point, so it is never cleared.

## 4. Encoding

`tools/previews.sh` runs, for each screen and theme, and for the tour:

```
sim --clip <screen> <theme> | ffmpeg -f rawvideo -pix_fmt rgb24 -s 240x320 -r 30 -i - \
    -vf scale=480:640:flags=neighbor,format=yuv420p -c:v libx264 -preset veryslow -crf 23 \
    -movflags +faststart -an <out>/<screen>-<theme>.mp4
```

The 2× nearest-neighbour step comes first so that 4:2:0 chroma lands on whole firmware pixels and small coloured text
does not smear. Measured on the real renderer (2026-11-09, 4 s clips at CRF 26 and 20): Radar 116 and 221 KB, Alert
77 and 120 KB, the others 30 to 90 KB; 8 s at CRF 23: time-lapsed Map 218 KB, Board 62 KB. Lossless WebP was 637 KB for
Radar, and the tour's full-frame fades would cost it far more.

The script fails if a clip exceeds 600 KB, the tour 2 MB or the set 5 MB, and prints each file's size. Simulator
failures fail it too (`set -o pipefail`).

## 5. The page

The HTML keeps today's `<img loading="lazy">` stills. Each that has a clip is wrapped in `<span class="clip">` and gets
`data-clip="shots/radar-cinematic.mp4"`; the hero's gets `data-clip="shots/tour.mp4"`. A deferred `site/previews.js`:

- Does nothing when `prefers-reduced-motion: reduce` or `navigator.connection.saveData` holds, leaving the stills.
- With an `IntersectionObserver` (margin 200 px), adds a `<video muted loop playsinline preload="metadata">` to a
  `.clip` when it nears the viewport, absolutely positioned over the still and `aria-hidden` (the still's `alt` remains
  the description). The video is shown only once its `playing` event has fired, so a failed or refused play leaves the
  still with no flash or broken frame.
- Calls `pause()` when a clip leaves the viewport, `play()` when it returns (unless the visitor paused).
- A "Pause animation" / "Play animation" button by the hero and another by the grid heading act on all clips. The
  state lasts until the page is left; nothing is stored. The buttons are in the HTML with `hidden` and are shown by the
  script, so without it there is no dead button, and they stay hidden when the script leaves the stills.
- `style.css`: `.clip { position: relative; display: block }`, the video filling it (same aspect), `image-rendering`
  left as the stills have it.

Copy: under "Seven screens, two themes", add that Map and Board run ten times faster than life in these clips. The
existing "straight from the firmware's renderer" holds.

The hero's `width`/`height` and the grid's stay as they are, so nothing shifts when a video appears. The hero's still
stays `closest-cinematic.png`. Its tour starts from the veil, a 0.2 s dip the first time.

## 6. Pages workflow

The apt line adds `ffmpeg`. A step after Screenshots: `tools/previews.sh firmware/.pio/build/sim/program site/shots`.
`site/shots/` is already git-ignored and already listed by the workflow's `ls`. The hourly schedule rebuilds them each
run; they take well under a minute.

## 7. Errors

- An encoder or simulator failure, or a clip over its limit, fails the Pages job and that hour's deploy, as a failed
  screenshot does today. The previous deploy stays up.
- A missing or unplayable clip on the page: the still stays (§5).

## 8. Testing

- `firmware/test/test_demo_clip` (native, CI runs it with the others): for one clip of each kind (Radar, Alert, Map,
  Board):
  - the frame count from §2, and -1 for Stats and unknown names;
  - two runs give identical frames;
  - the loop closes: the pixels changed between the last and first frame are no more than twice the largest count
    between neighbouring frames in the clip.
- The Board test also asserts the top row changes during the clip (the sort works).
- The tour: 820 frames; first and last frames the same solid colour; the frame at the middle of each dwell differs from its
  neighbour dwells.
- Goldens and `tools/sim_node_test.mjs` unchanged: `renderDemo` is not edited beyond exposing two helpers.
- `pio test -e test` and `pio run -e sim`; `tools/previews.sh` locally; the showcase page in the browser pane
  (videos appear, pause, off-screen pause, reduced motion, no JS) and at phone width.

## 9. As built

- **Sizes** (`tools/previews.sh`, 2026-11-09, macOS ffmpeg): 2,343 KB in all. Cinematic: Closest 38, Radar 155, Sky 35,
  Alert 82, Map 268, Board 216 KB. Editorial: 23, 117, 15, 21, 270, 235 KB. Tour 862 KB. Encoding all 13 takes 7 s.
- **The fade's colour:** `ScreenHost` dips through the theme's veil colour (near-black, not 0), so the tour's ends are
  that solid colour; the test checks both ends are one colour and the same.
- **Browser checks** were made in the app's browser pane, whose page is hidden, so the observer only ran after a
  repaint. Checked: videos appear near the viewport and show only once playing, both buttons pause and resume all
  in-view clips, off-screen clips pause, no horizontal scroll at phone width, the video lies exactly over its still.
  Reduced motion was checked by re-running the script with `matchMedia` stubbed (no videos added, buttons stay hidden),
  not with a real system setting. The clips have not yet been seen on real Pages hosting or on iOS.
- The WebAssembly build compiles `DemoClip.cpp` too (`find lib/core`). Built locally: `contrail.wasm` is 845 KB, and
  `tools/sim_node_test.mjs` finds all 14 frames the same as the desktop's.
