#pragma once
// The decisions hal::SdSpi makes from SD card replies (SD Physical Layer spec, SPI mode), kept portable so the
// native tests can check them without a card.
#include <cstdint>

#include "Pack.h"  // crc16

namespace assets::sd {

constexpr uint32_t kInitHz = 400000, kFastHz = 20000000, kSafeHz = 10000000;
// The clock after a signal failure at `hz`: a rung down (20, then 10 MHz), or 0 when there is none.
inline uint32_t slower(uint32_t hz) { return hz > kSafeHz ? kSafeHz : 0; }
// Waits: a busy card (MISO low) before a command, the data token after CMD17, and ACMD41 leaving idle (and,
// below, a block being programmed).
constexpr uint32_t kBusyMs = 300, kTokenMs = 100, kIdleMs = 2000;
// The card programming a written block (MISO held low). SD cards may take up to 250 ms, some more.
constexpr uint32_t kWriteBusyMs = 500;

// R1 to CMD8 (SEND_IF_COND): 0x01 is a 2.0+ card with an R7 to read; 0x05 (idle + illegal command) an SD 1.x
// card; anything else, 0xFF (no answer) included, a card that is not there or not working.
enum class IfCond : uint8_t { V2, V1, NoAnswer };
inline IfCond ifCond(uint8_t r1) { return r1 == 0x01 ? IfCond::V2 : r1 == 0x05 ? IfCond::V1 : IfCond::NoAnswer; }

// How a single-block read ended. Signal failures (no answer, a token timeout, an error token, a CRC mismatch)
// may be the clock, so the driver retries once at kSafeHz; Refused (an address or parameter error in R1) is
// the request itself, and retrying slower would not help.
enum class Read : uint8_t { Ok, Signal, Refused };
// R1 bit 3 is "command CRC error": with command CRCs on, a bit error in the frame, so Signal as well. Bit 7 of an
// R1 is always 0: with it set the byte is no answer (0xFF) or noise (say 0xF0), never a card's refusal.
constexpr uint8_t kR1CrcError = 0x08, kR1Invalid = 0x80;
inline Read cmd17(uint8_t r1) {
    return r1 == 0x00 ? Read::Ok : (r1 & (kR1Invalid | kR1CrcError)) ? Read::Signal : Read::Refused;
}
inline bool retrySlower(Read r) { return r == Read::Signal; }

// How a single-block write ended, from the data response token (xxx0sss1: sss 010 accepted, 101 CRC error,
// 110 write error). A CRC error or no answer may be the clock (retry slower); a write error is the card's.
// A card still busy after kWriteBusyMs is not a clock problem, so that is Refused too (no retry at 10 MHz).
enum class Write : uint8_t { Ok, Signal, Refused };
inline Write cmd24(uint8_t r1) {
    return r1 == 0x00 ? Write::Ok : (r1 & (kR1Invalid | kR1CrcError)) ? Write::Signal : Write::Refused;
}
inline Write dataResponse(uint8_t token) {
    if ((token & 0x11) != 0x01) return Write::Signal;  // not a data response at all
    const uint8_t s = token >> 1 & 7;
    return s == 2 ? Write::Ok : s == 5 ? Write::Signal : Write::Refused;
}

// Multi-block transfers (CMD18 read, CMD25 write) answer their command as the single-block ones do, then repeat
// the data phase per block: each read block behind its own start token and data CRC, each written block behind
// kStartMultiWrite with its own data response. A read ends with CMD12 (STOP_TRANSMISSION), which the card answers
// after one stuff byte; a write ends with the kStopTran token, then the card's busy time.
constexpr uint8_t kStartBlock = 0xFE;       // before a read block, and before a single-block write's data
constexpr uint8_t kStartMultiWrite = 0xFC;  // before each block of a CMD25 write
constexpr uint8_t kStopTran = 0xFD;         // ends a CMD25 write
inline Read cmd18(uint8_t r1) { return cmd17(r1); }
inline Write cmd25(uint8_t r1) { return cmd24(r1); }
// What came where a read block's start token belongs: the token, or a failure the clock may cause (an error token,
// 0000xxxx, included: as for a single block, it is retried slower).
inline Read dataToken(uint8_t t) { return t == kStartBlock ? Read::Ok : Read::Signal; }
// R1 to the CMD12 that stops a multi-block read. The blocks were each CRC-checked already, and some cards flag a
// parameter or range error on a stop near the card's end, so those count as stopped. No answer (top bit set) or a
// command CRC error (the card ignored the CMD12 and is still sending) does not: the driver sends it again.
inline bool stopped(uint8_t r1) { return !(r1 & (kR1Invalid | kR1CrcError)); }

// The last byte of a command frame: CRC-7 (x^7 + x^3 + 1) over the command byte and the four argument bytes,
// shifted left with the end bit set. SPI mode checks it only for CMD0 and CMD8 until CMD59 turns checking on;
// the driver then sends the right one with every command, so a bit error in a CMD24 address cannot program
// another sector.
inline uint8_t commandCrc(uint8_t cmd, uint32_t arg) {
    const uint8_t frame[5] = {uint8_t(0x40 | cmd), uint8_t(arg >> 24), uint8_t(arg >> 16), uint8_t(arg >> 8),
                              uint8_t(arg)};
    uint8_t crc = 0;
    for (uint8_t byte : frame)
        for (int b = 7; b >= 0; --b) {
            const bool top = (crc >> 6 & 1) ^ (byte >> b & 1);
            crc = uint8_t(crc << 1 & 0x7F);
            if (top) crc ^= 0x09;
        }
    return uint8_t(crc << 1 | 1);
}

// The CRC an SD card sends after a data block: CRC-16/XMODEM (CCITT polynomial, initial value 0), as
// crc16(p, 512, 0) but four bits at a time from a 32-byte table: 56 us a block on the device, against 170 us
// bit by bit (a whole block read is ~520 us at 20 MHz).
inline uint16_t dataCrc(const uint8_t* p, uint32_t n) {
    static constexpr uint16_t kNibble[16] = {0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
                                             0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF};
    uint16_t crc = 0;
    for (uint32_t i = 0; i < n; ++i) {
        crc = uint16_t(crc << 4) ^ kNibble[(crc >> 12) ^ (p[i] >> 4)];
        crc = uint16_t(crc << 4) ^ kNibble[(crc >> 12) ^ (p[i] & 0x0F)];
    }
    return crc;
}
inline uint16_t dataCrc(const uint8_t* block512) { return dataCrc(block512, 512); }

}  // namespace assets::sd
