#pragma once
// Reading a map pack tile's features from the card (5b spec §3): a small buffered stream over an IFile, and
// readers for rings (the built-in map's encoding, Tiles.h), lines (the same steps, no cuts) and points.
// Nothing is loaded whole: a tile can be several KB and the chart has no RAM for it.
#include <cstdint>

#include "Tiles.h"
#include "hal.h"

namespace basemap {

// Bytes [begin, end) of a file, 32 at a time. After a short or failed read every byte reads as 0 and ok()
// turns false; readers stop on it.
class ByteStream {
   public:
    ByteStream(hal::IFile& f, uint32_t begin, uint32_t end) : f_(f), pos_(begin), begin_(begin), end_(end) {}
    uint8_t u8() {
        if (at_ == got_ && !fill()) return 0;
        return buf_[at_++];
    }
    uint16_t u16() {
        const uint8_t lo = u8();
        return uint16_t(lo | u8() << 8);
    }
    int16_t i16() { return int16_t(u16()); }
    // Within [begin, end) only: a position outside fails the stream (reads then give 0) rather than reading
    // other parts of the file. Nothing else is reset: a stream that has failed stays failed.
    void seek(uint32_t pos) {
        if (pos < begin_ || pos > end_) ok_ = false, pos = end_;
        pos_ = pos, at_ = got_ = 0;
    }
    uint32_t tell() const { return pos_ - (got_ - at_); }
    bool ok() const { return ok_; }

   private:
    bool fill();
    hal::IFile& f_;
    uint32_t pos_, begin_, end_;
    uint8_t buf_[32];
    uint8_t at_ = 0, got_ = 0;
    bool ok_ = true;
};

// One step: short (int8 dx, dy), medium (0x82, int12 pair), long (0x80, int16 pair). `lead` is the byte
// already read.
void readStep(ByteStream& s, uint8_t lead, int32_t& dx, int32_t& dy);

// A ring, as RingReader: after the last point it reports the first again with the closing edge's cut flag.
class StreamRingReader {
   public:
    explicit StreamRingReader(ByteStream& s);
    bool next(RingPoint& out);
    int count() const { return count_; }

   private:
    ByteStream& s_;
    int count_ = 0, left_ = 0;
    int32_t x0_ = 0, y0_ = 0, x_ = 0, y_ = 0;
    bool closingCut_ = false, started_ = false, closed_ = false;
};

// An open line: u16 count, u16 x0, u16 y0, then count - 1 steps.
class LineReader {
   public:
    explicit LineReader(ByteStream& s);
    bool next(int32_t& x, int32_t& y);
    int count() const { return count_; }

   private:
    ByteStream& s_;
    int count_ = 0, left_ = 0;
    int32_t x_ = 0, y_ = 0;
    bool started_ = false;
};

// A town or airport: u16 x, u16 y, u8 length, name (kept to 20 bytes).
struct Point {
    uint16_t x = 0, y = 0;
    char name[21] = "";
};
bool readPoint(ByteStream& s, Point& out);

}  // namespace basemap
