// SD loader (the cyd_sdload env): writes files to the microSD card over the USB serial link, so the logo
// pack can be put on the card without taking it out of the board; the documented route in README.md and
// docs/HARDWARE_TESTING.md. Host side: tools/sd_upload.py. The display stays off.
//
// On boot it mounts the card (Arduino SD on its own VSPI bus) and prints the card type, size, FAT
// type and a listing of / and /contrail. If the card does not mount it says why and stops: it
// never formats anything.
//
// Protocol (lines end in \n; every reply to a command ends with an "OK ..." or "ERR ..." line):
//   PING                     -> OK READY
//   LS <dir>                 -> one "F <size> <name>" / "D <name>" line per entry, then OK <n>
//   CRC <path>               -> OK <size> <crc32 hex>
//   PUT <path> <size> <crc>  -> GO <chunk>, then per chunk the host sends min(chunk, left) bytes
//                               followed by the chunk's CRC32 (4 bytes, little-endian); the device
//                               answers A <received> (written) or N <received> (resend it). After
//                               the last chunk the device reads the file back and, if the CRC
//                               matches, renames <path>.tmp over <path>: OK <size> <crc32>.
// The only files it ever deletes are /contrail/logos.pak, /contrail/map.pak and their .tmp files; a PUT to any
// other path that already exists (or has a leftover .tmp) is refused.
#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>

#include "board_cyd.h"
#include "ff.h"

#ifndef SDLOAD_BAUD
#define SDLOAD_BAUD 460800
#endif

namespace {

using board::kSdCs, board::kSdMiso, board::kSdMosi, board::kSdSck;
constexpr uint32_t kSdHz = 20000000;
constexpr size_t kChunk = 4096;
constexpr uint32_t kByteTimeoutMs = 3000;
constexpr int kMaxRetries = 8;
const char* const kDeletable[] = {"/contrail/logos.pak", "/contrail/logos.pak.tmp", "/contrail/map.pak",
                                  "/contrail/map.pak.tmp"};

SPIClass sdSpi(VSPI);
bool mounted = false;
uint8_t buf[kChunk + 4];
uint32_t crcTable[256];

void crcInit() {
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crcTable[i] = c;
    }
}

// Running CRC32 (zlib): start with 0, feed the previous result back in.
uint32_t crcUpdate(uint32_t crc, const uint8_t* p, size_t n) {
    crc = ~crc;
    while (n--) crc = crcTable[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

bool deletable(const String& path) {
    for (const char* d : kDeletable)
        if (path == d) return true;
    return false;
}

// A failed upload's .tmp is removed only when it is on the deletable list; otherwise it stays.
void dropTmp(const String& tmp) {
    if (deletable(tmp)) SD.remove(tmp);
}

const char* cardTypeName(uint8_t t) {
    switch (t) {
        case CARD_MMC: return "MMC";
        case CARD_SD: return "SDSC";
        case CARD_SDHC: return "SDHC/SDXC";
        case CARD_NONE: return "none";
        default: return "unknown";
    }
}

const char* fatTypeName() {
    FATFS* fs = nullptr;
    DWORD freeClusters = 0;
    if (f_getfree("0:", &freeClusters, &fs) != FR_OK || !fs) return "unknown";
    switch (fs->fs_type) {
        case FS_FAT12: return "FAT12";
        case FS_FAT16: return "FAT16";
        case FS_FAT32: return "FAT32";
#ifdef FS_EXFAT
        case FS_EXFAT: return "exFAT";
#endif
        default: return "unknown";
    }
}

// Lists one directory; returns the entry count, or -1 if it cannot be opened.
int listDir(const char* path) {
    File dir = SD.open(path);
    if (!dir || !dir.isDirectory()) return -1;
    int n = 0;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        if (f.isDirectory())
            Serial.printf("D %s\n", f.name());
        else
            Serial.printf("F %u %s\n", (unsigned)f.size(), f.name());
        ++n;
    }
    return n;
}

bool fileCrc(const String& path, uint32_t& size, uint32_t& crc) {
    File f = SD.open(path, FILE_READ);
    if (!f || f.isDirectory()) return false;
    size = 0;
    crc = 0;
    for (;;) {
        int n = f.read(buf, kChunk);
        if (n <= 0) break;
        crc = crcUpdate(crc, buf, n);
        size += n;
    }
    return true;
}

void drainInput(uint32_t quietMs) {
    uint32_t last = millis();
    while (millis() - last < quietMs) {
        if (Serial.available()) {
            Serial.read();
            last = millis();
        }
        delay(1);
    }
}

// Reads exactly n bytes; false on a gap longer than kByteTimeoutMs.
bool readExact(uint8_t* p, size_t n) {
    size_t got = 0;
    uint32_t last = millis();
    while (got < n) {
        int a = Serial.available();
        if (a > 0) {
            got += Serial.readBytes(p + got, min((size_t)a, n - got));
            last = millis();
        } else if (millis() - last > kByteTimeoutMs) {
            return false;
        } else {
            delay(0);
        }
    }
    return true;
}

void cmdPut(const String& path, uint32_t size, uint32_t want) {
    if (!path.startsWith("/") || path.endsWith("/")) {
        Serial.println("ERR bad path");
        return;
    }
    const String tmp = path + ".tmp";
    if (SD.exists(tmp)) {
        if (!deletable(tmp)) {
            Serial.println("ERR leftover .tmp exists and is not deletable");
            return;
        }
        SD.remove(tmp);
    }
    if (SD.exists(path) && !deletable(path)) {
        Serial.println("ERR target exists and is not deletable");
        return;
    }
    const int slash = path.lastIndexOf('/');
    if (slash > 0) {
        const String dir = path.substring(0, slash);
        if (!SD.exists(dir) && !SD.mkdir(dir)) {
            Serial.println("ERR mkdir failed");
            return;
        }
    }
    File f = SD.open(tmp, FILE_WRITE);
    if (!f) {
        Serial.println("ERR open .tmp failed");
        return;
    }
    Serial.printf("GO %u\n", (unsigned)kChunk);
    uint32_t done = 0, crc = 0;
    const uint32_t t0 = millis();
    while (done < size) {
        const size_t n = min((uint32_t)kChunk, size - done);
        int tries = 0;
        for (;;) {
            bool ok = readExact(buf, n + 4);
            if (ok) {
                uint32_t sent;
                memcpy(&sent, buf + n, 4);
                ok = crcUpdate(0, buf, n) == sent;
            }
            if (ok) break;
            if (++tries > kMaxRetries) {
                f.close();
                dropTmp(tmp);
                Serial.printf("ERR chunk at %u failed %d times\n", (unsigned)done, tries);
                return;
            }
            drainInput(100);
            Serial.printf("N %u\n", (unsigned)done);
        }
        if (f.write(buf, n) != n) {
            f.close();
            dropTmp(tmp);
            Serial.printf("ERR write failed at %u\n", (unsigned)done);
            return;
        }
        crc = crcUpdate(crc, buf, n);
        done += n;
        Serial.printf("A %u\n", (unsigned)done);
    }
    f.close();
    const uint32_t t1 = millis();
    if (crc != want) {
        dropTmp(tmp);
        Serial.printf("ERR received crc %08x, expected %08x\n", (unsigned)crc, (unsigned)want);
        return;
    }
    uint32_t rsize = 0, rcrc = 0;
    if (!fileCrc(tmp, rsize, rcrc) || rsize != size || rcrc != want) {
        dropTmp(tmp);
        Serial.printf("ERR readback %u bytes crc %08x, expected %u %08x\n", (unsigned)rsize,
                      (unsigned)rcrc, (unsigned)size, (unsigned)want);
        return;
    }
    const uint32_t t2 = millis();
    if (SD.exists(path) && !SD.remove(path)) {
        Serial.println("ERR could not remove old file");
        return;
    }
    if (!SD.rename(tmp, path)) {
        Serial.println("ERR rename failed");
        return;
    }
    Serial.printf("INFO receive %u ms, readback %u ms\n", (unsigned)(t1 - t0), (unsigned)(t2 - t1));
    Serial.printf("OK %u %08x\n", (unsigned)size, (unsigned)want);
}

void handle(String line) {
    line.trim();
    if (line.isEmpty()) return;
    const int sp = line.indexOf(' ');
    const String cmd = sp < 0 ? line : line.substring(0, sp);
    const String rest = sp < 0 ? String() : line.substring(sp + 1);
    if (cmd == "PING") {
        Serial.println("OK READY");
    } else if (cmd == "LS") {
        const int n = listDir(rest.isEmpty() ? "/" : rest.c_str());
        if (n < 0)
            Serial.println("ERR not a directory");
        else
            Serial.printf("OK %d\n", n);
    } else if (cmd == "CRC") {
        uint32_t size, crc;
        if (fileCrc(rest, size, crc))
            Serial.printf("OK %u %08x\n", (unsigned)size, (unsigned)crc);
        else
            Serial.println("ERR cannot open");
    } else if (cmd == "PUT") {
        char path[128];
        unsigned size = 0, crc = 0;
        if (sscanf(rest.c_str(), "%127s %u %x", path, &size, &crc) != 3) {
            Serial.println("ERR usage: PUT <path> <size> <crc32hex>");
            return;
        }
        cmdPut(String(path), size, crc);
    } else {
        Serial.println("ERR unknown command");
    }
}

}  // namespace

void setup() {
    Serial.setRxBufferSize(kChunk * 2 + 64);
    Serial.begin(SDLOAD_BAUD);
    delay(300);
    crcInit();
    Serial.println();
    Serial.println("[sdload] SD loader, baud " + String(SDLOAD_BAUD));
    sdSpi.begin(kSdSck, kSdMiso, kSdMosi, kSdCs);
    const uint32_t t0 = millis();
    if (!SD.begin(kSdCs, sdSpi, kSdHz)) {
        Serial.println("[sdload] FATAL mount failed (no card, not FAT, or wiring); stopping");
        return;
    }
    const uint8_t type = SD.cardType();
    if (type == CARD_NONE) {
        Serial.println("[sdload] FATAL no card; stopping");
        return;
    }
    Serial.printf("[sdload] mounted in %u ms\n", (unsigned)(millis() - t0));
    Serial.printf("[sdload] card %s, %llu MB, fs %s, total %llu MB, used %llu MB\n",
                  cardTypeName(type), SD.cardSize() / (1024ull * 1024), fatTypeName(),
                  SD.totalBytes() / (1024ull * 1024), SD.usedBytes() / (1024ull * 1024));
    Serial.println("[sdload] ls /");
    listDir("/");
    Serial.println("[sdload] ls /contrail");
    if (listDir("/contrail") < 0) Serial.println("(none)");
    mounted = true;
    Serial.println("[sdload] READY");
}

void loop() {
    static String line;
    static uint32_t lastBanner = 0;
    if (!mounted) {
        if (millis() - lastBanner > 5000) {
            lastBanner = millis();
            Serial.println("[sdload] STOPPED: card not usable");
        }
        delay(10);
        return;
    }
    while (Serial.available()) {
        const char c = (char)Serial.read();
        if (c == '\n') {
            handle(line);
            line = "";
        } else if (c != '\r' && line.length() < 256) {
            line += c;
        }
    }
    delay(1);
}
