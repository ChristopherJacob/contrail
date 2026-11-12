#include "SdSpi.h"

#include <Arduino.h>
#include <SPI.h>
#include <driver/gpio.h>

#include "assets/SdProtocol.h"

namespace hal {

namespace {
// The CYD's panel is on HSPI, so the card has VSPI. On the ESP32-S3 the panel takes SPI2 (FSPI), and Arduino's HSPI
// is SPI3.
#if CONFIG_IDF_TARGET_ESP32S3
SPIClass sdBus(HSPI);
#else
SPIClass sdBus(VSPI);
#endif
bool selected = false;

using namespace assets::sd;
constexpr uint8_t kStartWrite = kStartBlock;  // before a single-block write's data (CMD24)

// Clocks until the card lets go of MISO (a busy card holds it low); false after `ms`.
bool waitReady(uint32_t ms) {
    const uint32_t t0 = millis();
    while (sdBus.transfer(0xFF) != 0xFF)
        if (millis() - t0 > ms) return false;
    return true;
}
}  // namespace

void SdSpi::select() {
    if (selected) return;
    sdBus.beginTransaction(SPISettings(hz_, MSBFIRST, SPI_MODE0));
    digitalWrite(cs_, LOW);
    selected = true;
}

void SdSpi::deselect() {
    if (!selected) return;
    digitalWrite(cs_, HIGH);
    sdBus.transfer(0xFF);  // eight clocks so the card lets go of MISO
    sdBus.endTransaction();
    selected = false;
}

// Sends a command and returns its R1 (0xFF: no answer). The frame always carries its CRC-7: the card ignores it
// in SPI mode except for CMD0, CMD8 and once CMD59 has turned checking on.
uint8_t SdSpi::command(uint8_t cmd, uint32_t arg) {
    if (cmd != 0 && !waitReady(kBusyMs)) return 0xFF;  // wait out a busy card (MISO held low)
    const uint8_t frame[6] = {uint8_t(0x40 | cmd), uint8_t(arg >> 24), uint8_t(arg >> 16), uint8_t(arg >> 8),
                              uint8_t(arg), commandCrc(cmd, arg)};
    sdBus.writeBytes(frame, sizeof frame);
    uint8_t r = 0xFF;
    for (int i = 0; i < 10 && (r & 0x80); ++i) r = sdBus.transfer(0xFF);
    return r;
}

bool SdSpi::begin(int sck, int miso, int mosi, int cs) {
    cs_ = cs, ready_ = false, sdhc_ = false, crcOn_ = false, hz_ = kInitHz;
    pinMode(cs_, OUTPUT);
    digitalWrite(cs_, HIGH);
    sdBus.begin(sck, miso, mosi, -1);
    gpio_pullup_en(gpio_num_t(miso));  // an empty slot reads 0xFF, not a floating line

    // 80 clocks with CS high put the card into native mode, ready for CMD0.
    sdBus.beginTransaction(SPISettings(hz_, MSBFIRST, SPI_MODE0));
    for (int i = 0; i < 10; ++i) sdBus.transfer(0xFF);
    sdBus.endTransaction();

    select();
    uint8_t r = 0xFF;
    for (int i = 0; i < 8 && r != 0x01; ++i) r = command(0, 0);  // GO_IDLE_STATE: SPI mode
    if (r != 0x01) return fail(r == 0xFF ? "no card" : "card did not reset (CMD0)");

    bool v2 = false;
    r = command(8, 0x1AA);  // SEND_IF_COND: 2.7-3.6 V, check pattern 0xAA
    const IfCond ic = ifCond(r);
    if (ic == IfCond::V2) {
        uint8_t r7[4];
        sdBus.transferBytes(nullptr, r7, sizeof r7);
        if ((r7[2] & 0x0F) != 0x01 || r7[3] != 0xAA) return fail("card refused 3.3 V (CMD8)");
        v2 = true;
    } else if (ic == IfCond::NoAnswer) {
        return fail("card did not answer CMD8");
    }

    const uint32_t t0 = millis();
    do {
        command(55, 0);
        r = command(41, v2 ? 0x40000000 : 0);  // SD_SEND_OP_COND, HCS for 2.0 cards
    } while (r == 0x01 && millis() - t0 < kIdleMs);
    if (r != 0x00) return fail(r == 0x01 ? "card stayed busy (ACMD41)" : "card refused ACMD41 (MMC?)");

    // Command CRC checking on, so a bit error in a later address is refused rather than obeyed. Sent after
    // ACMD41 (CMD59 is valid in any state, and then the card checks every command from here on). A card that
    // refuses stays usable for reads; writeBlock will not write to it.
    crcOn_ = command(59, 1) == 0x00;
    if (v2) {
        if (command(58, 0) != 0x00) return fail("card did not answer CMD58");
        uint8_t ocr[4];
        sdBus.transferBytes(nullptr, ocr, sizeof ocr);
        sdhc_ = ocr[0] & 0x40;  // CCS: block addressed
    }
    if (!sdhc_ && command(16, 512) != 0x00) return fail("card refused 512-byte blocks (CMD16)");
    deselect();
    hz_ = kFastHz;
    ready_ = true;
    error_ = "";
    return true;
}

// Ok, or why not (see assets::sd::Read).
Read SdSpi::readBlockAt(uint32_t lba, uint8_t* buf) {
    select();
    const Read r1 = cmd17(command(17, sdhc_ ? lba : lba * 512));
    if (r1 != Read::Ok) return fail(r1 == Read::Signal ? "read: no answer (CMD17)" : "read refused (CMD17)"), r1;
    const uint32_t t0 = millis();
    uint8_t token;
    while ((token = sdBus.transfer(0xFF)) == 0xFF)
        if (millis() - t0 > kTokenMs) return fail("read timed out"), Read::Signal;
    if (token != kStartBlock) return fail("read error token"), Read::Signal;
    sdBus.transferBytes(nullptr, buf, 512);
    const uint16_t crc = sdBus.transfer16(0xFFFF);
    deselect();
    // The data CRC (CRC-16/XMODEM: CCITT polynomial, initial 0).
    if (dataCrc(buf) != crc) return fail("read CRC mismatch"), Read::Signal;
    return Read::Ok;
}

// Ok, or why not (see assets::sd::Write). The data response is read before CS goes high, then the card's
// programming is waited out (it holds MISO low), so the next command finds it ready.
Write SdSpi::writeBlockAt(uint32_t lba, const uint8_t* buf) {
    select();
    const uint8_t r1 = command(24, sdhc_ ? lba : lba * 512);
    const Write c = cmd24(r1);
    if (c != Write::Ok)
        return fail(r1 & kR1Invalid ? "write: no answer (CMD24)" : r1 & kR1CrcError ? "write: command CRC error (CMD24)"
                                                                               : "write refused (CMD24)"),
               c;
    sdBus.transfer(0xFF);  // one byte of gap before the token
    sdBus.transfer(kStartWrite);
    sdBus.writeBytes(buf, 512);
    sdBus.transfer16(dataCrc(buf));  // checked by the card, which answers CRC error (CMD59 is on for any card we write to)
    const Write w = dataResponse(sdBus.transfer(0xFF));
    if (w != Write::Ok) return fail(w == Write::Signal ? "write: no data response" : "write error"), w;
    if (!waitReady(kWriteBusyMs)) return fail("write timed out"), Write::Refused;  // busy, not the clock
    deselect();
    return Write::Ok;
}

// CMD18: n blocks in one command, each checked against its data CRC, then CMD12. Stopped early on any failure.
Read SdSpi::readBlocksAt(uint32_t lba, uint8_t* buf, uint32_t n) {
    select();
    const Read r1 = cmd18(command(18, sdhc_ ? lba : lba * 512));
    if (r1 == Read::Signal) {  // the R1 may be lost while the card took the command and is streaming: stop it
        stopRead();
        return fail("read: no answer (CMD18)"), r1;
    }
    if (r1 != Read::Ok) return fail("read refused (CMD18)"), r1;
    Read r = Read::Ok;
    for (uint32_t i = 0; i < n && r == Read::Ok; ++i) {
        const uint32_t t0 = millis();
        uint8_t token;
        while ((token = sdBus.transfer(0xFF)) == 0xFF && millis() - t0 <= kTokenMs) {
        }
        r = dataToken(token);
        if (r != Read::Ok) break;
        uint8_t* b = buf + 512 * i;
        sdBus.transferBytes(nullptr, b, 512);
        if (dataCrc(b) != sdBus.transfer16(0xFFFF)) r = Read::Signal;
    }
    const bool stop = stopRead();
    if (r != Read::Ok) return fail("read: bad block (CMD18)"), r;
    if (!stop) return fail("read: no answer (CMD12)"), Read::Signal;
    deselect();
    return Read::Ok;
}

// CMD12 with the frame sent straight away (the card is streaming data, so no busy wait first), the stuff byte, R1,
// then the card's busy time; sent up to three times while the card does not answer or reports the frame garbled.
// Whether it stopped.
bool SdSpi::stopRead() {
    const uint8_t frame[6] = {uint8_t(0x40 | 12), 0, 0, 0, 0, commandCrc(12, 0)};
    for (int attempt = 0; attempt < 3; ++attempt) {
        sdBus.writeBytes(frame, sizeof frame);
        sdBus.transfer(0xFF);  // the stuff byte
        uint8_t r = 0xFF;
        for (int i = 0; i < 10 && (r & 0x80); ++i) r = sdBus.transfer(0xFF);
        if (stopped(r)) return waitReady(kBusyMs);
    }
    return false;
}

// Ends a CMD25 write the card may still be in: the stop token, a byte, then its busy time.
void SdSpi::stopWrite() {
    sdBus.transfer(kStopTran);
    sdBus.transfer(0xFF);  // a byte before the card shows busy
    waitReady(kWriteBusyMs);
}

// CMD25: n blocks behind one command, each with its data response and programming wait, then the stop token.
Write SdSpi::writeBlocksAt(uint32_t lba, const uint8_t* buf, uint32_t n) {
    select();
    const uint8_t r1 = command(25, sdhc_ ? lba : lba * 512);
    const Write c = cmd25(r1);
    if (r1 & kR1Invalid) stopWrite();  // R1 lost: the card may have taken CMD25 and be waiting for data
    if (c != Write::Ok)
        return fail(r1 & kR1Invalid ? "write: no answer (CMD25)" : r1 & kR1CrcError ? "write: command CRC error (CMD25)"
                                                                               : "write refused (CMD25)"),
               c;
    sdBus.transfer(0xFF);  // one byte of gap before the first token
    for (uint32_t i = 0; i < n; ++i) {
        const uint8_t* b = buf + 512 * i;
        sdBus.transfer(kStartMultiWrite);
        sdBus.writeBytes(b, 512);
        sdBus.transfer16(dataCrc(b));
        const Write w = dataResponse(sdBus.transfer(0xFF));
        if (w != Write::Ok) {
            // A refused block ends the transfer: CMD12 (after the card's busy time) rather than the stop token.
            waitReady(kWriteBusyMs);
            command(12, 0);
            waitReady(kWriteBusyMs);
            return fail(w == Write::Signal ? "write: no data response" : "write error"), w;
        }
        if (!waitReady(kWriteBusyMs)) {  // busy, not the clock: give it longer, then end the transfer
            waitReady(kWriteBusyMs);
            stopWrite();
            return fail("write timed out"), Write::Refused;
        }
    }
    sdBus.transfer(kStopTran);
    sdBus.transfer(0xFF);  // a byte before the card shows busy
    if (!waitReady(kWriteBusyMs)) return fail("write timed out"), Write::Refused;
    deselect();
    return Write::Ok;
}

bool SdSpi::writeBlock(uint32_t lba, const uint8_t* buf) { return writeBlocks(lba, buf, 1); }

bool SdSpi::writeBlocks(uint32_t lba, const uint8_t* buf, uint32_t n) {
    if (!ready_) return false;
    if (!crcOn_) return error_ = "card refused CRC", false;  // no writes without command CRCs
    if (n == 0) return true;
    auto once = [&] { return n == 1 ? writeBlockAt(lba, buf) : writeBlocksAt(lba, buf, n); };
    const Write w = once();
    if (w == Write::Ok) return true;
    if (w != Write::Signal || !slower(hz_)) return false;
    hz_ = slower(hz_);  // as readBlocks: a rung slower from now on (select() applies hz_ at the next transaction)
    return once() == Write::Ok;
}

bool SdSpi::readBlock(uint32_t lba, uint8_t* buf) { return readBlocks(lba, buf, 1); }

bool SdSpi::readBlocks(uint32_t lba, uint8_t* buf, uint32_t n) {
    if (!ready_) return false;
    if (n == 0) return true;
    auto once = [&] { return n == 1 ? readBlockAt(lba, buf) : readBlocksAt(lba, buf, n); };
    const Read r = once();
    if (r == Read::Ok) return true;
    if (!retrySlower(r) || !slower(hz_)) return false;
    hz_ = slower(hz_);  // a marginal card or wiring: a rung slower from now on (20, then 10 MHz)
    return once() == Read::Ok;
}

}  // namespace hal
