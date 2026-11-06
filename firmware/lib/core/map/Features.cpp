#include "Features.h"

#ifdef CONTRAIL_ESP32
#pragma GCC optimize("O2")  // every vertex of a chart comes through here
#endif

namespace basemap {

bool ByteStream::fill() {
    if (!ok_ || pos_ >= end_) return ok_ = false;
    const uint32_t n = end_ - pos_ < sizeof buf_ ? end_ - pos_ : uint32_t(sizeof buf_);
    if (!f_.readAt(pos_, buf_, n)) return ok_ = false;
    pos_ += n, at_ = 0, got_ = uint8_t(n);
    return true;
}

void readStep(ByteStream& s, uint8_t lead, int32_t& dx, int32_t& dy) {
    if (lead == 0x80) {
        dx = s.i16(), dy = s.i16();
    } else if (lead == 0x82) {
        const uint32_t b0 = s.u8(), b1 = s.u8(), b2 = s.u8();
        const uint32_t v = b0 | b1 << 8 | b2 << 16;
        dx = int32_t((v & 0xFFF) ^ 0x800) - 0x800, dy = int32_t((v >> 12) ^ 0x800) - 0x800;
    } else {
        dx = int8_t(lead), dy = int8_t(s.u8());
    }
}

StreamRingReader::StreamRingReader(ByteStream& s) : s_(s) {
    const uint16_t head = s.u16();
    count_ = left_ = head & 0x7FFF;
    closingCut_ = head >> 15;
    x_ = x0_ = s.u16(), y_ = y0_ = s.u16();
}

bool StreamRingReader::next(RingPoint& out) {
    if (!s_.ok() || count_ == 0) return false;
    if (!started_) {
        started_ = true, --left_;
        out = {x0_, y0_, false};
        return true;
    }
    if (left_ > 0) {
        uint8_t lead = s_.u8();
        const bool cut = lead == 0x81;
        if (cut) lead = s_.u8();
        int32_t dx, dy;
        readStep(s_, lead, dx, dy);
        if (!s_.ok()) return false;
        x_ += dx, y_ += dy, --left_;
        out = {x_, y_, cut};
        return true;
    }
    if (closed_) return false;
    closed_ = true;
    out = {x0_, y0_, closingCut_};
    return true;
}

LineReader::LineReader(ByteStream& s) : s_(s) {
    count_ = left_ = s.u16();
    x_ = s.u16(), y_ = s.u16();
}

bool LineReader::next(int32_t& x, int32_t& y) {
    if (left_ <= 0 || !s_.ok()) return false;
    if (started_) {
        int32_t dx, dy;
        readStep(s_, s_.u8(), dx, dy);
        if (!s_.ok()) return false;
        x_ += dx, y_ += dy;
    }
    started_ = true, --left_;
    x = x_, y = y_;
    return true;
}

bool readPoint(ByteStream& s, Point& out) {
    out.x = s.u16(), out.y = s.u16();
    const int n = s.u8();
    int k = 0;
    for (int i = 0; i < n; ++i) {
        const char c = char(s.u8());
        if (k < int(sizeof out.name) - 1) out.name[k++] = c;
    }
    out.name[k] = 0;
    return s.ok();
}

}  // namespace basemap
