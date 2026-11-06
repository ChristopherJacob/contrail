#pragma once
// A CPAK file on removable storage (5a spec §2). tools/build_pack.py writes them; this is the layout as built,
// every integer little-endian.
//
//   Header, 32 bytes:
//     0  char[4] magic "CPAK"
//     4  u16     version (kVersion, 1)
//     6  u16     kind (kKindLogos, 1)
//     8  u32     count: index entries
//    12  u32     indexOffset (32: straight after the header)
//    16  u32     dataOffset: the first item (indexOffset + 12 * count)
//    20  u32     builtUnix
//    24  u8[8]   reserved, 0
//   Index, `count` entries of 12 bytes at indexOffset, sorted by key (byte order, so find() binary-searches):
//     0  char[4] key: the ICAO airline code, NUL-padded ("UAL\0")
//     4  u32     offset of the item from the start of the file
//     8  u32     size of the item in bytes
//   Items, back to back from dataOffset. A logo item (kind 1; LogoStore reads it):
//     0  u8      sizeCount: blocks in this item (3: 40, 30 and 25 px)
//     then sizeCount descriptors of 8 bytes:
//        u8 w, u8 h, u8 bpp (8 or 4), u8 colours (palette entries, 1..1 << bpp, at most 128),
//        u32 offset of the block from the start of the item
//     then the blocks. Each block:
//        colours x { u16 RGB565, u8 alpha }       the palette, 3 bytes an entry
//        h rows of ceil(w * bpp / 8) bytes        palette indices; at 4 bpp the left pixel in the high nibble,
//                                                 an odd row padded with a 0 nibble
//        u16 CRC-16/CCITT-FALSE (crc16 below) over the palette and pixels of this block alone
//     A block is checked on its own, so a 25 px Board badge reads ~400 B, not the whole ~3.7 KB item.
#include <cstddef>
#include <cstdint>

#include "hal.h"

namespace assets {

enum class PackError : uint8_t { None, NotOpen, BadMagic, BadVersion, BadKind, Truncated };

// CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF), as build_pack.py.
uint16_t crc16(const uint8_t* p, size_t n, uint16_t crc = 0xFFFF);

class Pack {
   public:
    static constexpr uint16_t kVersion = 1;
    static constexpr uint16_t kKindLogos = 1;
    // Validates the header and that the index lies inside the file. False (and error()) when refused.
    bool open(hal::IFile* f, uint16_t kind);
    bool isOpen() const { return f_ != nullptr; }
    PackError error() const { return error_; }
    uint16_t version() const { return version_; }  // as found, for the refusal message
    uint32_t count() const { return count_; }
    // The item for `key` (1..4 characters): its offset in the file and size. False when absent, out of bounds,
    // or the index could not be read (then readFailed()).
    bool find(const char* key, uint32_t& offset, uint32_t& size);
    bool read(uint32_t offset, void* buf, uint32_t n);
    // A read of this pack has failed since open() (the card was pulled); sticky until the next open(). Tells
    // "absent" from "unreadable" after find.
    bool readFailed() const { return readFailed_; }

   private:
    hal::IFile* f_ = nullptr;
    PackError error_ = PackError::NotOpen;
    uint16_t version_ = 0;
    uint32_t count_ = 0, indexOffset_ = 0;
    bool readFailed_ = false;
};

}  // namespace assets
