#pragma once
// The CYD's microSD slot on VSPI (5a spec §1), separate from the display's HSPI bus: our own SPI-mode driver
// (SdSpi) under a FAT16/FAT32 reader (assets::FatVolume; read-only except for overwriting inside map.pak).
// No heap, about 1.3 KB of static RAM (the two-way sector cache and two file slots).
#include "SdSpi.h"
#include "assets/FatReader.h"
#include "hal.h"

namespace hal {

class Esp32Storage : public IStorage {
   public:
    // Mounts the card; false (and mounted() false) with no card or an unreadable one, error() saying why.
    // Never fatal. With no card it gives up after a few CMD0 tries (a few ms).
    bool begin();
    bool mounted() const override { return vol_.mounted(); }
    // Opening a path that is already open returns the same file (as NativeStorage).
    IFile* open(const char* path) override { return vol_.open(path); }
    bool writable() const override { return sd_.crcOn(); }  // no writes without command CRCs (CMD59)

    const char* error() const { return sd_.error()[0] ? sd_.error() : vol_.error(); }
    // "FAT32, SDHC, 20 MHz, CRC on" for the boot log.
    void describe(char* out, int n) const;

   private:
    SdSpi sd_;
    assets::FatVolume vol_;
};

}  // namespace hal
