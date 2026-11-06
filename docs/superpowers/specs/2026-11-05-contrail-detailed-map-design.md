# Contrail Sub-project 5b — Detailed Map Design

Date: 2026-11-05
Status: Implemented (branch `detailed-map`, 2026-11-05). Sections marked *As built* record where the
build differs from the design; measured results are in §10.
Parent: `2026-10-30-contrail-requirements.md` (sub-project 5: "an SD card adds … a detailed local
map"), `2026-11-01-contrail-screens-pass2-design.md` §3.1 (Live Map), `2026-11-04-contrail-sd-assets-design.md`
(5a: the SD stack, packs, `IStorage`)
Mockups: drawn from real data around the board's home by the prototype renderer (§8), both themes
at 10, 25, 50 and 100 nm; approved 2026-11-05.

## Goal

With a map pack on the card, the Live Map shows the area around home in detail: sharp coastlines
and lakes, rivers, main roads, towns and every public airfield with its runways, at any radius from
5 to 100 nm and in both themes. Without one, the map is exactly today's.

Out of scope: controlled airspace (later, US-only data), OpenStreetMap data, uploading the pack from
the web page, the in-browser pack builder (sub-project 6), packs for a whole country or the world,
detail on any screen other than the Live Map.

**Done when:**
1. `tools/build_map.py --lat … --lon …` writes `map.pak` covering 250 nm around home; copying it to
   `/contrail/` on the card is the whole install.
2. With the pack, the Live Map matches the approved mockups: land, lakes and coastline from
   Natural Earth 1:10m; rivers; roads; towns; public airfields and runways, thinned by radius (§5).
3. Plane glyphs, trails, labels, selection, touch and the bottom strip behave as today; plane labels
   keep clear of town and airport names.
4. The map is rebuilt only when it opens, home, the radius (once it has settled) or the theme
   changes, or after a restart when the saved map no longer matches; otherwise it opens at once
   from the saved map.
5. Every failure in §7 falls back to today's built-in map with no crash or stall, and the web page
   says why.
6. `pio test -e test` passes, including the tests in §9.
7. Budgets (§8): ≤ 25 KB flash, ≤ 1 KB static RAM, minimum free heap ≥ 30 KB, rebuild ≤ 1.5 s,
   normal frames no slower than today's map.

## 1. Pieces

| Piece | Where | What |
|---|---|---|
| Builder | `tools/build_map.py` | downloads and caches the sources, cuts, simplifies and writes `map.pak` |
| Prototype renderer | `tools/map_preview.py` | renders a pack (or the sources) to PNG at a radius and theme: the look's reference |
| Pack reader | `firmware/lib/core/map/MapPack.{h,cpp}` | header, levels, tile index, tile CRCs; hands out tiles |
| Features | `firmware/lib/core/map/Features.{h,cpp}` | reads a tile's lines and points (rings keep `RingReader`) |
| Strip painter | `firmware/lib/core/map/DetailPainter.{h,cpp}` | draws one 16-row strip of the detailed chart from the tiles under it |
| Saved map | `firmware/lib/core/map/ChartCache.{h,cpp}` | the finished chart in the pack's reserved area: key, write, read back |
| Map screen | `screens/MapScreen.*` | chooses detailed or built-in, drives rebuilds, reads the saved map when drawing |
| SD stack | `assets/FatReader.*`, `hal_esp32/SdSpi.*`, `hal_native/NativeStorage.*` | block writes inside an existing file; a second open file |

## 2. Sources and the builder

- **Land, lakes, coastline, rivers, roads:** Natural Earth 1:10m GeoJSON, pinned to release v5.1.2
  (`ne_10m_land`, `ne_10m_lakes`, `ne_10m_lakes_north_america`, `ne_10m_rivers_lake_centerlines`,
  `ne_10m_rivers_north_america`, `ne_10m_roads`). Public domain.
- **Airports and runways:** OurAirports `airports.csv` and `runways.csv`, as `gen_map.py` uses
  today. Public domain.
- **Towns:** GeoNames `cities5000` (populated places of 5,000 people or more). CC BY 4.0: the
  README and the web page credit "Map data: Natural Earth, OurAirports, GeoNames".
- The builder downloads into `tools/.cache/map/` (git-ignored) once, about 50 MB in all, and reuses
  it; `--refresh` downloads again. It runs in the tools venv with no new packages (plain JSON and
  CSV, as `gen_map.py` does).
- **Coverage:** a square 250 nm from home on each side (500 nm across), north up. Home and the
  coverage are written into the header.
- **Simplification:** Douglas–Peucker per level, tolerance about half a screen pixel at the
  level's smallest radius (level 0: 40 m, level 1: 100 m, level 2: 250 m).
- **Run:** `python3 -I tools/build_map.py --lat 39.4176 --lon -77.3743 --out map.pak` in under a
  minute; it prints the size per level and the counts of each feature kind.
- *As built:* the downloads come to about 106 MB, not 50. **Public fields** are small airports with an ICAO
  code, or a `gps_code` of the form `K` + three letters or digits that is not `KSnn` / `KYnn` (private
  strips); medium and large airports are always in. Closed runways are left out. **Towns** have six ranks by
  population (0: ≥ 300,000, 1: ≥ 100,000, 2: ≥ 50,000, 3: ≥ 25,000, 4: ≥ 10,000, 5: ≥ 5,000), and each tile
  lists them biggest first, so the painter names bigger towns first. `tools/map_preview.py` renders a pack
  as the board does (the look's reference, §8); the test fixture is a pack built by the builder (§9).

## 3. The pack (`/contrail/map.pak`)

Little-endian throughout. Layout:

```
Header (64 B)
  magic "CMAP", u16 version (1), u16 header size
  i32 homeLatE5, homeLonE5           the home the pack was built for
  u16 coverNm (250)
  u8  levelCount (3), u8 reserved
  u32 packId                          CRC-32 over the level table, indexes and tile data: identifies
                                      the build (the saved map's key), not checked at run time
  u32 cacheOffset, cacheSize          the reserved area (§4), 4 KB aligned
  u16 headerCrc16 (CRC-16/CCITT-FALSE over the bytes before it), padding
Level table (levelCount × 24 B)
  u16 maxRadiusNm (25, 60, 100)
  u16 tileMilliDeg (250, 500, 1000)   square tiles in degrees of latitude and longitude
  i32 originLatE5, originLonE5        south-west corner of the grid
  u16 rows, cols
  u32 indexOffset
  u16 indexCrc16                     over this level's index, checked when the pack opens
  u16 reserved
Per level: tile index (rows × cols × 8 B), row-major from the south-west
  u32 offset (0: empty tile), u24 size, u8 flags (bit 0: all land)
Tile data, each tile:
  u16 crc16 over the rest of the tile
  u16 ringBytes, u16 lineBytes, u16 pointBytes
  rings   today's basemap ring format (Tiles.h), tile-local units 0..2048
  lines   u16 count; each: u8 kind, then a ring-format point run (open, no closing edge)
            kinds: 0 major road (major highway, beltway), 1 road (secondary highway, road),
                   2 river, 3 runway (two points)
  points  u16 count; each: u8 kind, u8 rank, u16 x, u16 y, u8 nameLen, name (UTF-8, ≤ 31 B)
            kinds: 0 town (rank: population class), 1 airport (rank: 0 large, 1 medium,
                   2 public small field), the name is the ICAO ident
Reserved area for the saved map (§4)
```

- Lines and points belong to the tile holding their first point; a line may run past its tile's
  edge (the painter clips to the strip), so no line is cut at tile borders.
- A tile's CRC is checked when it is read; a bad tile is skipped and counted (§7).
- Expected size for one home: 5–15 MB, most of it level 0 roads and rivers.
- *As built:*
  - **A feature table per tile:** each tile starts with one 8-byte entry per feature (kind, rank, its
    bytes, its y range in tile units), so a strip reads only the features crossing it instead of the whole
    tile 17 times. Rings, lines and points are all features, in drawing order. **Lines are cut at tile
    borders** (no overflow of tile units), and a ring edge along a cut is marked so it is not drawn as coast.
  - **Size:** 0.55 MB for 250 nm around the board's home (1,958 towns, 455 airports), not 5–15 MB.
  - **The reserved area** is 135,168 B (33 × 4 KB, `ChartCache::kAreaBytes`); a pack with less is refused as
    "damaged or from another version" (§4).

## 4. Drawing on the board

The built-in map keeps its 1-bit, half-resolution mask in the screen arena. A detailed chart cannot
be held that way, and a full-screen buffer (8–16 KB) cannot be afforded. So the card holds the
finished chart.

- **Choosing:** on map `enter()` and on any view change, the screen uses the detailed chart when
  `map.pak` opened and checked, and the whole view (home ± radius) lies inside the pack's coverage;
  otherwise the built-in map exactly as today.
- **Level:** the first level whose `maxRadiusNm` ≥ the radius.
- **Rebuild:** the chart is drawn strip by strip (the renderer's 16-row strips, 17 for the 270-px
  map) into the strip buffer by `DetailPainter`, then the strip's RGB565 rows are written to the
  saved map. For each strip it reads the tiles of the chosen level that overlap the strip's
  latitude band and the view's longitudes, and draws in order: water, land and lakes (even-odd
  coverage per pixel from the rings, 4× supersampled across, 2× down, no mask), coastline (the
  rings' non-cut edges, 1 px, anti-aliased), rivers, roads (minor, then major), range rings,
  runways, then airport and town marks and names.
  - Names are placed once per rebuild, before the first strip: candidates by priority (large
    airports, medium, towns by rank, small fields), four positions each, skipped if they overlap
    one already placed, the header, the scale bar, the north arrow or home. The placed boxes go
    into the arena as marks (`kMaxMarks` 20 → 32), so plane labels keep clear of them as they do
    of airports today.
  - The rebuild is spread over frames, a few strips per frame; the map fills in top to bottom
    under a "Drawing map…" note, and planes appear when it is done.
  - Scratch for a strip (the supersampled coverage row, the clip state) comes from the arena
    space the built-in mask would use; nothing else is allocated.
- **Normal frames:** `MapBase::draw` reads the damaged strip's rows of the saved map straight into
  the strip buffer (multi-block reads, 7,680 B per full strip), then planes draw over it as today.
  If whole-strip reads prove too slow on busy frames, it reads only the sectors covering the
  damaged rows' spans.
- **The saved map (`ChartCache`):** the reserved area holds the chart's pixels, then a 32-byte key and
  the label marks (*as built*: in 16 × 16 tiles, below). The key holds home, radius, theme, level, the pack's
  `packId` and a format version. During a rebuild the key is cleared first and written last, so
  a rebuild that is cut short (restart, card pulled) never looks valid.
- **Radius changes:** a radius change from the touch panel or the web page rebuilds once it has
  been steady for 1.5 s; until then the old chart stays up (rings and planes are scaled to the new
  radius only after the rebuild).
- *As built:*
  - **Tiles, not rows:** the saved chart is 16 × 16-pixel tiles of RGB565, one 512-byte sector each, 15 across
    and 17 down (the last row's bottom 2 pixel rows are padding): 130,560 B from the area's start, then the
    32-byte key, then 1,440 B for the label marks (§3's area is 135,168 B). A damaged box reads a sector per
    tile it touches whatever its height (a row of 480 B was a sector per row); the renderer's 16-row bands are
    15 consecutive sectors, one multi-block write each and one read for a full-width redraw. The key holds a
    format number (2: tiles) and `sizeof(DetailMark)`.
  - **The saved chart holds the label marks** too, so opening a matching chart reads no tiles at all.
  - **Painting:** land coverage is 2 × 2 supersampled in half-strips of 8 rows (a 960 B sub-pixel buffer); the
    coastline strokes hide the steps. Tile data is read through a small stream (`ByteStream`, 32 B), not a
    pointer. A tile's CRC is checked once per rebuild (`layout()`), not on every read; a damaged all-land tile
    stays land without its lines (the all-land flag is in the checked index).
  - **The job is paced by time:** it paints bands until the frame has spent about 60 ms (at least one band), so
    a full-damage frame (the first, a fade) never takes it all at once, and its damage grows by a band while
    frames have time to spare. The first frame clears the old chart and its planes at once; rows the job has
    not reached show water. Tests, with no clock, take 2 bands a frame.
  - **"Drawing map…" shows in the bottom strip**, not over the chart, so it is never saved into it; planes and
    their labels are hidden until the chart is finished.
  - **Arena:** `Scratch` is 4,084 of 4,096 B: the built-in mask and 20 airport marks (2,546 B) share a union
    with the painter's 1,088 B scratch (sub-pixels and a rank mask per tile) and 32 `DetailMark`s of 40 B
    (2,368 B); the 32 planes take 1,536 B.
  - **Planes move a whole pixel at a time over the detailed chart** (half pixels on the built-in map): each
    redraw reads the chart back from the card, and half-pixel steps redrew planes nearly twice as often.
  - **Trail points are staggered:** each plane takes its 10-s point in its own second (a phase from its code),
    so a second's frames redraw about a tenth of the trails instead of all of them at once (both maps).
  - **Logs:** `[map] chart drawn in N ms (level L): layout, paint, card read and write (ms / blocks), frames`,
    `[map] saved chart (level L, N names): no rebuild`, `[map] card error: the built-in map until restart`.
- **Colours:** four new theme entries, the approved mockup values:

  | | Cinematic | Editorial |
  |---|---|---|
  | `mapRoad` (major) | 58, 74, 112 | 84, 88, 96 |
  | `mapRoadMinor` | 42, 56, 90 | 64, 68, 74 |
  | `mapRiver` | 44, 72, 128 | 52, 66, 84 |
  | `mapTown` | 150, 162, 200 | 170, 170, 176 |

  Town names use the small body font; Editorial writes them in capitals.

## 5. What shows at each radius

| Radius | Level | Roads | Rivers | Towns | Airports |
|---|---|---|---|---|---|
| 5–25 nm | 0 | all | all | ≥ 5,000 people, at most 10 named | large, medium, public small fields (those with an ICAO code), with runways |
| 26–60 nm | 1 | major, and minor of scale rank ≤ 7 | scale rank ≤ 8 | ≥ 50,000, at most 7 | large and medium, with runways |
| 61–100 nm | 2 | major | scale rank ≤ 8 | ≥ 300,000, at most 5 | large, and medium up to 10 named |

The builder applies the level filters; the painter applies the caps and placement. Private strips
are never included.

*As built:* "public small fields" use the `gps_code` rule in §2. Level 0 takes at most 10 town names, level 1
7, level 2 5, and level 2 at most 10 medium airports; airports leave the towns their share of the 32 marks.

## 6. SD stack changes

- **Writes:** `SdSpi` gains `writeBlocks(lba, buf, n)` (CMD24/CMD25 with data-response and busy
  checks). `FatReader` gains `writeAt(offset, buf, n)` for an open file, sector-aligned, allowed
  only inside the file's existing size: it maps offsets to sectors through the file's own cluster
  chain and never touches the FAT, directory entries or the file's size. `IFile` gains
  `writeAt()`, returning false where writing is not supported.
- **Second file:** `FatReader` keeps two file slots (logos, map), each with its own checkpoints;
  the sector cache stays shared. Static RAM for the second slot is about 200 B.
- **Native:** `NativeStorage` opens files read-write so the simulator and tests use the same path.
- *As built:*
  - **Multi-block transfers:** `IBlockDevice` has `readBlocks` / `writeBlocks`; `SdSpi` does CMD18 (a start
    token and data CRC per block, then CMD12, resent up to three times if the card does not answer or reports
    a command CRC error) and CMD25 (a data response and programming wait per block, then the stop token; a
    lost R1 or a busy timeout still ends the transfer). `FatFile` hands the device each run of whole sectors
    at consecutive LBAs in one call: to the cluster's end, or the whole run in a contiguous file.
  - **Command CRCs on (CMD59):** a bit error in a write's address is refused rather than obeyed; a card that
    refuses CMD59 is read-only. A write timeout is not retried; signal errors step the clock down 20 → 10 MHz.
    40 MHz (CMD6 high speed) was tried and refused on this board (§10).
  - **The sector cache is two-way LRU** (+512 B static): index, feature-table and data sectors stop evicting
    one another, which removed 60-67 % of a rebuild's block reads with no painter change.
  - `IFile::ioStats()` counts and times the card's blocks for the job's log line.

## 7. Errors and status

The map falls back to the built-in map and `/api/info` reports `"sd":{…,"map":{"state":…,"why":…}}`;
the web page's Firmware card shows the line.

| Case | Map | Web page |
|---|---|---|
| No card, or no `map.pak` | built-in | "Map: built-in" |
| `map.pak` with a bad header, version or index CRC | built-in | "Map: built-in (map.pak is damaged or from another version)" |
| View not inside the pack's coverage (home moved, or the pack is for elsewhere) | built-in | "Map: built-in (home is outside the map pack; rebuild it)" |
| A read or write fails, or the card is pulled | built-in until restart | "Map: built-in (card error)" |
| A tile fails its CRC | detailed, that tile left as water | "Map: detailed (N damaged tiles)" |
| Pack in use | detailed | "Map: detailed · 250 nm around 39.42, -77.37" |

The pack is checked when it opens at boot: the header CRC and each level's index CRC (about 20 KB
of reads). Tile data is checked tile by tile as it is read; the whole file is never read at once.

*As built*, the web page's lines:

| Case | Web page |
|---|---|
| No card, or no `map.pak` | "Map: built-in" |
| Bad header, version, index CRC, or a reserved area too small | "Map: built-in (map.pak is damaged or from another version; rebuild it)" |
| The card refused command CRCs (CMD59): the chart cannot be written safely | "Map: built-in (the card can't be written safely: CRC off)" |
| Home and radius (from the settings) not inside the pack | "Map: built-in (home is outside the map pack; rebuild it)" |
| No location set | "Map: built-in (no location set)" |
| A read or write failed | "Map: built-in (card error)", until restart |
| Pack in use | "Map: detailed · 250 nm around 39.42, -77.37", with " (N damaged tiles)" after it when tiles failed their CRC, and the credit line "Map data: Natural Earth, OurAirports, GeoNames" |

## 8. Budgets

- **Flash:** ≤ 25 KB (painter, pack and feature readers, cache, SD writes). 94 KB is free.
- **Static RAM:** ≤ 1 KB (the second file slot, the cache key, the larger marks array lives in the
  arena).
- **Heap:** minimum free ≥ 30 KB, measured over a 10-minute run with the map up and busy traffic.
- **Time:** rebuild ≤ 1.5 s at any radius; normal map frames no slower than today's (measured on
  the board with the frame stats).
- The prototype renderer (`tools/map_preview.py`) is the look's reference: it renders the same
  pack with the same rules, so the board and the PNG should match apart from anti-aliasing.

## 9. Tests

- `test_map_pack`: header and level checks, tile index lookups, CRC failures (bad header, bad
  tile), coverage test, level choice by radius. Fixture: a small pack built by `build_map.py` from
  a checked-in clipped source set (`test/fixtures/map_small/`), committed as `map_small.pak`.
- `test_features`: line and point reading, including truncated data.
- `test_chart_cache`: key write order (a cut-short rebuild reads as invalid), key mismatch on each
  field, read-back of written strips through `NativeStorage`.
- `test_fat_reader`: `writeAt` inside the file, refused past its end, across a cluster boundary,
  and on a fragmented file; the image's FAT and directory are byte-identical afterwards; two open
  files at once.
- Golden PPMs: the detailed map at 10, 25 and 100 nm in both themes from `map_small.pak`; the
  built-in map's goldens unchanged.
- On the board (added to `docs/HARDWARE_TESTING.md`): rebuild time per level, frame time with busy
  traffic, `minheap` over 10 minutes, a restart reopening the saved map without a rebuild, and each
  failure in §7 (no pack, pack for another home, card pulled while the map is up).
- *As built:* the fixture is a built pack, `map_small.pak` (40 nm around the board's home, rebuilt with
  `build_map.py --lat 39.4176 --lon -77.3743 --cover 40`), not a checked-in source set. The goldens are the
  detailed map at 10, 25 and 35 nm (levels 0, 0 and 1: the 40 nm fixture cannot cover 100 nm) in Cinematic, 25 nm
  in Editorial, and the chart part way through its job; the built-in map's goldens are unchanged.
  `test_map_screen` also covers the job (its pacing, its damage, the key written after the last band, leaving
  mid-job, a fade), every fallback in §7 against the built-in map pixel for pixel, the radius settle, and no
  stale pixels with planes over the saved chart. `test_detail_painter`, `test_map_features`, `test_map_pack`, `test_chart_cache`, `test_map_trails`
  (phases), `test_sd_protocol` (multi-block replies, the clock ladder) and `test_fat_reader` (multi-block runs,
  I/O counts) cover the rest.

## 10. Results (*as built*)

Measured on a CYD2USB with mock (23-30 aircraft) and real adsb.lol traffic, with a 250 nm pack (details in
`docs/HARDWARE_TESTING.md`, "Detailed map (5b)"). Final build: three passes, the last at 45f1d4b; the
high-speed code measured there was removed afterwards (89d5bc1).

| Measure | Result |
|---|---|
| Flash | 1,872,113 → 1,895,273 B (+23,160 B; budget 25 KB) |
| Static RAM | 90,688 → 91,712 B (+1,024 B; budget 1 KB): the two-way sector cache (512 B), the second file slot, `MapPack`, the painter and job state in `MapBase` |
| Arena | 4,084 of 4,096 B (§4) |
| Rebuild | pass 2: 505-509 / 747-751 / 940-959 / 1,090-1,142 ms at 10 / 25 / 50 / 100 nm; pass 3: 709 ms at 25 nm, 1,052 ms at 100 nm (budget 1.5 s, **met**). Most of it card time: at 100 nm 592-638 ms reading 1,080 blocks and ~145 ms writing 260; paint 61-187 ms, layout 3-13 ms. Pass 1 (single-block, rows, 2 bands a frame) took 1.2-4.7 s |
| Restart | the saved chart reopens with no rebuild |
| `minheap` | 30,452 B with real traffic and TLS (72,504 B on the mock feed); with 64 aircraft the small largest blocks (692-1,012 B in `[heap]` windows) were the same with the Radar up, so the feed, not the map, causes them; the map path allocates nothing |
| Frames, 25 nm, 24-30 planes (30 s windows without a screen switch or rebuild) | detailed busy 12.4-16 %, typical 13-33 ms, max 75-113 ms, 34-64 of 150 frames over 33 ms; built-in 11.0-14.3 %, max 35-60 ms (pass 2 was 22-33 % and max 147-210 ms; pass 1 35-46 % and 194-234 ms) |
| Frames, 100 nm | detailed 5.5-8.2 %, max 44-94 ms; built-in 4.4-5.1 %, max 22-32 ms |
| Card clock | 20 MHz. 40 MHz high speed was refused: GPIO-matrix routing is expected to limit reads to ~26 MHz, and with the pins on the IO_MUX the card refused CMD17 at mount (reported from the board run; no log saved) |
| Logos | `[sd] logos.pak: 1628 logos` at boot; no SD errors in passes 2-3 |

**"Normal frames no slower than today's map" is not fully met.** At 25 nm the detailed map is now close in busy
time, but its slowest frames are about twice the built-in map's and more of its frames go over 33 ms. The
reason is structural: every redraw under a plane, a label or a trail reads the chart back from the card, about
0.6 ms a block (a 16 × 16 tile), where the built-in map recomputes the pixels from a mask in RAM. Tile reads, staggered trail
points and whole-pixel planes cut the blocks read per frame by about four fifths (in the native model, 30 planes
at 25 nm: 158 → 32); closing the rest needs the chart in RAM, which this board cannot hold (130 KB against ~45 KB of free heap): an ESP32 with PSRAM would.

Only the last chart is kept on the card, so going to another radius or theme and back draws it again (the soak
build's theme cycling rewrites the ~130 KB chart at each flip).

Still to check with someone at the board: boot with no card, the card pulled with the map up, a damaged tile,
the detailed map by eye on the panel in both themes, and touch on the detailed map.
