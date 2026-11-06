# Contrail Sub-project 5a — SD Card, Logos and Aircraft Shapes Design

Date: 2026-11-04
Status: Implemented (branch `sd-assets`, 2026-11-04). Sections marked *As built* record where the
implementation differs from the approved design; the hardware results are in `docs/HARDWARE_TESTING.md`
("SD card and logos (5a)").
Parent: `2026-10-30-contrail-requirements.md` (sub-project 5, split into 5a here and 5b, the
detailed map, later), `2026-10-31-contrail-screens-design.md`, `2026-11-01-contrail-screens-pass2-design.md`
Mockups: private canvas "Contrail Logos & Shapes Mockups" (shape families, Closest Flight in both
themes, Board in both themes, Alert, logos vs tail fins)

## Goal

Give every flight an identity at a glance: the airline's real logo when an SD card holds it, a
drawn tail fin in the airline's colours when it does not, and an aircraft shape that matches the
type (a 787 looks like a widebody, a Cessna like a light single, a helicopter like a helicopter).

Out of scope: the detailed map (5b), uploading packs from the web page, artwork per individual
type, logos on Map and Radar labels, hot-swapping the card while running, the in-browser pack
builder (sub-project 6).

**Done when:**
1. With `logos.pak` on the card, Closest Flight, the Board and the Alert takeover show the real
   logo for each airline the pack holds, in both themes, per the mockups.
2. With no card (or no logo for that airline), the same places show a drawn tail fin: the livery
   colour with a brand-colour stripe for the 64 airlines in the colour table, a neutral fin for
   any other airline code, and the aircraft family's top view on a tile for a registration.
3. The plane on Closest Flight, the Radar and Map icons and the Alert's target show the top view
   of the flight's shape family; the Closest Flight route bar's marker is its side view. An
   unknown type keeps today's generic plane.
4. `tools/build_pack.py` turns `assets/logos/` into `logos.pak`; copying it to `/contrail/` on a
   FAT32 card is the whole install.
5. The web page's Firmware card and `/api/info` report the card: "logos.pak · 1,412 logos", "No
   SD card", or why a pack was refused.
6. `pio test -e test` passes, including the tests in §8.
7. Frame times hold; minimum free heap stays ≥ 30 KB; the feature adds ≤ 45 KB of flash.
   *As built (measured 2026-11-05, §9):* **flash met**, +31,472 B (30.7 KB: 1,840,641 → 1,872,113 B,
   93.6 % → 95.2 %), static RAM +872 B after the review's RAM savings (+1,992 B before them).
   **Heap met, narrowly**: `minheap` 30,612 B over an 11-minute soak and 33,916 B in normal use
   before the savings (36.9 KB before 5a), 35,092 B in a 2-minute run after them; a TLS handshake
   once left a 660 B largest block, with no failure. **Frame times held** in the traffic available
   (2-8 aircraft at night): steady frames as before, the Board's steady max 9-14 ms in the soak and
   37 ms once in normal use; but a badge load takes 5-6 ms on average (3.7-7.7 ms), not the ~2 ms
   estimated in §3, and the Board's busy-traffic frame times are still to be measured.

## 1. Pieces

| Piece | Where | Thread | Job |
|---|---|---|---|
| `Storage` | `lib/hal` (interface), `hal_esp32`, `hal_native` | render | Mount at boot; open a file; read at an offset. ESP32: SD on VSPI (SCK 18, MISO 19, MOSI 23, CS 5) at 20 MHz, separate from the display's bus, through our own read-only stack (`SdSpi` + `FatReader`, §2a). Native: a folder (`CONTRAIL_SD`, default `sim_sd/`) |
| `Pack` | `lib/core/assets/` | render | Opens a `.pak`, validates the header, binary-searches the index, reads an item with bounds and CRC checks |
| `LogoStore` | `lib/core/assets/` | render | ICAO code + size → a `Badge` (palette image) from `logos.pak`, into caller-provided memory |
| `Badge` drawing | `lib/core/gfx/` | render | Draws an indexed image with per-entry alpha, clipped to the strip |
| `TailFin`, `Identity` | `lib/core/ui/` | render | Draws the fin badge for an airline at any size, in either theme; `Identity` picks logo, fin or shape for a flight and reloads a screen's logo when the airline changes |
| `Shapes` | `lib/core/data/` | render | 12 families × (top view, side view) as small vector outlines in flash; draws them with `aaPolygon`; type → family lookup |
| Tools | `tools/` | build | `fetch_logos.py` (one-time: logo set → `assets/logos/*.png`, committed), `build_pack.py` (→ `logos.pak`), `gen_shapes.py` (outlines + type table → `ShapeData.cpp`, committed) |

## 2. Storage and packs

- **Mount.** `Storage::begin()` runs once at boot after the display; no card, an unreadable card
  or a missing `/contrail/` folder is not an error. The result is kept for status. Removing the
  card while running makes reads fail; every caller falls back (fins) until the next restart.
- **Pack file**, little-endian:
  - Header, 32 bytes: magic `CPAK`, `u16 version` (1), `u16 kind` (1 = logos), `u32 count`,
    `u32 indexOffset`, `u32 dataOffset`, `u32 builtUnix`, 8 bytes reserved.
  - Index: `count` entries of `char key[4]` (ICAO code, NUL-padded), `u32 offset`, `u32 size`,
    sorted by key. 12 bytes each.
  - Items at their offsets; each ends with a CRC-16 of its bytes.
- A pack is refused (and reported) when the magic, version or kind is wrong, or the index runs
  past the end of the file. A bad item (bounds or CRC) is skipped as if absent.
- *As built:* each **logo size block** ends with its own CRC-16 (over its palette and pixels), not
  one CRC per item, so loading a 25 px Board badge reads ~400 B rather than the whole ~3.7 KB item.
  A block that fails its CRC or bounds makes that airline show its fin; this is logged once in all
  (`[sd] some logos failed their checks (first: …)`), not once per airline.
- `Pack` keeps one file open per pack; a lookup is a binary search reading index entries (about
  11 reads of 12 bytes for 1,628 items). *As built,* loading one size then reads the item's
  `sizeCount` byte, its size descriptors (8 bytes each), the block's palette 16 entries (48 B) at
  a time, its pixels in one read (whole sectors straight into the badge) and the 2-byte CRC:
  for a 25 px badge about 17 small reads, ~400 B in all.

## 2a. SD stack (*as built*)

The design assumed the Arduino `SD` library (FATFS under the ESP-IDF VFS). On the board it cost too
much: flash 97.0 % (1,907,349 B) and, with a card mounted, ~17 KB of heap, which took `minheap` down
to 28,024 B and made TLS handshakes fail (mbedTLS BIGNUM allocation errors). It was replaced by our
own read-only stack:

- **`SdSpi`** (`lib/hal_esp32/SdSpi.*`): a raw SPI-mode driver on VSPI through the Arduino `SPIClass`
  LovyanGFX already links. CMD0, a strict CMD8 check, ACMD41 with HCS, CMD58 (block or byte
  addressing), CMD16 on SDSC; CMD17 single-block reads with 100 ms token and 300 ms busy timeouts so
  a pulled card fails fast. Every data block's CRC-16 is checked. 20 MHz, falling back to 10 MHz
  only on signal errors (a CRC mismatch or a bad token), not on a missing card.
- **`FatReader`** (`lib/core/assets/FatReader.*`, testable on the desktop): FAT16 and FAT32 only
  (FAT12 and GPT are refused with a reason), from an MBR's first FAT partition or a superfloppy
  (no partition table). Paths match 8.3 short names, ignoring case; long names are not read. No
  heap: one 512-byte sector cache shared by directory, FAT and data, and one file slot, all static.
  `open()` notes whether the file's clusters are consecutive (a file copied onto a card with free
  space is; the test card's `logos.pak`, written with `cyd_sdload`, is); if they are, reads map offsets to clusters with no FAT reads at all. A fragmented
  file still reads, walking the FAT from one of 8 checkpoints through the same cache.
- **Bounds on a corrupt card:** a directory scan stops after 65,536 entries (4,096 sectors, the FAT
  limit); every cluster chain is walked with Brent's loop check; a file of more than 32,768
  clusters is refused (`open()` walks and checks the whole chain up front, so no read follows an
  unchecked chain; `logos.pak` is 10,400 clusters on the 512-byte-cluster test card).
- **Why:** flash and heap. With SD.h and FS.h gone, FATFS, the FAT VFS and Arduino FS drop out of
  the link: flash 1,907,349 → 1,871,601 B (97.0 % → 95.2 %), 7.2 KB over a build with no SD at
  all; static RAM +1.5 KB and no heap (0.7 KB after the review: one sector cache, one file slot). Measured results are in §9.
- **Writing the card:** `logos.pak` can be written over USB without a card reader: flash the
  `cyd_sdload` helper firmware and run `tools/sd_upload.py` (460,800 baud, about 165 s for the
  5.3 MB pack). Copying the file to `/contrail/` on a FAT32 card in a computer works too.

## 3. Logos

- **Source.** `fetch_logos.py` downloads the FlightAware-derived set from
  `github.com/Jxck-S/airline-logos` (`flightaware_logos/<ICAO>.png`, 90×90, ~1,425 airlines),
  falling back to `radarbox_logos` for codes it lacks (*as built:* 1,628 logos, 203 of them from
  RadarBox; the pack is 5,299,358 B), and commits them under `assets/logos/`
  with a `SOURCE.md` naming the origin and the trademark notice. The images are the airlines'
  trademarks; the project uses them for identification only.
- **Item.** One item per airline holds three sizes, pre-scaled at build time (Lanczos, then
  rounded corners as alpha): `u8 sizeCount`, then per size `u8 w, h, bpp` (8 or 4),
  `u8 paletteCount`, `u32 offset` (within the item); each size is a palette of `paletteCount` ×
  (RGB565, alpha) then `w × h` indices, packed two to a byte at 4 bpp.
  - Closest Flight (Cinematic) and Alert badge: 40 × 40, 8 bpp, ≤ 128 colours: about 2 KB. (The
    Alert mockup's 34 px becomes 40.)
  - Closest Flight (Editorial) band badge: 30 × 30, 8 bpp, ≤ 128 colours: about 1.3 KB.
  - Board badge: 25 × 25, 4 bpp, 16 colours: about 360 B.
  - About 3.7 KB per airline, ~5.3 MB for the whole set.
  - *As built:* a source that is not square is padded to a square with transparent margins
    (centred) before scaling, so no logo is stretched. The quantiser averages alpha across merged
    colours, so palette alpha is snapped at the ends: ≥ 250 becomes 255 and ≤ 5 becomes 0 (solid
    areas exactly opaque, corners exactly clear, so `drawBadge` skips blending them); values in
    between are kept.
- **Memory.** Badges load into the screen arena (`ScreenArena`, 4 KB), which Closest Flight and
  the Alert do not use and the Board uses little of: Closest and Alert hold one 40 px badge; the
  Board holds up to its 6 visible rows' 25 px badges (≈ 2.2 KB) next to its existing scratch. If
  the Board's total exceeds the arena, `kBytes` may grow to 6 KB (static RAM, counted against
  the heap target). *As built:* the arena stayed at 4 KB. The Board's scratch is 4,016 B on the native
build (10 rows of 152 B and 6 logo slots of 416 B; about 3,896 B on the ESP32, with 4-byte pointers),
and its six logo slots are cached by airline, not by row. Loads happen when the flight shown changes, on the render thread: about 2 KB
  read (~400 B for a Board badge). *As built* a load takes 5-6 ms on average on the board (3.7-7.7 ms;
  6-10 ms before reads of a contiguous file skipped the FAT; the estimate was ~2 ms): each small read
  that misses the sector cache costs a whole 512-byte SD block.
- **Lookup order** for a flight: its callsign's ICAO prefix (`callsignAirline`) → logo in the pack
  → tail fin (§4). No prefix (a registration) → the family tile (§5).

## 4. Tail fins

- A swept fin, `M10,94 L56,8 L84,8 L92,94` in a 100-unit box, filled with the airline's `fill`
  (its `color` when `fill` is zero), crossed by a stripe band clipped to the fin in the airline's
  `color`. When `color` and `fill` are too alike (RGB distance < 80), the stripe is white or
  near-black, whichever contrasts more with the fill.
- Tile: Cinematic, a rounded square in `card`-lightened navy (`#26306A`); Editorial, a white
  square with 3 px corners. Sizes as the badges (40, 30, 25).
- An airline code that is not in the colour table gets the fin in `brandNeutral` with a lighter
  stripe.
- Drawn with `aaPolygon`; no image data.

## 5. Aircraft shapes

- **Families (12):** narrowbody twin, widebody twin, four-engine jumbo, superjumbo, regional jet
  (rear engines), business jet, turboprop, light single, light twin, helicopter, fast jet,
  airlifter. Each has a top view (symmetric half-outline, mirrored, plus engine ellipses and prop
  or rotor lines) in a 100-unit box, nose up, and a side view in a 120 × 40 box, nose right, as
  drawn on the canvas. Coordinates are stored as `uint8` in half-units (0–240); all 12 come to
  well under 4 KB.
- **Sizes.** Radar and Map icons by wake category: L 13 px, M 16, H 20, J 22. *As built:* by each
  family's typical wake class rather than the flight's (as on the canvas): light single, light twin,
  business jet, helicopter 13 px; narrowbody, regional, turboprop, fast jet 16; widebody, jumbo,
  airlifter 20; superjumbo 22. Shared icon-geometry helpers in `data/Shapes.h` (`iconPx`,
  `selectRingR`, `iconReach`, `labelPushOut`) keep the Radar's and Map's icons, selection rings,
  damage boxes and label offsets consistent. Closest Flight's sky
  plane keeps its current 34 px wingspan scale; the Alert's target 40 px; the route-bar side view
  34 × 12.
- **Type → family.** `gen_shapes.py` reads tar1090-db's `icao_aircraft_types2.js` (ICAO Doc 8643:
  name, description class like `L2J`, wake category; 2,788 types) and classifies:
  - by rule, stored nowhere: `H*` → helicopter; `L1P`/`L1T`/`A1P`… → light single; `L2P` →
    light twin; `L2T`/`L4T` → turboprop (`L4T` heavy → airlifter);
  - by table: jets (`L2J`, `L3J`, `L4J`, …) and anything the rule gets wrong, using wake
    category, name keywords (Citation, Gulfstream, Learjet, Falcon, Challenger, Global, Phenom,
    Hawker → business jet; CRJ, ERJ-135/145, Fokker 70/100 → regional; known fighter and trainer
    makers/models → fast jet; C-17, C-5, An-124, A400M, C-130, Il-76 → airlifter; A380 →
    superjumbo; 747, A340, 707/KC-135, Il-96 → four-engine jumbo) and a hand-kept overrides list
    in `tools/shapes_overrides.csv`. The script reports any jet it could not classify; the build
    fails if that list is not empty.
  - Stored as sorted `u32` (type code in base 37, 21 bits; family, 4 bits): about 600 entries,
    ~2.4 KB. *As built:* the table holds **every** Doc 8643 type (2,787 entries, ~11 KB), not only
    the jets: the firmware cannot apply the class rules (`L2P` → light twin …) without each type's
    class, so the generator applies them and stores the result.
- **Fallback** when the type is missing or unknown: the ADS-B emitter category (A1 light single,
  A2 business jet, A3 narrowbody, A4 narrowbody, A5 widebody, A6 fast jet, A7 helicopter), if the
  provider gives one (`Aircraft::category`); otherwise today's generic `planeGlyph`.

## 6. Where they appear

| Place | Logo / fin | Shape |
|---|---|---|
| Closest Flight, Cinematic | 40 px badge left of the callsign; callsign shifts right | Sky plane: top view; route bar marker: side view |
| Closest Flight, Editorial | 30 px badge at the band's top-left, before the airline name | Same |
| Board, both themes | 25 px badge at each row's start (replaces the Cinematic colour stripe) | Family tile for registrations |
| Alert takeover | 40 px badge left of the callsign | Top view in the target circle |
| Radar, Map | — | Top view icons, sized by wake class |

Fins are drawn at the same three sizes.

*As built* (layout details settled on the board and in review):
- **Bold Editorial** draws a white tile under every logo (Board rows, the Closest band badge and the
  Alert), so a logo with transparent areas reads on any livery colour, as the fins' Editorial tile.
- **Closest Flight:** the Cinematic card badge is 40 px at (12, 145) and the callsign starts at x 59;
  the Editorial band badge is 30 px at (10, 8) and the airline name starts at x 47.
- **Board:** the 25 px badge sits at x 14, y + 6 in each row; the row's text starts at x 45. The six
  logo slots are cached by airline (the board shows at most six airlines); a slot no listed row needs
  is reused, so a row still fading out whose slot was reused drops to its airline's fin.
- **Alert:** the 40 px badge's top is at `kHeroY − 34` (its middle on the hero's cap height):
  Cinematic one centred group of badge, 9 px gap and callsign; Editorial the badge at the margin and
  the callsign after it. A callsign too wide beside the badge drops to the title face, raised so its
  caps stay level with the badge.

## 7. Errors and status

| Case | Behaviour |
|---|---|
| No card | Fins everywhere; status "No SD card" |
| Card unreadable (answers but does not mount: exFAT, NTFS, GPT, FAT12, no FAT partition, a failed read) | Fins everywhere; status names the reason ("SD card · exFAT (format the card FAT32)"); `/api/info` has `"mounted": false` with `error` set |
| `/contrail/logos.pak` missing | Fins; status "SD card · no logos.pak" |
| Wrong magic, version or kind | Fins; status names the reason ("logos.pak is a newer version; update the firmware"; the log has the version numbers) |
| Item fails its CRC or bounds | That airline shows its fin; logged once |
| Card removed while running | Reads fail → fins until restart; logged once |

`/api/info` gains `"sd": {"mounted": bool, "logos": count | null, "error": string | null}`; the
web page's Firmware card shows it.

## 8. Tests

- `test_pack`: header validation (magic, version, kind, truncated index); binary search finds
  first, last and missing keys; CRC and bounds failures read as absent; built with
  `build_pack.py`'s own writer on a tiny fixture so the Python and C++ sides agree.
- `test_logo_store`: both sizes decode; 4 bpp and 8 bpp; alpha corners; a missing airline falls
  back.
- `test_shapes`: every family's outline is closed and inside its box; type lookup for one type
  per family plus rule-derived types and an unknown code; emitter-category fallback.
- Goldens: Closest Flight with a logo (both themes), Closest Flight with a fin, Board with mixed
  logos, fins and a registration (both themes), Alert with a logo, Radar and Map with mixed
  families.
- On hardware (recorded in `docs/HARDWARE_TESTING.md`): mount with and without a card, logo load
  time, frame times on the Board, heap with logos showing, flash size before and after.

## 9. Results (*as built*)

Measured on a CYD2USB with live traffic (details in `docs/HARDWARE_TESTING.md`, "SD card and logos (5a)"):

| Measure | Result |
|---|---|
| Flash | 1,840,641 → 1,872,113 B (+31,472 B; budget 45 KB; 1,872,329 B before the review fixes). The Arduino SD stack would have been 1,907,349 B (97.0 %) |
| Static RAM | 89,816 → 90,688 B (+872 B). Before the review's savings 91,808 B (+1,992 B, 1.5 KB of it two SD sector caches and two file slots); they took 1,120 B: one 520 B sector cache and 8 checkpoints (600 B), one file slot (192 B), the unit circle in flash (248 B), status literals (64 B) |
| `minheap` | 30,612 B soak, 33,916 B normal before the savings; 35,092 B over 2 minutes of normal use after (Arduino SD stack: 28,024 B with TLS BIGNUM failures) |
| Mount | 4-5 ms (FAT32, SDHC, 20 MHz); opening `logos.pak` and walking its 10,400-cluster chain ~0.2 s |
| Badge load | 20 airlines × 3 sizes from the card: average 5.0 / 5.9 / 6.4 ms at 25 / 30 / 40 px (3.7-7.7 ms) with the contiguous-file path, from 7.1 / 8.0 / 8.4 ms (5.5-10.2 ms) without; first measured in live traffic as 25 px 6.3-8.9 ms, 30 px 9.2-9.6 ms, 40 px 8.9-9.3 ms |
| Board steady frames | avg 8.6-10.3 ms, max 9.4-13.5 ms (2-3 aircraft); busy traffic not yet measured |

Still to check with someone at the board: boot with no card, pulling the card while running, and
the logos, fins and shapes by eye in both themes.
