# Contrail on the Waveshare ESP32-S3-Touch-LCD-2.8

A second board for Contrail: Waveshare's ESP32-S3-Touch-LCD-2.8, offered on the Install page next to the CYD.

## Why this board

It has the CYD's resolution and orientation (240x320, portrait) and the same panel controller (ST7789), so
`lib/core` (screens, renderer, goldens; about 1,650 hard-coded 240/320 coordinates) runs unchanged. Only the
HAL changes.

## The board

| Part | CYD (ESP32-2432S028R) | Waveshare 2.8 |
|---|---|---|
| Chip | ESP32, 4 MB flash, no PSRAM | ESP32-S3, 16 MB flash, 8 MB octal PSRAM |
| USB | CH340 bridge | the S3's own USB (serial is USB CDC) |
| Panel | ILI9341 or ST7789 on HSPI (14/13/12, CS 15, DC 2), BL 21 | ST7789 on SPI2 (SCLK 40, MOSI 45, CS 42, DC 41, RST 39), BL 5, inversion on |
| Touch | XPT2046 resistive, software SPI | capacitive on I2C (SDA 1, SCL 3, INT 4, RST 2): CST3530 at 0x58 (V2, shipping since June 2026) or CST328 at 0x1A (V1) |
| microSD | SPI on VSPI (18/23/19, CS 5) | SDMMC wiring used in SPI mode on SPI3 (SCK 14, MOSI=CMD 17, MISO=D0 16, CS=D3 21) |
| RGB LED, LDR | yes | none |
| Extras | none | I2S DAC + 2 speaker ports, PCF85063 RTC, QMI8658 IMU, battery charger (unused) |

Sources: Waveshare's wiki and docs, CircuitPython's board definition (`waveshare_esp32_s3_touch_lcd_2_8`) for the
pins and the panel's init (INVON, RGB order), Waveshare's `esp_lcd_touch_cst3530` component and ESPHome's
`cst328` driver for the touch protocols. Checked on a V2 board: picture, touch axes (top-left reads 17,40,
bottom-right 206,281), polling, the web update.

## Design

- **One firmware per board.** A PlatformIO env `ws28` (`-DCONTRAIL_BOARD_WS28`) next to `cyd2usb`. `board.h`
  picks `board_cyd.h` or `board_ws28.h`; both give the pins and the settings' "auto" panel values, plus
  `kHasLed` and `kHasLightSensor`.
- **Same HAL classes.** `Esp32Display`, `Esp32Backlight`, `Esp32Led` and `Esp32Light` keep their names and
  interfaces, so `main_esp32.cpp` is unchanged. On the Waveshare:
  - the display is LovyanGFX's ST7789 on SPI2 at 80 MHz;
  - touch is `Ws28Touch`, a small Wire driver that finds whichever controller answers and polls it every 10 ms.
    The 180° rotation setting flips its coordinates;
  - the LED does nothing, and the light sensor reads a fixed 800, so "auto" brightness holds at about 80% and
    alerts pulse only the backlight.
- **SD card.** `SdSpi` runs unchanged on SPI3 (Arduino's `HSPI` on the S3), because the panel holds SPI2.
- **Flash layout.** `default_16MB.csv`: two 6.5 MB app slots for the web update. The NVS partition is the same
  size as on the CYD.
- **Web update.** The upload handler checks the image header's chip ID (byte 12) against the running chip
  before writing, so a CYD image can't be written to the Waveshare, or the reverse ("this firmware is for a
  different board").
- **Release and site.** `release_assets.sh` merges each board's image at its own offsets (the S3's bootloader
  is at 0x0) and passes esptool the matching `--chip`. Pages downloads the install images the latest release
  actually has and lists them in `release.json` (`models`). The Install page greys out a board whose image is
  missing ("next release"), so the Waveshare can be offered before the first release that includes it.

## Not done yet

- SD card on this board: the code path is the CYD's, on the new pins, but no card has been tried.
- Battery level (GPIO8 ADC) and power latch (GPIO7), RTC and IMU are unused.

## Alert chime (added after the port)

- `audio/Chime` in lib/core synthesises the chime: two exponentially decaying, nearly pure tones (D5 587 Hz, then
  B4 494 Hz 1.02 s later; third harmonic -45 dB; 50 ms raised-cosine onset, ~29 ms 10-90 %; amplitude time
  constant 0.18 s, ~48 dB/s), 2.3 s at 22.05 kHz. Every figure was measured from a royalty-free recording
  (Pixabay "airplane-chime" by saboteurcomics). A first draft built as a struck bar (bright partials, 4 ms
  attack, major third) sounded like a doorbell. Each partial is a two-pole resonator: sin and exp for every
  sample took tens of seconds on the S3.
- `Esp32Speaker` (hal_esp32) drives the PCM5101 over I2S (BCK 48, LRCK 38, DOUT 47; no MCLK). A task on core
  0 at idle priority renders the chime into PSRAM once at boot (~2.8 s; at priority 1 it starved IDLE0 and
  the task watchdog reset the board), then plays it on request. A new request restarts it.
- The chime plays once per alert, when the alert first lights (not again when the touch panel closes over it),
  unless it is soft (quiet hours) or `alert_sound` is off; the volume is `alert_volume` (0-100, square law).
  `/api/info` reports `speaker`, and the web page shows the two settings and a Play chime button
  (`POST /api/chime {"volume": n}`) only when it is true.
- V1 boards (CST328 touch): written from ESPHome's driver, not tried on a board.
- Two boards on one network both answer to `contrail.local`.
