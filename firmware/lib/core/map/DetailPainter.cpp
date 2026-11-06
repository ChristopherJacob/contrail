#include "DetailPainter.h"

#ifdef CONTRAIL_ESP32
#pragma GCC optimize("O2")  // the coverage loop runs per pixel, the edge toggles per sub-row
#endif

#include <cmath>
#include <cstring>
#include <utility>

#include "gfx/Font.h"
#include "gfx/Raster.h"
#include "gfx/fonts/Fonts.h"

namespace basemap {
using namespace gfx;

namespace {
constexpr int kFrac = 16;
constexpr int64_t kOne = int64_t(1) << kFrac, kHalf = kOne / 2;
constexpr int kTownCap[MapPack::kMaxLevels] = {10, 7, 5};
constexpr int kMediumCapLevel2 = 10;
constexpr int kStrokeMargin = 2;  // the widest line reaches this far past its centre: 1.5 / 2 + 1, rounded up

int64_t fixed(double v) { return int64_t(std::llround(v * double(kOne))); }
float pixels(int64_t v) { return float(v) / float(kOne); }

// Screen rows [yTop, yBottom] and a feature's y range (north up: its ymax is the top of it on screen).
template <typename Tf>
bool crosses(const FeatureEntry& e, const Tf& m, int yTop, int yBottom) {
    return m.y(e.ymin) >= yTop * kOne && m.y(e.ymax) <= yBottom * kOne;
}
}  // namespace

void DetailPainter::begin(const MapView& v, float radiusNm, uint8_t* scratch, DetailMark* marks) {
    v_ = v, sub_ = scratch, marks_ = marks, nMarks_ = 0, failed_ = false, bad_ = 0;
    vr0_ = vc0_ = vrows_ = 0, vcols_ = 1;
    if (!pack_ || !pack_->isOpen() || pack_->levelCount() == 0) {
        failed_ = true;
        return;
    }
    level_ = pack_->levelFor(radiusNm);
    const PackLevel& lv = pack_->level(level_);
    lv.cell(v.lat, v.lon, anchorRow_, anchorCol_);
    const double deg = lv.tileDeg(), k = v.pxPerNm * 60.0, kx = k * v.cosLat;
    const double lat0 = lv.originLatE5 * 1e-5 + anchorRow_ * deg, lon0 = lv.originLonE5 * 1e-5 + anchorCol_ * deg;
    anchor_.sx = fixed(deg / kUnits * kx), anchor_.ox = fixed(v.cx + (lon0 - v.lon) * kx);
    anchor_.sy = fixed(-deg / kUnits * k), anchor_.oy = fixed(v.cy - (lat0 - v.lat) * k);
}

bool DetailPainter::isBad(int row, int col) const {
    const int r = row - vr0_, c = col - vc0_, i = r * vcols_ + c;
    return r >= 0 && r < vrows_ && c >= 0 && c < vcols_ && i < kMaxTiles && (bad_ >> i & 1);
}

// Bits 0..2 airports of rank 0..2, 3..8 towns of rank 0..5. Any other rank is in no pass, so it needs no bit.
uint16_t DetailPainter::rankBit(uint8_t kind, uint8_t rank) {
    if (kind == kAirport && rank <= 2) return uint16_t(1u << rank);
    if (kind == kTown && rank <= 5) return uint16_t(1u << (3 + rank));
    return 0;
}

uint16_t DetailPainter::tileRanks(int row, int col) const {
    const int i = (row - vr0_) * vcols_ + (col - vc0_);
    if (i >= kMaxTiles) return 0xFFFF;  // beyond the mask: not known, so read
    const uint8_t* m = sub_ + kSubBytes + 2 * i;
    return uint16_t(m[0] | m[1] << 8);
}

void DetailPainter::setTileRanks(int i, uint16_t bits) {
    if (i >= kMaxTiles) return;
    uint8_t* m = sub_ + kSubBytes + 2 * i;
    m[0] = uint8_t(bits), m[1] = uint8_t(bits >> 8);
}

void DetailPainter::verifyView() {
    int r1, c1;
    tileRange(vr0_, r1, vc0_, c1, -float(kStrokeMargin), float(v_.h + kStrokeMargin));
    vcols_ = c1 - vc0_ + 1, vrows_ = r1 - vr0_ + 1;
    viewRanks_ = 0;
    for (int r = vr0_; r <= r1; ++r)
        for (int c = vc0_; c <= c1; ++c) {
            const int i = (r - vr0_) * vcols_ + (c - vc0_);
            setTileRanks(i, 0);
            TileInfo t;
            const bool good = pack_->tile(level_, r, c, t, true);
            if (good) {
                uint16_t ranks = 0;
                FeatureEntry e;  // points are last: back from the end until the first thing that is not one
                for (int f = t.featureCount - 1; f >= 0 && pack_->feature(t, f, e) && e.kind >= kTown; --f)
                    ranks |= rankBit(e.kind, e.rank);
                setTileRanks(i, ranks);
                viewRanks_ |= i < kMaxTiles ? ranks : uint16_t(0xFFFF);
            }
            if (!pack_->isOpen()) {
                failed_ = true;
                return;
            }
            if (!good && t.offset && i < kMaxTiles) bad_ |= uint64_t(1) << i;
        }
}

bool DetailPainter::tileFor(int row, int col, TileInfo& t) {
    const bool ok = pack_->tile(level_, row, col, t, false);
    if (!pack_->isOpen()) failed_ = true;
    return ok && !isBad(row, col);
}

DetailPainter::Tf DetailPainter::tf(int row, int col) const {
    Tf m = anchor_;
    m.ox += anchor_.sx * (int64_t(col - anchorCol_) * kUnits);
    m.oy += anchor_.sy * (int64_t(row - anchorRow_) * kUnits);
    return m;
}

void DetailPainter::tileRange(int& r0, int& r1, int& c0, int& c1, float yTop, float yBottom) const {
    const PackLevel& lv = pack_->level(level_);
    const double k = v_.pxPerNm * 60.0, kx = k * v_.cosLat;
    const double latTop = v_.lat + (v_.cy - yTop) / k, latBottom = v_.lat + (v_.cy - yBottom) / k;
    const double lonL = v_.lon - (v_.cx + kStrokeMargin) / kx, lonR = v_.lon + (v_.w - v_.cx + kStrokeMargin) / kx;
    lv.cell(latBottom, lonL, r0, c0);
    lv.cell(latTop, lonR, r1, c1);
    if (r0 < 0) r0 = 0;
    if (c0 < 0) c0 = 0;
    if (r1 > lv.rows - 1) r1 = lv.rows - 1;
    if (c1 > lv.cols - 1) c1 = lv.cols - 1;
}

// ------------------------------------------------------------------ land

// One ring edge into the sub-pixel rows of the half-strip starting at screen row sy0. Sub-row k is centred at
// sy0 + (k + 0.5) / 2: it is toggled where the edge crosses that centre, in the half-open rule (a vertex shared by
// two edges toggles once), at the first sub-pixel whose centre is at or right of the crossing. Toggles left of the
// chart land on bit 0, so what lies off the left still opens and closes the row's parity; toggles past its right
// edge are dropped, since nothing is read after them. Edges run bottom to top whichever way the ring goes, so a tile
// border walked either way gives the same toggles.
void DetailPainter::edge(int64_t x0, int64_t y0, int64_t x1, int64_t y1, int sy0) {
    if (y0 == y1) return;
    if (y0 > y1) std::swap(x0, x1), std::swap(y0, y1);
    // To sub-pixels of this half-strip, 16.16.
    const int64_t ya = (y0 - sy0 * kOne) * 2, yb = (y1 - sy0 * kOne) * 2;
    int64_t k0 = (ya - kHalf + kOne - 1) >> kFrac, k1 = (yb - kHalf + kOne - 1) >> kFrac;
    if (k0 < 0) k0 = 0;
    if (k1 > kSubRows) k1 = kSubRows;
    if (k0 >= k1) return;
    const int64_t xa = x0 * 2, xb = x1 * 2;
    if (xa >= kSubW * kOne && xb >= kSubW * kOne) return;
    // Exact when vertical (a tile border): the sum below then never rounds.
    const int64_t slope = xa == xb ? 0 : (xb - xa) * kOne / (yb - ya);
    int64_t xc = xa + (((k0 * kOne + kHalf - ya) * slope) >> kFrac);
    for (int k = int(k0); k < int(k1); ++k, xc += slope) {
        const int64_t bit = (xc - kHalf + kOne - 1) >> kFrac;
        if (bit >= kSubW) continue;
        const int b = bit < 0 ? 0 : int(bit);
        sub_[k * (kSubW / 8) + (b >> 3)] ^= uint8_t(0x80 >> (b & 7));
    }
}

void DetailPainter::fillLand(Canvas& c, int y0, int rows) {
    std::memset(sub_, 0, kSubBytes);
    int r0, r1, c0, c1;
    tileRange(r0, r1, c0, c1, float(y0 - 1), float(y0 + rows + 1));
    for (int r = r0; r <= r1 && !failed_; ++r)
        for (int col = c0; col <= c1 && !failed_; ++col) {
            TileInfo t;
            const bool have = tileFor(r, col, t);
            const Tf m = tf(r, col);
            if (t.allLand) {  // a whole tile of land, with or without roads on it: its rectangle's two sides
                edge(m.x(0), m.y(0), m.x(0), m.y(kUnits), y0);
                edge(m.x(kUnits), m.y(0), m.x(kUnits), m.y(kUnits), y0);
            }
            if (!have) continue;
            for (int i = 0; i < t.featureCount; ++i) {
                FeatureEntry e;
                if (!pack_->feature(t, i, e) || e.kind != kRing) break;  // rings come first
                if (!crosses(e, m, y0 - 1, y0 + rows + 1)) continue;
                ByteStream s(*pack_->file(), e.begin, e.end);
                StreamRingReader rr(s);
                RingPoint p;
                int64_t px = 0, py = 0;
                bool first = true;
                while (rr.next(p)) {
                    const int64_t qx = m.x(p.x), qy = m.y(p.y);
                    if (!first) edge(px, py, qx, qy, y0);
                    px = qx, py = qy, first = false;
                }
                if (!s.ok()) failed_ = true;  // an open ring would flip the rest of its rows
            }
            if (!pack_->isOpen()) failed_ = true;
        }
    if (failed_) return;
    // Running parity along each sub-row, then 2 x 2 sub-pixels to a coverage of 0..4 per pixel.
    for (int k = 0; k < 2 * rows; ++k) {
        uint8_t* row = sub_ + k * (kSubW / 8);
        uint8_t carry = 0;
        for (int b = 0; b < kSubW / 8; ++b) {
            uint8_t v = row[b];
            v ^= v >> 1, v ^= v >> 2, v ^= v >> 4;  // prefix parity from the most significant bit down
            if (carry) v = uint8_t(~v);
            row[b] = v;
            carry = v & 1;
        }
    }
    const uint16_t landPx = rgb565(t_.mapLand);
    const Rect cl = c.clip();
    auto at = [](const uint8_t* r, int i) { return r[i >> 3] >> (7 - (i & 7)) & 1; };
    for (int y = cl.y; y < cl.bottom(); ++y) {
        const uint8_t* a = sub_ + (2 * (y - y0)) * (kSubW / 8);
        const uint8_t* b = a + kSubW / 8;
        for (int x = cl.x; x < cl.right(); ++x) {
            const int bit = 2 * x;
            const int cov = at(a, bit) + at(a, bit + 1) + at(b, bit) + at(b, bit + 1);
            if (cov == 4) c.at(x, y) = landPx;
            else if (cov) c.at(x, y) = blend565(c.at(x, y), landPx, uint8_t(cov * 64));  // 8, 16 or 24 of 32 levels
        }
    }
}

// ------------------------------------------------------------------ lines

// One segment. Its box against the clip in fixed point first: most segments of a tile miss a half-strip, and aaLine
// would convert them to float and take their square roots first. aaLine then visits only the clip's rows, and on each
// only the columns near the line, however long the segment. Clamped units keep every float well inside an int.
void DetailPainter::stroke(Canvas& c, int64_t x0, int64_t y0, int64_t x1, int64_t y1, float w, Rgb col, uint8_t a) {
    const Rect& cl = c.clip();
    const int64_t pad = kStrokeMargin * kOne;
    if ((x0 < x1 ? x1 : x0) + pad < cl.x * kOne || (x0 < x1 ? x0 : x1) - pad > cl.right() * kOne ||
        (y0 < y1 ? y1 : y0) + pad < cl.y * kOne || (y0 < y1 ? y0 : y1) - pad > cl.bottom() * kOne)
        return;
    aaLine(c, pixels(x0), pixels(y0), pixels(x1), pixels(y1), w, col, a);
}

void DetailPainter::coast(Canvas& c, const FeatureEntry& e, const Tf& m) {
    ByteStream s(*pack_->file(), e.begin, e.end);
    StreamRingReader rr(s);
    RingPoint p;
    int64_t px = 0, py = 0;
    bool first = true;
    while (rr.next(p)) {
        const int64_t qx = m.x(p.x), qy = m.y(p.y);
        if (!first && !p.cut) stroke(c, px, py, qx, qy, 1.0f, t_.mapCoast, 230);
        px = qx, py = qy, first = false;
    }
    if (!s.ok()) failed_ = true;
}

void DetailPainter::lineFeature(Canvas& c, const FeatureEntry& e, const Tf& m, Rgb col, float w) {
    ByteStream s(*pack_->file(), e.begin, e.end);
    LineReader lr(s);
    int32_t x, y;
    int64_t px = 0, py = 0;
    bool first = true;
    while (lr.next(x, y)) {
        const int64_t qx = m.x(x), qy = m.y(y);
        if (!first) stroke(c, px, py, qx, qy, w, col, 255);
        px = qx, py = qy, first = false;
    }
    if (!s.ok()) failed_ = true;
}

void DetailPainter::strokeLines(Canvas& c, int y0, int rows) {
    int r0, r1, c0, c1;
    const int top = y0 - kStrokeMargin, bottom = y0 + rows + kStrokeMargin;
    tileRange(r0, r1, c0, c1, float(top), float(bottom));
    for (int r = r0; r <= r1 && !failed_; ++r)
        for (int col = c0; col <= c1 && !failed_; ++col) {
            TileInfo t;
            if (!tileFor(r, col, t)) continue;
            const Tf m = tf(r, col);
            for (int i = 0; i < t.featureCount && !failed_; ++i) {
                FeatureEntry e;
                if (!pack_->feature(t, i, e) || e.kind >= kTown) break;  // points come last
                if (!crosses(e, m, top, bottom)) continue;
                switch (e.kind) {
                    case kRing: coast(c, e, m); break;
                    case kRiver: lineFeature(c, e, m, t_.mapRiver, 1.2f); break;
                    case kRoad: lineFeature(c, e, m, t_.mapRoadMinor, 1.0f); break;
                    case kMajorRoad: lineFeature(c, e, m, t_.mapRoad, 1.5f); break;
                    case kRunway: lineFeature(c, e, m, t_.mapRunway, 1.5f); break;
                }
            }
            if (!pack_->isOpen()) failed_ = true;
        }
}

void DetailPainter::paintFill(Canvas& c, int y0, int rows) {
    if (rows > kRows) rows = kRows;
    const Rect saved = c.clip();
    c.setClip(saved.intersect({0, y0, 240, rows}));
    c.fill(c.clip(), rgb565(t_.mapWater));
    if (!failed_) fillLand(c, y0, rows);
    c.setClip(saved);
}

void DetailPainter::paint(Canvas& c, int y0, int rows) {
    if (rows > kRows) rows = kRows;
    paintFill(c, y0, rows);
    if (failed_) return;
    const Rect saved = c.clip();
    c.setClip(saved.intersect({0, y0, 240, rows}));
    strokeLines(c, y0, rows);
    c.setClip(saved);
}

// ------------------------------------------------------------------ names

// Right, left, below, above; false when the name fits nowhere clear of what is already placed.
bool DetailPainter::place(DetailMark& k, int w, const Rect* keep, int nKeep) {
    char text[sizeof k.name];
    std::memcpy(text, k.name, sizeof text);
    if (k.kind == kTown && t_.upperHeads)
        for (char* q = text; *q; ++q)
            if (*q >= 'a' && *q <= 'z') *q = char(*q - 32);
    // Boxes as the preview's: the text, a pixel more each way across, as tall as the font; beside the dot and centred
    // on it, or 3 px below or above it.
    const int tw = textWidth(kFontSmall, text), th = kFontSmall.ascent + kFontSmall.descent;
    const int cands[4][2] = {{k.x + 4, k.y - th / 2},
                             {k.x - 4 - tw, k.y - th / 2},
                             {k.x - tw / 2, k.y + 3},
                             {k.x - tw / 2, k.y - 3 - th}};
    for (const auto& cand : cands) {
        const Rect b{cand[0] - 1, cand[1], tw + 2, th};
        if (b.x < 2 || b.right() > w - 2 || b.y < 2 || b.bottom() > v_.h - 2) continue;
        bool clear = true;
        for (int j = 0; j < nKeep && clear; ++j) clear = !b.intersects(keep[j]);
        for (int j = 0; j < nMarks_ && clear; ++j) clear = !b.intersects(marks_[j].box());
        if (!clear) continue;
        k.labelled = true;
        k.lx = int16_t(cand[0]), k.ly = int16_t(cand[1] + kFontSmall.ascent);
        k.bx = int16_t(b.x), k.by = int16_t(b.y), k.bw = int16_t(b.w), k.bh = int16_t(b.h);
        std::memcpy(k.name, text, sizeof text);
        return true;
    }
    return false;
}

// Every point of one kind and rank in the view, tile by tile and in each tile's order (the builder lists towns by
// population), until `cap` of them have been placed.
void DetailPainter::placePoints(uint8_t kind, uint8_t rank, int w, const Rect* keep, int nKeep, int cap) {
    const uint16_t bit = rankBit(kind, rank);
    if (!(viewRanks_ & bit)) return;
    int placed = 0;
    for (int r = vr0_; r < vr0_ + vrows_ && !failed_; ++r)
        for (int col = vc0_; col < vc0_ + vcols_ && !failed_; ++col) {
            if (!(tileRanks(r, col) & bit)) continue;
            TileInfo t;
            if (!tileFor(r, col, t)) continue;
            const Tf m = tf(r, col);
            // Points are last: walk back to the first of them, then on from there.
            int first = t.featureCount;
            FeatureEntry e;
            while (first > 0 && pack_->feature(t, first - 1, e) && e.kind >= kTown) --first;
            for (int i = first; i < t.featureCount; ++i) {
                if (placed >= cap || nMarks_ >= kMaxMarks) return;
                if (!pack_->feature(t, i, e)) break;
                if (e.kind != kind || e.rank != rank) continue;
                ByteStream s(*pack_->file(), e.begin, e.end);
                Point p;
                if (!readPoint(s, p)) {  // perhaps a passing error: a name lost now would be saved with the chart
                    failed_ = true;
                    return;
                }
                const int64_t x = m.x(p.x), y = m.y(p.y);
                if (x < 4 * kOne || x > (w - 4) * kOne || y < 18 * kOne || y > (v_.h - 4) * kOne) continue;
                DetailMark& k = marks_[nMarks_];
                k = DetailMark{};
                k.x = int16_t((x + kHalf) >> kFrac), k.y = int16_t((y + kHalf) >> kFrac);
                k.kind = kind, k.rank = rank;
                std::memcpy(k.name, p.name, sizeof k.name);
                if (place(k, w, keep, nKeep)) ++nMarks_, ++placed;
            }
            if (!pack_->isOpen()) failed_ = true;
        }
}

int DetailPainter::layout(int w, const Rect* keep, int nKeep) {
    nMarks_ = 0;
    if (failed_) return 0;
    verifyView();
    if (failed_) return 0;
    const int townCap = kTownCap[level_];
    // Airports leave room for the towns' share: a crowd of small fields must not take every mark.
    const int airportMarks = kMaxMarks - townCap;
    placePoints(kAirport, 0, w, keep, nKeep, airportMarks);
    placePoints(kAirport, 1, w, keep, nKeep, level_ == 2 ? kMediumCapLevel2 : airportMarks - nMarks_);
    placePoints(kAirport, 2, w, keep, nKeep, airportMarks - nMarks_);
    int towns = 0;
    for (uint8_t rank = 0; rank <= 5 && towns < townCap; ++rank) {
        const int before = nMarks_;
        placePoints(kTown, rank, w, keep, nKeep, townCap - towns);
        towns += nMarks_ - before;
    }
    if (!pack_->isOpen()) failed_ = true;
    return nMarks_;
}

}  // namespace basemap
