# Contrail Sub-project 6c — SD Card Pack Builder in the Browser

Date: 2026-11-06
Status: Design approved, not built.
Parent: `2026-10-30-contrail-requirements.md` ("Web flasher site": "SD card pack builder (map for your area,
logos) downloaded as a zip"). Builds on 5a (`logos.pak`), 5b (`map.pak`, `tools/build_map.py`) and 6a (the site,
the Pages workflow). 6b (browser simulator) and 6d (animated previews) are separate.

## Goal

Anyone with a CYD can get the detailed Live Map without Python: pick home on a page of the site, press Build, and
download one zip to unzip onto a microSD card. The map is the one `tools/build_map.py` makes, byte for byte.

Out of scope: writing the card from the browser, uploading packs to the board over Wi-Fi, choosing the coverage
(always 250 nm, as the tool's default), choosing which logos, homes whose square crosses ±180° or ±85°.

**Done when:**
1. The site has an **SD card** page (`sd.html`, in the nav beside Install) that builds and downloads
   `contrail-sd-card.zip` holding `contrail/map.pak` and `contrail/logos.pak`.
2. Home can be set by search (airport code or town), "Use my location", typed coordinates, or a link from the
   board's own web page with home filled in.
3. The page's `map.pak` is byte-identical to `build_map.py --lat … --lon …` for the same home (CI proves it).
4. Nothing about the location leaves the browser: the page fetches only fixed files from the site and the CDN.
5. The Install page, the showcase and the README point to the page; the Python tool stays documented.
6. The board's settings page links to the page with its home.

## 1. Pieces

| Piece | Where | What |
|---|---|---|
| Region splitter | `tools/map_regions.py` | raw sources → `site/mapdata/` (regions, shared parts, search index) |
| Region loader | `tools/build_map.py` | `load_regions(dir, box)`, returning exactly what `load_sources` returns |
| Parity test | `tools/map_regions.py --selftest`, CI | packs from regions vs from raw sources, several homes |
| Page | `site/sd.html`, `site/sd.js`, `site/sd-worker.js` | home picker, progress, download |
| Pyodide test | `tools/sd_node_test.mjs`, CI | Pyodide under Node builds KFDK; bytes equal the native build |
| Pages workflow | `.github/workflows/pages.yml` | cached sources, the split, `build_map.py` and `logos.pak` onto the site |
| Board link | `firmware/web/index.html` | "Build an SD card for this location" in the map card |

## 2. Region data

The Pages workflow restores the raw sources from the Actions cache (key: the `SOURCES` table's hash; a miss
downloads them with `build_map.py`'s own `fetch`) and runs `tools/map_regions.py <cache> site/mapdata`.

- **Cells:** 5° × 5°, named `r<lat0>_<lon0>.json` by their south-west corner (`r35_-80.json`). A cell file exists
  only when something lies in it.
- **Shapes (the six GeoJSON layers):** split into the parts `geo_features` iterates (a polygon with its holes, or
  one line), each with its layer, its index in the layer's iteration order, the properties the builder reads
  (`scalerank`, `type`) and its rings. A part goes into every cell its rings' bounding boxes overlap, never
  clipped. A part with more than 2,000 points is written once to `shared/<layer>-<index>.json` and the cells
  hold only a reference. Because `Box.touches` tests bounding boxes, every part a home's square touches is in some
  cell the square overlaps, so the regions hold a superset of what the builder selects.
- **Towns:** the rows `load_sources` would keep anywhere (populated places, not `PPLX`/`PPLQ`), reduced to name,
  latitude, longitude, population and their row number, into the cell holding the point.
- **Airports:** the large, medium and small airports with a shown name (`airport_name`), reduced to rank, name,
  latitude, longitude, their row number and their open runways, into the cell holding the point.
- **Search index:** `search.json`: every large and medium airport and every small one with a public code
  (code, name, municipality, region, lat, lon), and every town of 5,000 or more (name, admin region, country,
  lat, lon). Loaded the first time the search box gets focus.
- **Manifest:** `index.json` lists the cells present, their sizes, and the sources' versions and dates; the page
  fetches only listed cells and reports the download size before it starts.
- Numbers are written with Python's `repr`, which round-trips floats exactly.

`build_map.py` gains `load_regions(dir, box)`: it reads the cells the box overlaps and the shared parts they
reference, drops duplicates, and rebuilds the six lists in the original order (layer, then part index; towns by
population then row; airports by rank then row), filtering with the same `Box.touches`, `Box.has` and rank rules as
`load_sources`. `--regions <dir>` selects it on the command line. `write_pack` gains an optional `progress(level)` callback
(none by default) and is otherwise unchanged.

## 3. The page

- **Home:**
  - A search box: matches airport codes first, then names and towns as you type; picking one fills the coordinates.
  - "Use my location": the browser's geolocation, rounded to 0.001°.
  - Latitude and longitude fields, always shown and editable.
  - `sd.html?lat=…&lon=…` fills the fields on load; the board's link uses it.
  - Under the fields: "Near <nearest town>, <region>" from the search index, so a wrong sign or a typo shows.
  - A home whose square crosses ±180° or ±85° shows the tool's message and disables Build.
- **Build:** runs in a Web Worker (`sd-worker.js`) so the page stays responsive. Steps shown with progress:
  "Loading Python" (Pyodide, pinned, from jsDelivr; about 8 MB the first time, then cached), "Downloading map
  data (3.1 MB)", "Building level 1 of 3" (through `write_pack`'s callback),
  "Making the zip". The worker writes the cells into Pyodide's file system, imports `build_map.py` (fetched from
  the site), calls `load_regions` and `write_pack`, then zips `contrail/map.pak` and `contrail/logos.pak` with
  Python's `zipfile` (stored, not deflated: the packs barely compress) and hands the bytes back.
- **Download:** a button saves `contrail-sd-card.zip` and shows its size and the map's summary (towns, airports).
  Then the card steps: a FAT32 microSD card (most cards up to 32 GB come that way); unzip so the `contrail`
  folder is at the top of the card; put it in the board; restart it. The Live Map shows the detailed map when
  home and radius lie inside the pack.
- **Look:** the site's style; works at phone width, though building is aimed at a desktop browser.
- **Footer:** the data credits (Natural Earth, public domain; OurAirports, public domain; GeoNames, CC BY 4.0)
  as on the other pages.

`logos.pak` comes from the latest release: the Pages workflow downloads it with the install images into
`site/firmware/`. With no release yet, the page builds the map alone and says logos are not available yet.

## 4. Errors

| Case | What the page does |
|---|---|
| Pyodide or a data file fails to load | "Couldn't download <what>. Check the connection and try again." with Retry |
| Home outside the supported range | the tool's message; Build disabled |
| The build raises | "The build failed: <message>" and a link to report it on GitHub |
| Geolocation refused or unavailable | "Location isn't available; search or type coordinates instead." |
| Browser without Web Workers or WebAssembly | a short message instead of the builder |

## 5. Testing

- **Parity (native, CI):** `tools/map_regions.py --selftest` splits the cached sources into a temporary folder and,
  for KFDK, a coastal home (Boston), a far-north home (Anchorage) and a European home (Amsterdam), builds
  `map.pak` from the raw sources and from the regions and compares the bytes. Runs in CI with the sources
  cached.
- **Pyodide (CI):** `tools/sd_node_test.mjs` loads the pinned Pyodide from npm in Node, runs the worker's build
  for KFDK on the split data and compares the bytes with the native build.
- **Page:** checked locally and on Pages in the browser pane: search, `?lat=&lon=`, the "Near" line, the range
  message, a full build, the zip's contents and names.
- **Board (developer):** a downloaded zip on the card, the detailed map on the Live Map. Recorded in
  `HARDWARE_TESTING.md`; it can wait for the developer.
