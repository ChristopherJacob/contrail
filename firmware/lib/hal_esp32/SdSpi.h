#pragma once
// A raw SPI-mode SD card driver on VSPI: init, single- and multi-block reads (CMD17, CMD18) and writes (CMD24,
// CMD25; writes only where FatFile::writeAt sends them: inside map.pak). Data CRCs are checked on reads, and command CRCs are on
// (CMD59) so a write cannot go to a sector the wire garbled. Feeds assets::FatVolume in place of
// the Arduino SD/FATFS stack.
//
// Clock: 20 MHz, stepping down to 10 MHz on signal errors. 40 MHz (CMD6 high speed) was tried and refused on this
// board: through the GPIO matrix reads top out near 26 MHz, and with the pins on the IO_MUX the card refused CMD17.
//
// Built on the Arduino SPIClass, which LovyanGFX already links, so it adds almost nothing; the IDF spi_master
// transaction path (spi_device_polling_transmit) is not otherwise linked. LovyanGFX re-begins VSPI (Arduino's
// default SPI bus) only when the panel is on VSPI; this board's panel is on HSPI, so the bus is ours alone.
#include <cstdint>

#include "assets/FatReader.h"
#include "assets/SdProtocol.h"

namespace hal {

class SdSpi : public assets::IBlockDevice {
   public:
    // Wakes and initialises the card; false with error() set when there is none or it will not answer. Worst
    // cases: an empty slot ~3 ms (eight CMD0s at 400 kHz); a card that answers CMD0 and then stalls ~3.5 s
    // (busy waits of 300 ms on CMD8, CMD58 and CMD16, and ACMD41's 2 s plus one last 600 ms try).
    bool begin(int sck, int miso, int mosi, int cs);
    // Worst case ~0.8 s: a 300 ms busy wait and a 100 ms token wait, then the same again a rung slower.
    bool readBlock(uint32_t lba, uint8_t* buf512) override;
    // False without command CRCs (the card refused CMD59). Worst case ~1.1 s: a failed attempt (a 300 ms busy
    // wait before CMD24), then the retry a rung slower (300 ms and up to 500 ms of programming; a card still busy
    // then is not retried). FatFile::writeAt reads a partial sector first (~0.8 s worst).
    bool writeBlock(uint32_t lba, const uint8_t* buf512) override;
    // n consecutive blocks in one command (CMD18, CMD25), with the same checks and the same one retry a rung slower
    // (of the whole run). Worst cases, per attempt and twice that with the retry: a read ~0.6 s + 100 ms a block (a
    // 300 ms busy wait, a 100 ms token wait per block, CMD12 sent up to three times and its 300 ms busy wait; a
    // 15-block band ~2.1 s); a write ~0.8 s + 500 ms a block (a 300 ms busy wait, up to 500 ms of programming per
    // block, the stop token's 500 ms; a block still busy gets 500 ms more, then the transfer ends; a band ~8.3 s).
    bool readBlocks(uint32_t lba, uint8_t* buf, uint32_t n) override;
    bool writeBlocks(uint32_t lba, const uint8_t* buf, uint32_t n) override;

    const char* error() const { return error_; }
    bool highCapacity() const { return sdhc_; }  // SDHC/SDXC: block addressed
    uint32_t hz() const { return hz_; }
    bool crcOn() const { return crcOn_; }  // CMD59 accepted: writes are allowed

   private:
    uint8_t command(uint8_t cmd, uint32_t arg);
    assets::sd::Read readBlockAt(uint32_t lba, uint8_t* buf);
    assets::sd::Write writeBlockAt(uint32_t lba, const uint8_t* buf);
    assets::sd::Read readBlocksAt(uint32_t lba, uint8_t* buf, uint32_t n);
    assets::sd::Write writeBlocksAt(uint32_t lba, const uint8_t* buf, uint32_t n);
    bool stopRead();
    void stopWrite();
    void select();
    void deselect();
    bool fail(const char* why) {
        deselect();
        error_ = why;
        return false;
    }

    int cs_ = -1;
    bool ready_ = false;
    bool sdhc_ = false;
    bool crcOn_ = false;  // CMD59 accepted: the card checks command CRCs
    uint32_t hz_ = 400000;
    const char* error_ = "";
};

}  // namespace hal
