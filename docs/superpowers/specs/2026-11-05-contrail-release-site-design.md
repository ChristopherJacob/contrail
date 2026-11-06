# Contrail Sub-project 6a — Releases, Install Site and Going Public

Date: 2026-11-05
Status: Implemented on `release-site`, except going public (§6 steps 4-5) and the install onto the board, which wait
for the developer. See §9 for what differs from the design.
Parent: `2026-10-30-contrail-requirements.md` ("Web flasher site", "Project": public repo, GPL-3.0,
Actions builds release firmware). Sub-project 6 is split: **6a** (this), 6b browser simulator (WebAssembly),
6c pack builder in the browser, 6d animated previews on the showcase page.

## Goal

Anyone with a CYD can install Contrail from a web page in Chrome or Edge, and every tagged version is built,
tested and published by GitHub Actions. The repository becomes public, without the developer's home location.

Out of scope: the simulator, the browser pack builder, animated previews (6b-6d), a custom domain, OTA from
GitHub (the device's own web update stays manual), signing.

**Done when:**
1. Every push and pull request runs the native tests and builds every board environment in Actions.
2. Pushing a tag `vX.Y.Z` builds `cyd2usb` and `cyd_microusb` with that version and publishes a GitHub
   Release with, per model, a combined install image and the plain app image, plus `logos.pak`.
3. The firmware reports the tag's version (`/api/info`, the web page, the boot log); local builds report
   `dev`.
4. The site on GitHub Pages has an Install page (ESP Web Tools, model picker, setup guide) and a showcase page
   (screenshots of every screen in both themes, features, hardware), with the licence, trademark and data
   credits in the footer. The site needs no edit for a new release.
5. Installing from the site onto the developer's board works end to end.
6. The repository is public, its history holds no home location, and Pages is on.

## 1. Pieces

| Piece | Where | What |
|---|---|---|
| CI workflow | `.github/workflows/ci.yml` | tests and all board builds on push and pull request |
| Release workflow | `.github/workflows/release.yml` | on `v*` tags: builds, combined images, `logos.pak`, the Release |
| Pages workflow | `.github/workflows/pages.yml` | on main: screenshots from the simulator, then the site deployed |
| Version | `firmware/platformio.ini`, a small `tools/version.py` extra script | `CONTRAIL_VERSION` from the tag or `git describe`, else `dev` |
| Screenshot mode | `firmware/src/main_sim.cpp` | headless, a fixed scene and screen and theme, no network |
| Site | `site/` | `index.html` (showcase), `install.html`, `style.css`, manifests |
| Scrub | repo-wide | the home location replaced by KFDK's |

## 2. Version

- `tools/version.py` (a PlatformIO extra script) sets `-DCONTRAIL_VERSION="..."`: `GITHUB_REF_NAME` without the
  leading `v` on a tag build; otherwise `git describe --tags --dirty` when tags exist; otherwise `dev`.
- `main_esp32.cpp`'s `kVersion` comes from `CONTRAIL_VERSION`. Nothing else changes in the firmware.

## 3. Workflows

- **CI** (`ubuntu-latest`): Python 3.12, PlatformIO pinned (the version used locally, 6.2.0), the PlatformIO
  cache keyed on `platformio.ini`, SDL2 dev packages for the `sim` build, then `pio test -e test` and
  `pio run` for `cyd2usb`, `cyd_microusb`, `cyd2usb_editorial`, `sim`, `cyd_sdload` and the diagnostic envs.
  `tools/build_map.py --selftest` and `tools/build_pack.py`'s self-test run too.
- **Release** (tag `v*`): the same tests; then per model `pio run -e <env>`, and a combined image made with
  `esptool.py merge_bin` from the bootloader (0x1000), partition table (0x8000), boot_app0 (0xE000) and app
  (0x10000), offsets taken from the build, not hard-coded where the build reports them. Files:
  `contrail-<model>-<version>-install.bin`, `contrail-<model>-<version>-app.bin`, `logos.pak` (built by
  `tools/build_pack.py` from `assets/logos`), and `SHA256SUMS`. A GitHub Release is created with notes from
  the tag's annotation (or "Contrail <version>").
- **Pages** (push to main, and after a release): builds `sim`, runs the screenshot mode for every screen in
  both themes, converts to PNG (`tools/ppm2png.py`), copies the site and the screenshots, and deploys with
  `actions/deploy-pages`. Manifests point at release files by URL (§4), so the site does not rebuild on
  release, but the workflow may also run on `release: published` to refresh the version shown.

## 4. The site

- **Install page:**
  - ESP Web Tools (`esp-web-tools` pinned, loaded from a CDN as a module) with one manifest per model:
    `manifest-cyd2usb.json`, `manifest-cyd_microusb.json`, each naming the chip family ESP32 and the combined
    image at offset 0. Image URLs are `https://github.com/<owner>/contrail/releases/latest/download/<file>`.
    Asset names therefore carry no version; versioned names are kept alongside for archives:
    `contrail-cyd2usb-install.bin` and so on. (This refines §3's names.)
  - Model picker: "CYD 2.8 with USB-C" (`cyd2usb`, two USB ports, colours inverted) and "CYD 2.8, micro-USB
    only" (`cyd_microusb`), the second labelled "not yet tested on this board".
  - A browser check: Web Serial needs Chrome or Edge on a desktop; other browsers get a short message.
  - Setup guide: plug in, Install, pick the port; on first boot join `Contrail-setup` and set Wi-Fi and
    location; optional SD card with `logos.pak` (link to the latest release) and the map pack (README link
    until 6c).
- **Showcase page:** a header, a grid of screenshots (Closest Flight, Radar, Live Map, Board, Sky, Stats,
  Alert; Cinematic and Editorial side by side), the feature list from the README, the hardware (the board,
  a link to buy, what the SD card adds), and a link to the Install page and the GitHub repo.
- **Look:** dark, Cinematic Sky colours (deep blue, the peach accent), Inter from the repo's font licence or
  a system font stack; responsive to phone width.
- **Footer:** GPL-3.0; "Airline logos are trademarks of their respective owners and are shown only to
  identify airlines; Contrail is not affiliated with any airline."; "Map data: Natural Earth, OurAirports,
  GeoNames"; the Inter licence.

## 5. Screenshot mode

`main_sim.cpp` gains `--scene <name> --screen <id> --theme <cinematic|editorial> --out <file.ppm>`: no
network, a fixed scene from the same fixtures the golden tests use (moved from `test/` into a place both can
include if they live only under `test/`), a fixed clock (a daytime sky for Sky), a few seconds of frames,
then one frame written. The live `--shot` mode stays. The Live Map shot uses the built-in map (no pack in
CI).

## 6. Going public

1. Replace the developer's home location everywhere with KFDK's reference point (39.4176, -77.3743,
   from OurAirports) in docs, tests, tool examples and the README; rebuild `firmware/test/fixtures/map_small.pak`
   and the `map_detail_*` goldens; check the goldens show the same kind of chart.
2. Search the tree for anything else private: e-mail addresses other than the commit author's, IP addresses
   of the home network (192.168.x.x in docs and logs), Wi-Fi names, serial port names that identify the
   machine (harmless; keep), API keys.
3. Untrack `firmware/.cache/` (clangd index) and ignore it.
4. With the developer's go-ahead at each step: back up the private history into a separate local clone with
   no remote; make main one commit of the cleaned tree (an orphan branch) and check main alone holds nothing
   private; delete the GitHub repository and create it again as public with only that main. A force-push to
   the existing repository is not enough: GitHub keeps old commits reachable by SHA, and the private repo's
   Actions runs (with their logs, caches and artifacts) link to them, so making that repository public would
   expose the old history. The old repository has been private throughout, so nothing old has been public.
5. Enable Pages from Actions on the new repository; run Pages; push a first tag (`v0.2.0`).
6. Only CI-built binaries are published: a local build embeds local paths through `assert`'s `__FILE__`.

## 7. Errors

- A failing test or build fails the workflow; no Release is made.
- The Install page with no release yet says "No release yet" (a HEAD request to the latest-release URL).
- An unsupported browser sees the message instead of the button.

## 8. Testing

- Workflows are proven on the private repo first with a test tag (`v0.0.0-test`, deleted afterwards).
- The site is checked locally and on Pages in the browser pane: manifests load, the button appears, release
  files download (HTTP 200 through the redirect), screenshots load.
- The installer is proven by installing onto the developer's board from the site, which needs the developer
  at the computer. Their settings survive if ESP Web Tools does not erase (the "erase" choice is off by
  default; the guide says so).

## 9. As built

- **Version:** `firmware/scripts/version.py` (not `tools/`), a PlatformIO `post:` script that puts
  `CONTRAIL_VERSION` on the project's sources only, so a new version recompiles `main_*.cpp`, not the libraries.
  `git describe` output without a dot (no tags yet) gives `dev`. The ESP32's and the simulator's User-Agent
  carry the version and the repo URL.
- **Screenshot mode:** `--render <scene> <screen> <theme> <out.ppm>` (positional, as the plan has it) draws in
  the golden tests' order: no clock, alert engine, network, settings file or SDL window; the alert is set into
  the busy scene as the alert goldens do; the Map gets the goldens' 70 s of trails and the Sky 100 frames. The
  Sky shot is the night scene. 12 of the 14 shots are byte-identical to a golden.
- **Goldens on Linux:** clang on Apple arm64 fuses `a*b+c` into one FMA by default, GCC on x86-64 does not; the
  rounding moved a few anti-aliased trail pixels. The `test` and `sim` envs build with `-ffp-contract=off`;
  the ESP32 builds are unchanged. CI uploads `.actual.ppm` files when a golden test fails.
- **Workflows:** the first ESP32 build writes `src/web/WebPage.h`, which changes PlatformIO's project checksum
  and made the next `pio` command wipe `.pio/build`; the workflows generate it before any `pio` run. CI caches
  `firmware/.pio/build` (about 3 minutes warm instead of 12); releases always build from scratch. The asset
  script checks the flash layout against `pio run -t envdump`. Release names have no version as well as the
  versioned copies (§4). esptool is pinned to 4.8.1.
- **Install images from the site:** GitHub's release downloads send no `Access-Control-Allow-Origin`, so ESP
  Web Tools cannot fetch them from the page. The Pages workflow copies the latest full release's install
  images into `site/firmware/`, stamps the version into the manifests and writes `firmware/release.json`; the
  manifests use relative paths and the Install page reads that file for "Latest version" or "No release yet"
  (not the GitHub API). A release made with `GITHUB_TOKEN` fires no `release` event for other workflows, so the
  release workflow starts Pages itself after a full release. ESP Web Tools 10.4.0 from jsDelivr.
- **Tokens:** every workflow defaults to `contents: read`. The release's test-and-build job checks out without
  keeping the token and hands `dist/` to a publish job, the only one with `contents: write` (and `actions:
  write` to start Pages); it fetches the tag by itself for the notes (the annotation's subject and body, never
  a signature; "Contrail <version>" for a lightweight tag). Pages' deploy job alone has `pages: write` and
  `id-token: write`, and a deploy in progress is never cancelled. Pages fails on any error reading the latest
  release except a 404 ("No release yet"), and copies only the two unversioned install images.
- **Scrub:** KFDK's point used as 39.4176, -77.3743 (OurAirports: 39.417598724399994, -77.3742980957).
  `firmware/.cache` is untracked.
- **Later:** Actions warns that `actions/checkout@v4`, `setup-python@v5`, `cache@v4`, `upload-artifact@v4`
  and `deploy-pages@v4` target Node 20 (deprecated; they are forced onto Node 24), and that `ubuntu-latest`
  moves to Ubuntu 26 from October 2026. Moving to the actions' newer majors and checking the build on the new
  image are left for later.
