# Contrail 6a — Releases, Install Site and Going Public: Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Tagged versions are built, tested and published by GitHub Actions; anyone can install Contrail from a GitHub Pages site; the repository goes public without the developer's home location.

**Architecture:** Three workflows (CI, release, Pages) around the existing PlatformIO project; a version string injected at build time; a headless screenshot mode in the simulator feeding a plain static site (`site/`) that uses ESP Web Tools with manifests pointing at the latest release's fixed-name files. Going public is a scrub, then a one-commit history, then the visibility switch, gated by the developer.

**Tech Stack:** GitHub Actions (ubuntu-latest, Python 3.12, PlatformIO 6.2.0), esptool `merge_bin`, ESP Web Tools (CDN module), plain HTML/CSS/JS, the SDL simulator built headless.

**Spec:** `docs/superpowers/specs/2026-11-05-contrail-release-site-design.md`.

## Ground rules

- Branch `release-site` (created; the spec is its first commit). Never commit on main; Task 9 is the developer-gated merge and history rewrite.
- Commands from `firmware/`: `pio test -e test` (624 tests pass today), `pio run -e <env>`. One pio command at a time.
- Commits end with a blank line, then `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- The repo `ChristopherJacob/contrail` is private until Task 9. Actions run on it (free minutes are plenty); Pages cannot deploy from a private repo on a free plan, so Task 6's deploy job is proven only after Task 9.
- Never paste or commit the old home coordinates in new text; Task 7 removes them.

## File map

| File | Task | What |
|---|---|---|
| `firmware/scripts/version.py` | 1 | PlatformIO extra script: `-DCONTRAIL_VERSION` |
| `firmware/platformio.ini`, `firmware/src/main_esp32.cpp` | 1 | use it |
| `firmware/src/main_sim.cpp` | 2 | `--render` screenshot mode |
| `tools/screenshots.sh` | 2 | every screen × theme → PNG |
| `.github/workflows/ci.yml` | 3 | tests and builds |
| `.github/workflows/release.yml`, `tools/release_assets.sh` | 4 | tag → Release |
| `site/index.html`, `site/install.html`, `site/style.css`, `site/install.js`, `site/manifest-*.json` | 5 | the site |
| `.github/workflows/pages.yml` | 6 | screenshots + deploy |
| repo-wide | 7 | scrub, untrack `firmware/.cache/` |
| `README.md`, `docs/HARDWARE_TESTING.md` | 8 | install instructions, release process |

---

### Task 1: Version from the tag

**Files:** Create `firmware/scripts/version.py`; modify `firmware/platformio.ini` (`[esp32_base]` and `[env:sim]` extra_scripts), `firmware/src/main_esp32.cpp:37`.

- [ ] **Step 1: Write the script**

```python
# PlatformIO extra script: CONTRAIL_VERSION from the release tag, else `git describe`, else "dev".
import os
import subprocess

Import("env")  # noqa: F821 (PlatformIO's SCons global)


def version():
    ref = os.environ.get("GITHUB_REF_NAME", "")
    if os.environ.get("GITHUB_REF_TYPE") == "tag" and ref.startswith("v"):
        return ref[1:]
    try:
        out = subprocess.run(["git", "describe", "--tags", "--dirty", "--always"], capture_output=True,
                             text=True, cwd=env.subst("$PROJECT_DIR"), timeout=10)  # noqa: F821
        tag = out.stdout.strip()
        if out.returncode == 0 and tag and any(c == "." for c in tag):
            return tag.lstrip("v")
    except (OSError, subprocess.SubprocessError):
        pass
    return "dev"


env.Append(CPPDEFINES=[("CONTRAIL_VERSION", env.StringifyMacro(version()))])  # noqa: F821
```

(`--always` returns a bare hash when there are no tags; the `.` check turns that into "dev".)

- [ ] **Step 2: Wire it in**

`[esp32_base]`: `extra_scripts = pre:scripts/web_embed.py pre:scripts/version.py`. `[env:sim]`: add `extra_scripts = pre:scripts/version.py` (keep any existing entries). `main_esp32.cpp`:

```cpp
#ifndef CONTRAIL_VERSION
#define CONTRAIL_VERSION "dev"
#endif
constexpr const char* kVersion = CONTRAIL_VERSION;
```

Check whether the boot log prints the version; if not, add it to the first boot log line in the existing style. In `main_sim.cpp`, change the user agent's URL to `https://github.com/ChristopherJacob/contrail` and its version to `CONTRAIL_VERSION` (same `#ifndef` fallback).

- [ ] **Step 3: Verify**

Run `pio run -e cyd2usb` and `pio run -e sim`: both build. `strings .pio/build/cyd2usb/firmware.bin | grep -x dev` finds "dev" (no tags yet). Then `git tag v0.0.1-local && pio run -e cyd2usb && strings .pio/build/cyd2usb/firmware.bin | grep 0.0.1-local; git tag -d v0.0.1-local` finds the tag's version. Do not push that tag.

- [ ] **Step 4: Commit** — `git add firmware/scripts/version.py firmware/platformio.ini firmware/src/main_esp32.cpp firmware/src/main_sim.cpp` — "Firmware version from the release tag (dev for local builds)".

---

### Task 2: Screenshot mode in the simulator

**Files:** Modify `firmware/src/main_sim.cpp`; create `tools/screenshots.sh`.

`--render <scene> <screen> <theme> <out.ppm>`: no network thread, no settings file (an in-memory key-value store if CoreApp needs one; reuse whatever `hal::FileStore` can point at a temp path), no SD unless `CONTRAIL_SD` is set. Scenes come from `screens/DemoScenes.h`: `busy`, `quiet`, `night`, `empty`, `stats`. Screens by name: `closest`, `radar`, `board`, `sky`, `map`, `stats`, `alert` (the alert screen via `injectAlert(Emergency)` on the busy scene). Themes: `cinematic`, `editorial`.

- [ ] **Step 1: Implement**

Before the existing `shot` branch, add:

```cpp
// --render <scene> <screen> <theme> <out.ppm>: a fixed demo scene, no network, for the site's screenshots.
if (argc == 6 && std::strcmp(argv[1], "--render") == 0) return renderShot(argv[2], argv[3], argv[4], argv[5]);
```

and a `static int renderShot(...)` above `main` that: builds `scr::ScreenApp`, picks the theme with `scr::themeByName`, fills `screens->scene()` with the named scene function, calls `sceneUpdated()`, shows the screen with `screens->host().showNow(id)` (for `alert`: `injectAlert(scr::AlertKind::Emergency, t)` after the scene), runs 75 frames of `renderer.frame(1 / 30.f)` with `screens->tick(t += 33)`, and writes the PPM with the existing `writePpm`. Unknown names print the valid ones and return 2. Look at `firmware/test/common/GoldenRender.h` for the exact call order the goldens use and copy it; the shots should look like the goldens.

Map and Sky depend on home and the clock in the scene (DemoScenes set them). If a scene leaves the location unset, the map says "Set location": use `quiet` or `busy` for the map.

- [ ] **Step 2: The script**

`tools/screenshots.sh <sim-binary> <out-dir>`: for each of the pairs below and both themes, run the binary and convert with `python3 tools/ppm2png.py` (check its arguments), naming files `<screen>-<theme>.png`.

```
closest busy · radar busy · board busy · sky night · map busy · stats stats · alert busy
```

- [ ] **Step 3: Verify**

`pio run -e sim`, then `tools/screenshots.sh firmware/.pio/build/sim/program /tmp/shots` (scratchpad when run by an agent): 14 PNGs. Look at a few: they match the goldens' look. Run it twice and compare checksums: identical (the mode must be deterministic; if the sky or alert depends on wall time, inject the scene's clock).

- [ ] **Step 4: Commit** — "Simulator: --render a demo scene to a file, for the site's screenshots".

---

### Task 3: CI workflow

**Files:** Create `.github/workflows/ci.yml`.

- [ ] **Step 1: Write it**

```yaml
name: CI
on:
  push:
    branches: ["**"]
  pull_request:

jobs:
  test-and-build:
    runs-on: ubuntu-latest
    defaults:
      run:
        working-directory: firmware
    steps:
      - uses: actions/checkout@v4
        with:
          fetch-depth: 0
      - uses: actions/setup-python@v5
        with:
          python-version: "3.12"
      - name: Cache PlatformIO
        uses: actions/cache@v4
        with:
          path: |
            ~/.platformio
            ~/.cache/pip
          key: pio-${{ runner.os }}-${{ hashFiles('firmware/platformio.ini') }}
      - name: Install PlatformIO and SDL2
        run: |
          pip install platformio==6.2.0 -r ../tools/requirements.txt
          sudo apt-get update && sudo apt-get install -y libsdl2-dev zlib1g-dev
      - name: Native tests
        run: pio test -e test
      - name: Tool self-tests
        run: |
          python -I ../tools/build_map.py --selftest
          python -I ../tools/build_pack.py --selftest
      - name: Build every environment
        run: |
          for e in cyd2usb cyd2usb_editorial cyd2usb_soak cyd_microusb sim cyd_sdload cyd_probe cyd_bench cyd_touch; do
            pio run -e "$e"
          done
```

`tools/requirements.txt` pins Pillow and pyserial (check it). If a test needs `FIXTURE_DIR` paths or anything the Mac has, fix the test or the workflow, not by skipping. If the golden tests fail on Linux by a pixel (float differences), stop and report: that is a finding, not something to paper over.

- [ ] **Step 2: Run it**

Commit ("CI: native tests, tool self-tests and every board build"), `git push -u origin release-site`, then `gh run watch` (or `gh run list --branch release-site` and `gh run view <id> --log-failed`). Fix until green. Record the run time.

---

### Task 4: Release workflow

**Files:** Create `.github/workflows/release.yml`, `tools/release_assets.sh`.

- [ ] **Step 1: The asset script**

`tools/release_assets.sh <version> <out-dir>`, run from the repo root after the builds: for each model in `cyd2usb cyd_microusb`:
- find the build's flash images and offsets: PlatformIO writes `firmware.bin`, `bootloader.bin`, `partitions.bin` in `firmware/.pio/build/<env>/`; boot_app0 is in the Arduino framework package (`~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin`). Offsets for this core: bootloader 0x1000, partitions 0x8000, boot_app0 0xe000, app 0x10000. Read them from the build where PlatformIO exposes them (`pio run -e <env> -t envdump` shows `FLASH_EXTRA_IMAGES`); if they match these constants, use the constants with an assertion in the script.
- `python -m esptool --chip esp32 merge_bin -o <out>/contrail-<model>-install.bin --flash_mode dio --flash_freq 40m --flash_size 4MB 0x1000 bootloader.bin 0x8000 partitions.bin 0xe000 boot_app0.bin 0x10000 firmware.bin` (check the build's flash mode and frequency in envdump and use those).
- copy `firmware.bin` to `<out>/contrail-<model>-app.bin`;
- copies with the version: `contrail-<model>-<version>-install.bin`, `-app.bin`.
Then `python -I tools/build_pack.py --logos assets/logos --out <out>/logos.pak` (check the real arguments), and `sha256sum * > SHA256SUMS` in `<out>`.

- [ ] **Step 2: The workflow**

```yaml
name: Release
on:
  push:
    tags: ["v*"]

permissions:
  contents: write

jobs:
  release:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          fetch-depth: 0
      - uses: actions/setup-python@v5
        with:
          python-version: "3.12"
      - uses: actions/cache@v4
        with:
          path: ~/.platformio
          key: pio-${{ runner.os }}-${{ hashFiles('firmware/platformio.ini') }}
      - run: pip install platformio==6.2.0 esptool -r tools/requirements.txt
      - name: Tests
        working-directory: firmware
        run: pio test -e test
      - name: Build
        working-directory: firmware
        run: pio run -e cyd2usb && pio run -e cyd_microusb
      - name: Assets
        run: tools/release_assets.sh "${GITHUB_REF_NAME#v}" dist
      - name: Release
        env:
          GH_TOKEN: ${{ github.token }}
        run: |
          notes="$(git tag -l --format='%(contents)' "$GITHUB_REF_NAME")"
          prerelease=""
          case "$GITHUB_REF_NAME" in *-*) prerelease="--prerelease";; esac
          gh release create "$GITHUB_REF_NAME" dist/* --title "Contrail ${GITHUB_REF_NAME#v}" \
            --notes "${notes:-Contrail ${GITHUB_REF_NAME#v}}" $prerelease
```

A tag with a hyphen (`v0.0.0-test`) makes a pre-release, so `releases/latest` never points at it.

- [ ] **Step 3: Prove it on a test tag**

Commit ("Release workflow: tag → tests, install images, logos.pak, GitHub Release"), push the branch, then `git tag -a v0.0.0-test -m "Workflow test" && git push origin v0.0.0-test`. Watch the run. Check the Release's file list and sizes (`gh release view v0.0.0-test`); download `contrail-cyd2usb-install.bin` and check with `python -m esptool image_info` that the app at 0x10000 is valid and the version string inside is `0.0.0-test`. Flash it to the board **only if the developer agrees in the conversation**; otherwise stop at the file checks. Then delete the test release and tag: `gh release delete v0.0.0-test --yes --cleanup-tag`.

---

### Task 5: The site

**Files:** Create `site/index.html`, `site/install.html`, `site/style.css`, `site/install.js`, `site/manifest-cyd2usb.json`, `site/manifest-cyd_microusb.json`, `site/favicon.svg`.

- [ ] **Step 1: Manifests**

```json
{
  "name": "Contrail for CYD 2.8 (USB-C)",
  "version": "latest",
  "new_install_prompt_erase": true,
  "builds": [
    { "chipFamily": "ESP32",
      "parts": [{ "path": "https://github.com/ChristopherJacob/contrail/releases/latest/download/contrail-cyd2usb-install.bin", "offset": 0 }] }
  ]
}
```

and the same for `cyd_microusb` ("CYD 2.8 (micro-USB only)"). `new_install_prompt_erase` asks the user; check ESP Web Tools' docs for whether "erase" defaults to off, and say what it does in the guide. Note: ESP Web Tools fetches the binary from the browser, so the URL must allow cross-origin reads. GitHub release downloads redirect to `objects.githubusercontent.com`, which sends `Access-Control-Allow-Origin: *`; verify this in Task 6 with the browser pane. If it doesn't, the Pages workflow copies the latest release's install images into the site and the manifests use relative paths: implement that fallback if the check fails.

- [ ] **Step 2: Pages**

- `install.html`: header and nav (Contrail, Showcase, Install, GitHub); a model picker (two radio cards with a photo-free description: "Two USB ports (USB-C and micro-USB)" / "One micro-USB port", the second with an "untested" tag); the `<esp-web-install-button>` whose `manifest` attribute `install.js` switches with the picker; an unsupported-browser message (`navigator.serial` missing: "Installing needs Chrome or Edge on a computer"); a "No release yet" message when `HEAD` on the latest-release install URL fails (`fetch(url, {method: 'HEAD', mode: 'no-cors'})` cannot tell; use the GitHub API `https://api.github.com/repos/ChristopherJacob/contrail/releases/latest` and show its `tag_name` as "Latest version: …", or the message on 404); the setup guide (numbered steps from the spec §4) with a link to `logos.pak` (`…/releases/latest/download/logos.pak`) and to the README's map pack section.
- `index.html`: hero (name, one line: "Flights overhead, on a $15 touch screen", a Cinematic screenshot, Install button linking to install.html); a screenshot grid (`shots/<screen>-cinematic.png` and `-editorial.png`, 240×320 shown at 2× with `image-rendering: pixelated` off: these are anti-aliased, show them at 1× and 1.5× with smooth scaling; captions); features (from README); hardware (ESP32-2432S028R, where to buy: a search link rather than a shop; what the SD card adds); footer (spec §4 text).
- `style.css`: CSS variables in the Cinematic colours (from `firmware/lib/core/screens/Theme.cpp`: background `#020410`-ish deep blue, text `#F3F5FF`, accent `#FFD29A`), system font stack, max-width 960 px, responsive grid (2 columns on a phone), visible focus outlines, good contrast.
- ESP Web Tools: `<script type="module" src="https://unpkg.com/esp-web-tools@10.1.1/dist/web/install-button.js?module"></script>` (check the current version and its exact path; pin it).
- No build step, no frameworks, no trackers or external fonts.

- [ ] **Step 3: Check locally**

Run Task 2's screenshot script into `site/shots/` (git-ignored: add `site/shots/` to `.gitignore`; Pages makes them). Serve with `python3 -m http.server -d site 8080` (in the background) and open `http://localhost:8080` and `/install.html` in the browser pane: both pages render, the picker switches the manifest, the unsupported-browser message logic works (the pane is Chromium: `navigator.serial` may exist; check the button appears), there are no console errors, and the layout holds at phone width (resize the pane to 375 px). Take screenshots for the controller. Stop the server.

- [ ] **Step 4: Commit** — "Site: showcase and install pages, ESP Web Tools manifests".

---

### Task 6: Pages workflow

**Files:** Create `.github/workflows/pages.yml`.

- [ ] **Step 1: Write it**

```yaml
name: Pages
on:
  push:
    branches: [main]
  release:
    types: [published]
  workflow_dispatch:

permissions:
  contents: read
  pages: write
  id-token: write

concurrency:
  group: pages
  cancel-in-progress: true

jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-python@v5
        with:
          python-version: "3.12"
      - uses: actions/cache@v4
        with:
          path: ~/.platformio
          key: pio-${{ runner.os }}-${{ hashFiles('firmware/platformio.ini') }}
      - run: |
          pip install platformio==6.2.0 -r tools/requirements.txt
          sudo apt-get update && sudo apt-get install -y libsdl2-dev
      - name: Simulator
        working-directory: firmware
        run: pio run -e sim
      - name: Screenshots
        run: tools/screenshots.sh firmware/.pio/build/sim/program site/shots
      - uses: actions/upload-pages-artifact@v3
        with:
          path: site
  deploy:
    needs: build
    runs-on: ubuntu-latest
    environment:
      name: github-pages
      url: ${{ steps.deployment.outputs.page_url }}
    steps:
      - id: deployment
        uses: actions/deploy-pages@v4
```

The sim needs no display for `--render` (SDL is linked but not opened); if it tries to open a window, make `--render` skip SDL init, or run it under `xvfb-run`.

- [ ] **Step 2: Prove the build job**

The repo is private, so deploy will fail until Task 9. Run the workflow on the branch with `workflow_dispatch` (`gh workflow run pages.yml --ref release-site`; dispatch needs the file on the default branch: if so, temporarily add `push: branches: [release-site]` to `on:`, push, check the build job, then remove it in the same task). The build job must pass and its artifact must contain the 14 screenshots (`gh run download <id>`). Expect deploy to fail with a Pages-not-enabled or plan error; record the message.

- [ ] **Step 3: Commit** — "Pages workflow: simulator screenshots and the site".

---

### Task 7: Scrub before going public

**Files:** repo-wide; `.gitignore`.

- [ ] **Step 1: Replace the home location**

KFDK's reference point from OurAirports (check `tools/.cache/map/airports.csv`, ident KFDK: latitude_deg, longitude_deg). Replace the old coordinates (the developer has them; search for them without writing them into new text) in every file, including the README's example, docs, tests, `tools/build_map.py`'s docstring, `firmware/test/fixtures/MAP_SMALL.md`, the plans and specs. Rebuild the fixture: `tools/.venv/bin/python -I tools/build_map.py --lat <KFDK lat> --lon <KFDK lon> --cover 40 --out firmware/test/fixtures/map_small.pak`. Then `UPDATE_GOLDEN=1 pio test -e test -f test_map_screen`, then the whole suite without UPDATE_GOLDEN. Look at the new `map_detail_*` goldens (convert with `tools/ppm2png.py`): the same kind of chart, shifted a few miles. Tests asserting fixture counts or positions may need updating; keep their intent.

- [ ] **Step 2: Scan for anything else private**

`git grep -n -E "192\.168\.|10\.0\.|usbserial|chris@|jacob|/Users/"` and review each hit. Home-network IP addresses and the developer's local paths in docs become neutral (`<board-ip>`, `~/…`); keep the copyright line in LICENSE/README as the developer has it. Look for Wi-Fi names, `api_key`/token values, OpenSky ids. Report every category found and what you did.

- [ ] **Step 3: Untrack the clangd index**

`git rm -r --cached firmware/.cache` and add `firmware/.cache/` to `.gitignore`.

- [ ] **Step 4: Verify and commit**

Full suite, `cyd2usb` and `sim` builds; a search for the old coordinates finds nothing. Commit: "Scrub: KFDK instead of the developer's home; untrack the clangd index".

---

### Task 8: Docs

- [ ] **Step 1:** README: an "Install" section at the top pointing at `https://christopherjacob.github.io/contrail/install.html` and the Releases page; "Building from source" keeps today's steps; a short "Releasing" section (tag `vX.Y.Z` with an annotated message; Actions does the rest; a hyphenated tag makes a pre-release); the trademark sentence near the logos.
- [ ] **Step 2:** `docs/HARDWARE_TESTING.md`: a "Release and install (6a)" section listing what was proven (workflows, test tag, file checks) and what needs the developer (installing from the site onto a board).
- [ ] **Step 3:** Spec status "Implemented" with as-built notes for anything that differed. Commit: "Docs: install from the site, releasing".

---

### Task 9: Going public (developer-gated)

Do none of these steps without the developer's explicit go-ahead in the conversation for that step.

Why a new repository rather than a force-push: GitHub keeps commits reachable by SHA after a force-push (and
the 12+ Actions runs on the private repo link to them), so the old history would still be public once the
repository is. Deleting the repository and creating it fresh removes the old commits, runs, caches and
artifacts with it. Published binaries are only ever the ones CI builds: a local build embeds local paths
(`assert`'s `__FILE__`), so locally built images are never uploaded anywhere.

- [ ] **Step 1:** Final whole-branch review (subagent). Fix findings.
- [ ] **Step 2 (go-ahead: merge):** merge `release-site` into main locally (`--no-ff`), run the suite.
- [ ] **Step 3: Back up the private history** into a separate local clone with no remote:
  `git clone --no-local . ../contrail-private-backup && git -C ../contrail-private-backup remote remove origin`.
  Check it has every branch and tag (`git -C ../contrail-private-backup log --oneline --all | head`).
- [ ] **Step 4 (go-ahead: one-commit history):** `git checkout --orphan public-main && git commit -m "<short
  description>" -m "Co-Authored-By: ..."`, then `git branch -M public-main main`; delete the other local
  branches and tags in this clone (`git branch -D release-site ...`, `git tag -d ...`). Check main alone, not
  `--all`: `git rev-list main` is one commit; `git log -p main` and `git grep -n -E "<old coordinates>"
  $(git rev-list main)` find nothing; the privacy search from Task 7 finds nothing new. Full suite and builds.
- [ ] **Step 5 (go-ahead: replace the GitHub repository):** `gh repo delete ChristopherJacob/contrail --yes`
  (needs the `delete_repo` scope: `gh auth refresh -s delete_repo`, done by the developer), then
  `gh repo create ChristopherJacob/contrail --public --source . --remote origin --push` with main only.
  Check `git ls-remote origin` lists only main. Enable Pages from Actions
  (`gh api -X POST repos/ChristopherJacob/contrail/pages -f build_type=workflow`); CI runs on the push.
- [ ] **Step 6:** Run the Pages workflow (`gh workflow run pages.yml --ref main`); the site loads in the
  browser pane with "No release yet".
- [ ] **Step 7 (go-ahead: first release):** `git tag -a v0.2.0 -m "Contrail 0.2.0: ..."` (summary of
  sub-projects 1-5), push the tag, watch the release and the Pages run it starts; check the site's "Latest
  version" and that the Install button's image loads from the site (HTTP 200) in the browser pane.
- [ ] **Step 8:** Ask the developer to install from the site onto the board (they must be at the computer: Web
  Serial needs a person to pick the port). Record the result in `docs/HARDWARE_TESTING.md`.
- [ ] **Step 9:** The backup clone stays on the developer's machine only; never add a remote to it.
