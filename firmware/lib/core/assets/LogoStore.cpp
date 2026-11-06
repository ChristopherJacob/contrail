#include "LogoStore.h"

#include <cstring>

#include "services/Log.h"

namespace assets {

namespace {
uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
constexpr int kMaxSizes = 4;
constexpr int kPalChunk = 16;  // palette entries read at a time
}  // namespace

void LogoStore::begin(hal::IStorage* storage) {
    status_ = "";
    mounted_ = storage && storage->mounted();
    failed_ = false;
    checkLogged_ = false;
    pack_ = Pack{};
    if (!mounted_) return;
    hal::IFile* f = storage->open(kPath);
    if (!f) {
        status_ = "no logos.pak";
        svc::logf("[sd] no logos.pak");
        return;
    }
    if (pack_.open(f, Pack::kKindLogos)) {
        svc::logf("[sd] logos.pak: %u logos", unsigned(pack_.count()));
        return;
    }
    // Literals, not a formatted buffer: the status costs no RAM. The log has the version found.
    const char* why;
    switch (pack_.error()) {
        case PackError::BadVersion:
            why = pack_.version() > Pack::kVersion ? "logos.pak is a newer version; update the firmware"
                                                   : "logos.pak is an older version; rebuild it";
            svc::logf("[sd] logos.pak is version %u; this firmware reads %u", unsigned(pack_.version()),
                      unsigned(Pack::kVersion));
            break;
        case PackError::BadMagic: why = "logos.pak is not a Contrail pack"; break;
        case PackError::BadKind: why = "logos.pak is not a logo pack"; break;
        default: why = "logos.pak is truncated"; break;
    }
    status_ = why;
    svc::logf("[sd] %s", why);
}

void LogoStore::readFailed() {
    if (failed_.exchange(true)) return;
    status_ = "SD card read failed";
    svc::logf("[sd] read failed: logos off until restart");
}

// A malformed item (bad CRC or a header out of bounds): that airline shows its fin. Logged once in all, with
// the first code, so a bad card cannot flood the log as flights come and go.
void LogoStore::checkFailed(const char* icao) {
    if (checkLogged_.exchange(true)) return;
    svc::logf("[sd] some logos failed their checks (first: %s)", icao);
}

bool LogoStore::loadSize(const char* icao, int side, int bpp, int maxColours, uint16_t* pal, uint8_t* alpha,
                         uint8_t* px, size_t pxBytes, gfx::Badge& badge) {
    if (failed_ || !pack_.isOpen() || !icao || !icao[0]) return false;
    uint32_t off, size;
    if (!pack_.find(icao, off, size)) {
        if (pack_.readFailed()) readFailed();  // the index read failed, not merely an unknown airline
        return false;
    }
    uint8_t head[1 + 8 * kMaxSizes];
    if (size < 1) return checkFailed(icao), false;
    if (!pack_.read(off, head, 1)) return readFailed(), false;
    const int n = head[0];
    if (n < 1 || n > kMaxSizes || size < uint32_t(1 + 8 * n)) return checkFailed(icao), false;
    if (!pack_.read(off + 1, head + 1, uint32_t(8 * n))) return readFailed(), false;
    // A palette index is bpp bits wide, so more than 1 << bpp colours is malformed even if the store had room.
    const int colourCap = maxColours < (1 << bpp) ? maxColours : (1 << bpp);
    const uint32_t pixels = uint32_t((side * bpp + 7) / 8) * uint32_t(side);
    if (pixels > pxBytes) return false;
    for (int i = 0; i < n; ++i) {
        const uint8_t* d = head + 1 + 8 * i;
        if (d[0] != side || d[1] != side || d[2] != bpp) continue;
        const int colours = d[3];
        const uint32_t at = le32(d + 4);
        const uint32_t block = uint32_t(colours) * 3 + pixels;
        if (colours < 1 || colours > colourCap || at > size || size - at < block + 2) return checkFailed(icao), false;
        // The palette (3 bytes an entry) and the CRC through a small buffer; the pixels straight into place.
        uint16_t crc = 0xFFFF;
        uint8_t entry[3 * kPalChunk];
        for (int c = 0; c < colours; c += kPalChunk) {
            const int k = colours - c < kPalChunk ? colours - c : kPalChunk;
            if (!pack_.read(off + at + uint32_t(3 * c), entry, uint32_t(3 * k))) return readFailed(), false;
            crc = crc16(entry, size_t(3 * k), crc);
            for (int j = 0; j < k; ++j) {
                pal[c + j] = uint16_t(entry[3 * j] | entry[3 * j + 1] << 8);
                alpha[c + j] = entry[3 * j + 2];
            }
        }
        uint8_t tail[2];
        if (!pack_.read(off + at + uint32_t(3 * colours), px, pixels) || !pack_.read(off + at + block, tail, 2))
            return readFailed(), false;
        crc = crc16(px, pixels, crc);
        if (crc != uint16_t(tail[0] | tail[1] << 8)) return checkFailed(icao), false;
        badge = gfx::Badge{uint8_t(side), uint8_t(side), uint8_t(bpp), uint8_t(colours), pal, alpha, px};
        return true;
    }
    return false;
}

}  // namespace assets
