# Contrail

Flights overhead, beautifully, on an ESP32 "Cheap Yellow Display" that costs about $15.

Contrail turns an ESP32-2432S028R (CYD 2.8") into a portrait flight tracker with a
cinematic, sun-aware sky, smooth 30 fps animation and live ADS-B data from free
community feeds, your own receiver, OpenSky or FlightAware. Seven screens (Closest Flight,
Radar, Live Map, Board, Sky, Stats and alerts) in two themes, set up from a phone, with an
optional SD card for airline logos and a detailed map.

## Install

Open **https://christopherjacob.github.io/contrail/** in Chrome or Edge on a computer, plug the board in
with a USB data cable, pick your board on the [Install page](https://christopherjacob.github.io/contrail/install.html)
and click Install. On first boot, join the **Contrail-setup** Wi-Fi network to set your home Wi-Fi and
location; after that every setting is on the board's web page at `http://contrail.local`.

Every version's files are on the [Releases page](https://github.com/ChristopherJacob/contrail/releases):
per board, `contrail-<board>-install.bin` (the whole flash image, written at offset 0) and
`contrail-<board>-app.bin` (the app alone, for the Firmware card's update on the board's web page), plus
`logos.pak` for the SD card and `SHA256SUMS`. The site serves its own copies of the latest release's install
images: GitHub's release downloads do not allow the cross-origin reads the in-browser installer needs, so the
Pages workflow copies them next to the site's manifests on every release.

## Hardware

| Part | Notes |
|---|---|
| ESP32-2432S028R (CYD 2.8") | Either the original micro-USB board or the CYD2USB (USB-C + micro-USB); about $15 |
| microSD card (optional) | Airline logos (see [SD card and logos](#sd-card-and-logos)) and a detailed Live Map (see [Detailed map](#detailed-map)) |

## Building from source

Requires [PlatformIO](https://platformio.org/) (`brew install platformio`).

```bash
cd firmware
pio run -e cyd2usb -t upload     # CYD2USB (USB-C) boards
pio run -e cyd_microusb -t upload  # original micro-USB boards
pio device monitor               # serial log, 115200 baud
```

On first boot, join the **Contrail-setup** Wi-Fi network and pick your home network.
Latitude and longitude are optional; without them Contrail locates itself by IP.
Hold a finger on the screen while it boots to reopen the Wi-Fi setup.

If colours look wrong (inverted or red/blue swapped), the display settings
`disp_driver`, `disp_invert`, `disp_bgr` and `disp_rotate` fix it, in the hardware section of the board's
web page.

## SD card and logos

With a microSD card holding `logos.pak` (on each release, or built as below), Closest Flight, the Departure
Board and the Alert show each airline's real logo. Airline logos are trademarks of their respective owners and
are shown only to identify airlines; Contrail is not affiliated with any airline. Without one (or for an airline the pack lacks) they show a drawn tail fin in the
airline's colours, so the card is optional.

The [SD card page](https://christopherjacob.github.io/contrail/sd.html) includes `logos.pak`.

```bash
tools/.venv/bin/python -I tools/build_pack.py --logos assets/logos --out logos.pak
```

Copy `logos.pak` to `/contrail/logos.pak` on a FAT16 or FAT32 card (MBR partition or a superfloppy with no
partition table; exFAT and GPT are not supported). The reader matches 8.3 short names only, which that path
already fits. Or write it over USB without taking the card out: flash the `cyd_sdload` helper env and run
`tools/sd_upload.py` (see [HARDWARE_TESTING](docs/HARDWARE_TESTING.md#sd-card-and-logos-5a)), then flash
`cyd2usb` again. The web page's Firmware card shows the logo count, or why the card or pack was refused.

### Detailed map

With `map.pak` on the card, the Live Map shows the area around home in detail: coastlines and lakes,
rivers, roads, towns and every public airfield with its runways, at any radius up to 100 nm, in both
themes. Without it (or when the view reaches past the pack) the map is the built-in one.

The easiest way to get it is the [SD card page](https://christopherjacob.github.io/contrail/sd.html): pick home,
and it builds `contrail-sd-card.zip` (`map.pak` and `logos.pak`) in the browser. Or build the pack yourself. The pack is
built for one home and covers 250 nm around it, about 0.55 MB:

```bash
tools/.venv/bin/python -I tools/build_map.py --lat 39.4176 --lon -77.3743 --out map.pak
```

The first run downloads Natural Earth, OurAirports and GeoNames data into `tools/.cache/map/` (about
106 MB) and reuses it after that (`--refresh` downloads it again). Copy `map.pak` to `/contrail/map.pak`
with a card reader, or over USB as for `logos.pak`:
`tools/.venv/bin/python -I tools/sd_upload.py --port <port> --file map.pak --dest /contrail/map.pak`
(it replaces an existing `map.pak`), then flash `cyd2usb` again. After moving the board, build the pack
again for the new home: the web page's Firmware card says "Map: built-in (home is outside the map pack;
rebuild it)" until you do. The board draws the chart for the current home, radius and theme (under a second
at 25 nm) and keeps it on the card, so later visits and restarts open it at once. Only the last chart is kept:
changing the radius or theme and back draws it again.

`tools/map_preview.py map.pak --radius 25 [--theme editorial] -o preview.png` renders a pack the way the
board draws it, for checking one on the computer.

Map data: Natural Earth (public domain), OurAirports (public domain), GeoNames (CC BY 4.0).

## Desktop simulator

The same rendering and data code runs on macOS/Linux in an SDL window:

```bash
brew install sdl2
cd firmware
pio run -e sim && .pio/build/sim/program
.pio/build/sim/program --shot frame.ppm 10   # headless: 10 s of live data, save the frame
.pio/build/sim/program --render busy map editorial map.ppm   # a fixed demo scene, no network
```

`tools/screenshots.sh <sim-binary> <out-dir>` renders every screen in both themes as PNGs (the site's
screenshots). `tools/previews.sh <sim-binary> <out-dir>` encodes the showcase page's animated clips (needs ffmpeg).

Settings live in `~/.contrail/settings.json` (same keys as the device), for example
`{"lat": "51.47", "lon": "-0.45", "loc_set": "true"}`. The simulator's "SD card" is a folder,
`CONTRAIL_SD` (default `sim_sd/` in the working directory), holding `contrail/logos.pak` and `contrail/map.pak`.

### Simulator in the browser

The [Simulator page](https://christopherjacob.github.io/contrail/sim.html) runs the same firmware, compiled to
WebAssembly, on flights recorded over a handful of airports; the Pages workflow records them again every hour. To build it
locally (emscripten 7.0.0 on `PATH`, and the site's map data from `tools/sd_site.sh site`):

```bash
python3 -I tools/record_sky.py tools/sim_places.json /tmp/rec --replies 6   # a minute of flights per place
python3 -I tools/sim_site.py site /tmp/rec
npx http-server site                                                          # then open /sim.html
```

The server must answer Range requests (the maps and logos are read in pieces); `python3 -m http.server` doesn't.

## Python tools

The generators and pack tools run from a virtualenv in `tools/.venv`:

```bash
python3 -m venv tools/.venv && tools/.venv/bin/pip install -r tools/requirements.txt
```

## Tests

```bash
cd firmware
pio test -e test                       # unit + golden-image tests, no hardware needed
UPDATE_GOLDEN=1 pio test -e test       # accept intentional rendering changes
tools/.venv/bin/python tools/ppm2png.py firmware/test/golden/demo_day.ppm   # view a golden
```

The test env links zlib (the FAT reader tests unpack gzipped disk images from `firmware/test/fixtures`).
macOS has it already; on Linux install the headers first (`sudo apt install zlib1g-dev` on Debian/Ubuntu).
`tools/make_fat_fixtures.sh` rebuilds those images, on macOS only.

GitHub Actions runs the tests, the tool self-tests and every board build on each push and pull request
(`.github/workflows/ci.yml`). Its `pack-builder` job checks the SD card page's map: `tools/map_regions.py
--selftest` builds packs for several homes from the site's map regions and from the sources and compares them
byte for byte, and `tools/sd_node_test.mjs` does the same build in Pyodide under Node.

## Releasing

Tag the commit with an annotated tag and push it:

```bash
git tag -a v0.3.0 -m "Contrail 0.3.0: what changed"
git push origin v0.3.0
```

The release workflow (`.github/workflows/release.yml`) runs the tests, builds `cyd2usb` and `cyd_microusb`
with the tag's version (local builds say `dev`), makes the install and app images, `logos.pak` and
`SHA256SUMS`, and publishes a GitHub Release with the tag's message as its notes. It then starts the Pages
workflow, which rebuilds the site with fresh simulator screenshots and the new install images. A tag with a
hyphen (`v0.3.0-rc1`) makes a pre-release: the site keeps offering the last full release.

## Data sources

| Source | Key needed | Notes |
|---|---|---|
| Community (adsb.lol → adsb.fi → airplanes.live) | No | Default; rotates hosts on failure |
| Local receiver | No | Any dump1090 / readsb / tar1090 `aircraft.json` URL on your LAN |
| OpenSky Network | Optional OAuth client | Anonymous works with lower limits |
| FlightAware AeroAPI | Yes (paid) | Used only to enrich the featured flight's route; daily cap |

Routes come from [adsbdb](https://www.adsbdb.com/), checked against the aircraft's
position so stale callsign routes are hidden.

## Layout

```
firmware/lib/core        platform-independent: renderer, fonts, model, providers, settings, app
firmware/lib/core/assets SD packs: FAT16/32 reader, pack format, logo store
firmware/lib/core/map    built-in base map; the map pack reader, chart painter and saved chart
firmware/lib/hal         hardware interfaces
firmware/lib/hal_esp32   CYD implementation (LovyanGFX, WiFi, NVS, SD over SPI)
firmware/lib/hal_native  SDL + libcurl implementation for the simulator
firmware/lib/hal_wasm    browser simulator build: replayed flights, SD files read by HTTP Range, worker glue
assets/logos             airline logos for logos.pak (third-party trademarks, see Licence)
tools/                   generators: fonts, CA bundle, map, stars, trees, gen_shapes.py (aircraft shapes)
tools/build_pack.py      assets/logos -> logos.pak
tools/build_map.py       Natural Earth, OurAirports and GeoNames around home -> map.pak
tools/map_preview.py     map.pak -> PNG, the detailed map as the board draws it
tools/map_regions.py     the map sources in 5-degree cells for the SD card page (site/mapdata)
tools/sd_card.py         map regions -> contrail-sd-card.zip, as the SD card page makes it
tools/sd_site.sh         puts the SD card page's map data and Python on the site
tools/sd_node_test.mjs   the SD card page's build in Pyodide under Node, checked against build_map.py
tools/fetch_logos.py     one-time download of assets/logos
tools/sd_upload.py       write a file to the board's SD card over USB (with the cyd_sdload env)
tools/make_fat_fixtures.sh  rebuild the FAT reader's test disk images (macOS)
tools/build_wasm.sh      the firmware with emscripten -> contrail.js and contrail.wasm
tools/record_sky.py      recorded flights over tools/sim_places.json's places for the browser simulator
tools/sim_site.py        puts the browser simulator, its recordings and maps on the site (site/sim)
tools/sim_node_test.mjs  the WebAssembly build's frames under Node, checked against the desktop simulator
docs/superpowers         specs and plans
site/                    the GitHub Pages site (showcase, simulator, installer, SD card page); screenshots, install
                         images, logos.pak, map data and the simulator's recordings added by Actions
.github/workflows        CI, release and Pages workflows
tools/screenshots.sh     simulator screenshots of every screen and theme
tools/previews.sh        animated previews of the screens for the showcase page
tools/release_assets.sh  release files from the cyd2usb and cyd_microusb builds
```

## Licence

GPL-3.0 (see `LICENSE`). Inter font © The Inter Project Authors, SIL Open Font License
1.1 (`tools/fonts/OFL.txt`); Barlow font © The Barlow Project Authors, SIL Open Font License 1.1
(`tools/fonts/Barlow-OFL.txt`). The airline logos in `assets/logos/` are third-party trademarks of their
airlines, used only to identify a flight's airline; they are not covered by this project's GPL-3.0
licence (see `assets/logos/SOURCE.md`). Inspired by
[Overhead](https://github.com/JamesDavid/Overhead) by James David.
