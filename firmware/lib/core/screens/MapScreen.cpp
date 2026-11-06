#include "MapScreen.h"

#ifdef CONTRAIL_ESP32
#pragma GCC optimize("O2")  // the chart is a per-pixel loop
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstddef>
#include <cstdlib>
#include <cstring>

#include "assets/Pack.h"  // crc16
#include "data/Airlines.h"
#include "gfx/Font.h"
#include "gfx/Hot.h"
#include "gfx/Math.h"
#include "gfx/Raster.h"
#include "gfx/fonts/Fonts.h"
#include "map/MapData.h"
#include "services/Log.h"
#include "ui/Widgets.h"

namespace scr {
using namespace gfx;

namespace {
// One row of the chart, columns [x0, x1): each pixel's colour from the four mask cells around it.
// Cell i's centre is at screen x = 2i + 1, so pixel x sits between cells (x - 1) >> 1 and the next,
// a quarter of the way along on odd x and three quarters on even x (likewise for rows). Kept out of
// line so it stays in IRAM: inlined into MapBase::draw it would run from flash.
GFX_HOT __attribute__((noinline)) void chartRow(uint16_t* out, const uint8_t* bits, int x0, int x1, int y,
                                                const uint16_t* shade) {
    using basemap::LandMask;
    const int j = (y - 1) >> 1;
    const uint8_t* ra = bits + (j < 0 ? 0 : j) * LandMask::kStride;
    const uint8_t* rb = bits + (j + 1 >= LandMask::kH ? LandMask::kH - 1 : j + 1) * LandMask::kStride;
    const uint16_t* s = shade + ((y & 1) ? 0 : 2);
    auto bit = [](const uint8_t* r, int i) { return r[i >> 3] >> (7 - (i & 7)) & 1; };
    for (int x = x0; x < x1; ++x) {
        const int i = (x - 1) >> 1;
        const int ia = i < 0 ? 0 : i, ib = i + 1 >= LandMask::kW ? LandMask::kW - 1 : i + 1;
        // Far from the coast all four cells agree: whole bytes of both rows are water or land.
        const uint8_t a8 = ra[ia >> 3];
        if ((a8 == 0 || a8 == 0xFF) && rb[ia >> 3] == a8 && (ib >> 3) == (ia >> 3)) {
            // So does every pixel up to the byte's last cell pair (cells 8k + 6 and 8k + 7): x up to
            // 16k + 14. Where the cells are clamped (the chart's edges) only this pixel is known.
            const uint16_t v = s[a8 ? 15 << 2 : 0];
            const int end = i < 0 ? x + 1 : std::max(x + 1, std::min(x1, ((ia >> 3) << 4) + 15));
            for (; x < end; ++x) *out++ = v;
            --x;  // the loop's ++x
            continue;
        }
        const int code = bit(ra, ia) | bit(ra, ib) << 1 | bit(rb, ia) << 2 | bit(rb, ib) << 3;
        *out++ = s[code << 2 | ((x & 1) ? 0 : 1)];
    }
}
}  // namespace

// ------------------------------------------------------------------ MapBase

void MapBase::rebuild() {
    ++version_;
    located_ = scene_->locationSet;
    radius_ = scene_->radiusNm;
    nMarks_ = 0;
    detailed_ = false, ready_ = true, dropped_ = false;
    std::snprintf(header_, sizeof header_, "LIVE MAP · %d NM", int(radius_ + 0.5f));
    if (!located_) return;
    buildShades();
    view_ = basemap::viewFor(scene_->homeLat, scene_->homeLon, radius_);
    // Scale bar: the longest round distance up to half the radius.
    static const int kSteps[] = {1, 2, 5, 10, 20, 50};
    int nm = 1;
    for (int s : kSteps)
        if (s <= radius_ / 2) nm = s;
    scalePx_ = nm * view_.pxPerNm;
    std::snprintf(scale_, sizeof scale_, "%d nm", nm);
    // The header and north arrow have no descenders: their boxes end at the baseline's margin.
    fixed_[0] = ui::textBox(kFontSmall, 12, 20, header_);
    fixed_[1] = ui::textBox(kFontSmall, 228, 20, "N ↑", Align::Right);
    fixed_[0].h -= kFontSmall.descent, fixed_[1].h -= kFontSmall.descent;
    fixed_[2] = ui::textBox(kFontSmall, 12, 249, scale_).unite({12, 250, int(scalePx_) + 2, 6});
    if (pack_ && pack_->isOpen() && pack_->covers(scene_->homeLat, scene_->homeLon, radius_) && startDetail())
        return;
    mask_.build(view_);
    // Airport labels keep clear of home's marker, the chart's own text and the strip below it.
    const Rect keep[] = {fixed_[0], fixed_[1], fixed_[2], homeBox(), {0, kMapH, 240, 320 - kMapH}};
    nMarks_ = basemap::airportsInView(view_, marks_.data(), kMaxMarks);
    basemap::placeLabels(marks_.data(), nMarks_, kFontSmall, 240, keep, int(sizeof keep / sizeof keep[0]));
}

// The chart as a field: the bilinear blend v of the four cells around a pixel, land where v > 1/2.
// Dividing v - 1/2 by the field's gradient gives the pixel's distance from the coast in pixels, so
// land and water meet along a smooth curve (not the mask's 2-px steps), blended over about a pixel,
// with a ~1-px anti-aliased coastline on it. Every pixel sits at one of four spots between cell
// centres, so 16 cell patterns x 4 spots cover the whole chart.
void MapBase::buildShades() {
    for (int code = 0; code < 16; ++code)
        for (int q = 0; q < 4; ++q) {
            const float a = code & 1, b = code >> 1 & 1, c = code >> 2 & 1, d = code >> 3 & 1;
            const float fx = (q & 1) ? 0.75f : 0.25f, fy = (q & 2) ? 0.75f : 0.25f;
            const float v = a * (1 - fx) * (1 - fy) + b * fx * (1 - fy) + c * (1 - fx) * fy + d * fx * fy;
            const float gx = ((b - a) * (1 - fy) + (d - c) * fy) / 2, gy = ((c - a) * (1 - fx) + (d - b) * fx) / 2;
            const float g = std::fmax(std::sqrt(gx * gx + gy * gy), 0.05f);  // per pixel
            const float dist = (v - 0.5f) / g;                                // pixels, land positive
            float t = std::fmin(std::fmax(0.5f + dist, 0.f), 1.f);
            t = t * t * (3 - 2 * t);
            const float coast = std::fmin(std::fmax(1.1f - std::fabs(dist), 0.f), 1.f);
            const uint16_t ground = rgb565(lerp(t_.mapWater, t_.mapLand, t));
            shade_[code << 2 | q] = blend565(ground, t_.mapCoast, uint8_t(coast * 230));
        }
}

// Microseconds of f() not spent waiting for card reads.
template <class F>
uint32_t MapBase::cpuUs(F&& f) {
    const uint32_t t = now(), card = pack_->file()->ioStats().readUs;
    f();
    return now() - t - (pack_->file()->ioStats().readUs - card);
}

// The saved chart's key for the view rebuild() set up (key_ holds its home).
basemap::ChartKey MapBase::chartKey() const {
    basemap::ChartKey k;
    k.latE5 = int32_t(std::lround(key_.lat * 1e5)), k.lonE5 = int32_t(std::lround(key_.lon * 1e5));
    k.radiusNm10 = uint16_t(std::lround(radius_ * 10));
    // Here and not in the constructor: useTheme() changes the colours under the same MapBase.
    k.theme = assets::crc16(reinterpret_cast<const uint8_t*>(&t_.mapWater),
                            offsetof(Theme, mapCard) - offsetof(Theme, mapWater));
    k.theme = assets::crc16(reinterpret_cast<const uint8_t*>(&t_.mapRoad), 4 * sizeof(Rgb), k.theme);
    k.theme = assets::crc16(reinterpret_cast<const uint8_t*>(&t_.home), sizeof(Rgb), k.theme);  // home's marker
    k.theme = uint16_t(k.theme ^ (t_.upperHeads ? 0x5A5A : 0));
    k.level = uint8_t(pack_->levelFor(radius_));
    k.packId = pack_->packId();
    k.format = uint16_t(basemap::kChartFormat * 256 + sizeof(basemap::DetailMark));  // a drawing or mark-layout change
    return k;
}

// The saved chart's marks, or the start of a job that draws them. No tile is read for a chart already saved.
bool MapBase::startDetail() {
    static_assert(sizeof(Rgb) == 3, "the theme key hashes runs of colours as bytes");
    static_assert(4 + sizeof(basemap::DetailMark) * basemap::DetailPainter::kMaxMarks <=
                      basemap::ChartCache::kExtraBytes,
                  "the saved marks must fit beside the chart");
    const basemap::ChartKey k = chartKey();
    basemap::ChartCache cache(*pack_->file(), pack_->cacheOffset(), pack_->cacheSize());
    // A failed read is a card error; a mark count that makes no sense only means the chart is drawn again.
    uint32_t n = 0;
    bool ok = true;
    if (cache.matches(k) && (ok = cache.readExtra(0, &n, 4)) && n <= uint32_t(basemap::DetailPainter::kMaxMarks)) {
        if ((ok = cache.readExtra(4, dmarks_.data(), n * sizeof(basemap::DetailMark)))) {
            for (uint32_t i = 0; i < n; ++i)  // the marks area has no CRC: every name ends inside its array
                dmarks_[int(i)].name[sizeof dmarks_[0].name - 1] = 0;
            nMarks_ = int(n), detailed_ = true, ready_ = true;
            svc::logf("[map] saved chart (level %d, %d names): no rebuild", int(k.level), nMarks_);
            return true;
        }
    } else if (ok) {
        job_ = JobStats{};
        job_.startUs = now(), job_.io = pack_->file()->ioStats();
        painter_.begin(view_, radius_, sub_, dmarks_.data());
        const Rect keep[] = {fixed_[0], fixed_[1], fixed_[2], homeBox()};
        job_.layoutUs = cpuUs([&] { nMarks_ = painter_.layout(240, keep, int(sizeof keep / sizeof keep[0])); });
        if (!painter_.failed() && cache.begin(k)) {
            detailed_ = true, ready_ = false, jobRow_ = 0, jobAsk_ = 1;
            return true;
        }
    }
    pack_->readFailed();
    svc::logf("[map] card error: the built-in map until restart");
    nMarks_ = 0;
    return false;
}

// The rest of this frame must not touch the built-in map's mask or marks: what is in the arena is the detailed map's.
void MapBase::dropDetail() {
    pack_->readFailed();
    svc::logf("[map] card error: the built-in map until restart");
    detailed_ = false, ready_ = true, dropped_ = true, full_ = true;
    nMarks_ = 0;
}

void MapBase::update(float dt, DamageList& damage) {
    if (!mask_.bits()) return;  // not entered yet
    marks_.check();
    dmarks_.check();
    if (scene_ && (full_ || dirty_ || settle_ > 0)) {
        ViewKey k;
        k.lat = scene_->homeLat, k.lon = scene_->homeLon, k.radius = scene_->radiusNm, k.located = scene_->locationSet;
        const bool onlyRadius =
            k.lat == key_.lat && k.lon == key_.lon && k.located == key_.located && k.radius != key_.radius;
        if (full_ || (!(k == key_) && !(detailed_ && onlyRadius))) {
            settle_ = 0, key_ = k, rebuild(), full_ = true;
        } else if (k == key_) {
            settle_ = 0;
        } else if (settle_ <= 0 || k.radius != pendingRadius_) {
            pendingRadius_ = k.radius, settle_ = kRadiusSettleS;  // being adjusted: the old chart stays up meanwhile
        } else if ((settle_ -= dt) <= 0) {
            settle_ = 0, key_ = k, rebuild(), full_ = true;
        }
    }
    dirty_ = false;
    if (detailed_ && !ready_) {
        // The job, a few bands a frame: painted until the frame has spent kJobFrameUs, so a full-damage frame (the
        // first, a fade) never takes it all at once. Its damage is as many bands as the last frame painted, one more
        // when that frame had time to spare.
        // The first frame damages everything, so the old chart and its planes go at once: from then on the rows
        // below jobRow_ show water, whichever frame draws them.
        ++job_.frames, frameUs_ = now();
        jobBands_ = 0;
        if (jobRow_ < kMapH) {
            // Whole bands, the bottom one's padding rows too.
            const int bands = clock_ ? std::max(1, int(jobAsk_)) : kJobBands;
            const int rows = std::min(bands * basemap::ChartCache::kTile,
                                      basemap::ChartCache::kTilesY * basemap::ChartCache::kTile - jobRow_);
            damage.add(full_ ? Rect{0, 0, 240, 320} : Rect{0, jobRow_, 240, rows});
            full_ = false;
            return;
        }
        basemap::ChartCache cache(*pack_->file(), pack_->cacheOffset(), pack_->cacheSize());
        const uint32_t n = uint32_t(nMarks_);
        if (!cache.writeExtra(0, &n, 4) || !cache.writeExtra(4, dmarks_.data(), n * sizeof(basemap::DetailMark)) ||
            !cache.finish(chartKey()))
            return dropDetail();
        ready_ = true, ++version_, full_ = true;
        const hal::IoStats io = pack_->file()->ioStats();
        svc::logf("[map] chart drawn in %u ms (level %d): layout %u ms, paint %u ms, card read %u ms / %u blocks, "
                  "card write %u ms / %u blocks, %u frames",
                  unsigned((now() - job_.startUs) / 1000), painter_.level(),
                  unsigned(job_.layoutUs / 1000), unsigned(job_.paintUs / 1000),
                  unsigned((io.readUs - job_.io.readUs) / 1000), unsigned(io.blocksRead - job_.io.blocksRead),
                  unsigned((io.writeUs - job_.io.writeUs) / 1000), unsigned(io.blocksWritten - job_.io.blocksWritten),
                  unsigned(job_.frames));
    }
    if (full_) damage.add({0, 0, 240, 320});
    full_ = false;
}

void MapBase::draw(Canvas& c) {
    marks_.check();
    dmarks_.check();
    const Rect cl = c.clip();
    Rect below = Rect{0, kMapH, 240, 320 - kMapH}.intersect(cl);  // behind the strip
    if (!below.empty()) c.fill(below, rgb565(t_.mapWater));
    const Rect chart = Rect{0, 0, 240, kMapH}.intersect(cl);
    if (chart.empty()) return;
    if (!located_) {
        c.fill(chart, rgb565(t_.mapWater));
        drawText(c, *t_.fontTitle, 120, 128, t_.upperHeads ? "SET LOCATION" : "Set location", t_.text, Align::Center);
        drawText(c, kFontBody, 120, 150, "Hold the screen at power-on", t_.textDim, Align::Center);
        return;
    }
    if (detailed_) return detailDraw(c, chart);
    if (dropped_) return c.fill(chart, rgb565(t_.mapWater));
    for (int y = chart.y; y < chart.bottom(); ++y)
        chartRow(&c.at(chart.x, y), mask_.bits(), chart.x, chart.right(), y, shade_);
    overlay(c, chart);
}

// The renderer's canvases sit on a fixed grid of 16-row bands and hold the damaged rows and columns of one band, as the
// saved chart's tiles do. The job's damage covers whole bands across the full width, so it gets canvases of 16 rows
// contiguous in memory (rows 270 and 271 included at the bottom), each one multi-block write. The job's rows advance a
// band at a time, so a band is either saved or not.
void MapBase::detailDraw(Canvas& c, const Rect& chart) {
    using basemap::ChartCache;
    constexpr int kBand = ChartCache::kTile;
    ChartCache cache(*pack_->file(), pack_->cacheOffset(), pack_->cacheSize());
    const uint16_t water = rgb565(t_.mapWater);
    const Rect b = c.band();
    for (int y = chart.y; y < chart.bottom();) {
        const int y1 = std::min(chart.bottom(), (y / kBand + 1) * kBand);
        const Rect part{chart.x, y, chart.w, y1 - y};
        bool ok = true;
        if (ready_ || y1 <= jobRow_) {
            // Saved: read it back, in one read when the canvas is whole tiles (its rows below the chart are padding:
            // water again after).
            if (b.y == y && b.h == kBand && b.x % kBand == 0 && b.w % kBand == 0 && part.x == b.x && part.w == b.w) {
                ok = cache.readRect(b.x, b.y, b.w, kBand, &c.at(b.x, b.y), b.w);
                if (ok && b.bottom() > kMapH) c.fill({b.x, kMapH, b.w, b.bottom() - kMapH}, water);
            } else {
                ok = cache.readRect(part.x, part.y, part.w, part.h, &c.at(part.x, part.y), c.width());
            }
        } else if (y == jobRow_ && b.y == y && b.h == kBand && b.x == 0 && b.w == 240 && part.w == 240 &&
                   (jobBands_ == 0 || (clock_ ? now() - frameUs_ < kJobFrameUs : jobBands_ < kJobBands))) {
            // The job's next band (the strip renderer's strips are kBand rows, so the job's damage gives this).
            job_.paintUs += cpuUs([&] {
                for (int r = y; r < y1; r += basemap::DetailPainter::kRows)
                    painter_.paint(c, r, std::min(basemap::DetailPainter::kRows, y1 - r));
                if (!painter_.failed()) overlay(c, part);
            });
            ok = !painter_.failed() && cache.writeBand(y / kBand, &c.at(0, y));
            jobRow_ = y1, ++jobBands_;
            jobAsk_ = uint8_t(clock_ && now() - frameUs_ < kJobFrameUs ? jobBands_ + 1 : jobBands_);
        } else {
            c.fill(part, water);  // not drawn yet: the job repaints it when it gets there
        }
        if (!ok) {
            dropDetail();  // and the next frame redraws it all
            return c.fill(chart, water);
        }
        y = y1;
    }
}

// All of it inside `chart`: what is saved in the detailed chart, drawn the same way each time.
void MapBase::overlay(Canvas& c, const Rect& chart) {
    const Rect cl = c.clip();
    // Nothing on the chart (runways, airports near the bottom edge, labels) reaches the strip.
    c.setClip(chart);
    const float cx = view_.cx, cy = view_.cy;
    aaRing(c, cx, cy, 120, 1, t_.mapRing, 120);
    aaRing(c, cx, cy, 60, 1, t_.mapRing, 90);
    aaRing(c, cx, cy, 7, 1, t_.home, 110);  // home's halo, under any airport label there
    // Airports: runways for large ones when the radius is 50 nm or less, then dots and labels.
    // (The detailed chart's runways are part of the painted chart.)
    const bool runways = radius_ <= 50;
    // Runway ends lie within ~2 nm of an airport's reference point: 3 nm (and 40 px) is safe.
    const int reach = std::max(40, int(3 * view_.pxPerNm));
    for (int i = 0; detailed_ && i < nMarks_; ++i) {
        const basemap::DetailMark& m = dmarks_[i];
        if (!chart.intersects({m.x - 3, m.y - 3, 7, 7}) && !chart.intersects(m.box())) continue;
        const bool town = m.kind == basemap::kTown;
        if (town) c.fill({m.x - 1, m.y - 1, 3, 3}, rgb565(t_.mapTown));
        else aaCircle(c, m.x + 0.5f, m.y + 0.5f, m.rank == 0 ? 2.6f : m.rank == 1 ? 1.8f : 1.4f, t_.mapAirport);
        if (m.labelled) drawText(c, kFontSmall, m.lx, m.ly, m.name, town ? t_.mapTown : t_.mapAirport, Align::Left, 190);
    }
    for (int i = 0; !detailed_ && i < nMarks_; ++i) {
        const basemap::AirportMark& m = marks_[i];
        if (!chart.intersects({m.x - reach, m.y - reach, 2 * reach, 2 * reach}) && !chart.intersects(m.box())) continue;
        const basemap::gen::Airport& a = basemap::gen::kAirports[m.index];
        if (runways && a.large()) {
            const int32_t lat = a.latE5(), lon = a.lonE5(), first = a.firstRunway();
            for (int k = 0; k < a.runways(); ++k) {
                const basemap::gen::Runway& r = basemap::gen::kRunways[first + k];
                Vec2 p0 = view_.project((lat + r.dLat1) * 1e-5, (lon + r.dLon1) * 1e-5);
                Vec2 p1 = view_.project((lat + r.dLat2) * 1e-5, (lon + r.dLon2) * 1e-5);
                aaLine(c, p0.x, p0.y, p1.x, p1.y, 1.6f, t_.mapRunway, 220);
            }
        }
        aaCircle(c, m.x, m.y, m.large ? 2.6f : 1.8f, t_.mapAirport);
        if (m.labelled)
            drawText(c, kFontSmall, m.lx, m.ly, m.ident, t_.mapText, m.alignRight ? Align::Right : Align::Left, 190);
    }
    aaCircle(c, cx, cy, 2.5f, t_.home);
    const float sx = 12, sy = 254;
    aaLine(c, sx, sy, sx + scalePx_, sy, 1.4f, t_.mapText, 220);
    aaLine(c, sx, sy - 3, sx, sy + 0.5f, 1, t_.mapText, 220);
    aaLine(c, sx + scalePx_, sy - 3, sx + scalePx_, sy + 0.5f, 1, t_.mapText, 220);
    drawText(c, kFontSmall, int(sx), int(sy) - 5, scale_, t_.mapText, Align::Left, 220);
    drawText(c, kFontSmall, 12, 20, header_, t_.mapText, Align::Left, 230);
    drawText(c, kFontSmall, 228, 20, "N ↑", t_.mapText, Align::Right, 230);
    c.setClip(cl);
}

// ------------------------------------------------------------------ MapStrip

const SceneFlight* MapStrip::pick() const {
    if (!scene_ || !scene_->count || !scene_->locationSet) return nullptr;
    if (const SceneFlight* f = selected_ ? scene_->find(selected_) : nullptr) return f;
    return &scene_->flights[0];
}

void MapStrip::compose(const SceneFlight& f, Text& t) const {
    std::snprintf(t.callsign, sizeof t.callsign, "%s", f.callsign);
    const data::Airline* al = data::airlineForCallsign(f.callsign);
    char sub[64];
    int n = 0;
    if (f.hasRoute) n += std::snprintf(sub + n, sizeof sub - n, "%s → %s · ", f.orig, f.dest);
    if (al) n += std::snprintf(sub + n, sizeof sub - n, "%s · ", al->name);
    std::snprintf(sub + n, sizeof sub - n, "%s", f.type);
    ui::ellipsize(t.sub, sizeof t.sub, kFontSmall, sub, 128);
    char a[16], d[12];
    ui::formatAltitude(a, sizeof a, f.altFt);
    const char* arrow = ui::trendArrow(f.vrateFpm);
    std::snprintf(t.alt, sizeof t.alt, "%s%s%s", a, arrow[0] ? " " : "", arrow);
    ui::formatDistance(d, sizeof d, f.distNm);
    std::snprintf(t.dist, sizeof t.dist, "%s %s", d, ui::compassPoint(f.bearingDeg));
    t.trend = f.vrateFpm >= 300 ? 1 : f.vrateFpm <= -300 ? 2 : 0;
}

void MapStrip::update(float, DamageList& damage) {
    const bool drawing = !base_.ready();
    if (!dirty_ && drawing == drawing_) return;
    dirty_ = false, drawing_ = drawing;
    const SceneFlight* f = pick();
    located_ = !scene_ || scene_->locationSet;
    shows_ = f != nullptr;
    uint32_t sig = located_ ? 1 : 2;
    if (f) {
        compose(*f, text_);
        const Text& t = text_;
        sig = ui::hashStr(t.dist, ui::hashStr(t.alt, ui::hashStr(t.sub, ui::hashStr(t.callsign, t.trend + 2u))));
    }
    if (drawing) sig = 3, f = nullptr;  // the note, not the plane (which is not on the chart yet)
    if (sig != sig_) damage.add({kBox.x - 2, kBox.y - 2, kBox.w + 4, kBox.h + 4});
    sig_ = sig;
    shown_ = f ? f->hex : 0;
}

void MapStrip::draw(Canvas& c) {
    if (!c.clip().intersects(kBox) || !located_) return;  // "Set location" says it all
    ui::panel(c, kBox, 10, t_.mapCard, t_.mapCardEdge, t_);
    if (drawing_) {
        drawText(c, kFontBody, 120, kBox.y + 24, t_.upperHeads ? "DRAWING MAP…" : "Drawing map…", t_.mapText,
                 Align::Center);
        return;
    }
    if (!shows_) {
        drawText(c, kFontBody, 120, kBox.y + 24, "No aircraft in range", t_.mapText, Align::Center);
        return;
    }
    const Text& t = text_;
    const Rgb trend[3] = {t_.level, t_.climb, t_.descend};
    drawText(c, *t_.fontLabel, kBox.x + 10, kBox.y + 17, t.callsign, t_.text);
    drawText(c, kFontSmall, kBox.x + 10, kBox.y + 31, t.sub, t_.mapText);
    drawText(c, kFontSmall, kBox.right() - 10, kBox.y + 16, t.alt, trend[t.trend], Align::Right);
    drawText(c, kFontSmall, kBox.right() - 10, kBox.y + 30, t.dist, t_.textDim, Align::Right);
}

// ------------------------------------------------------------------ MapScreen

void MapScreen::setSelection(uint32_t hex) {
    Screen::setSelection(hex);
    planes_.setSelection(hex), strip_.setSelection(hex);
}

void MapScreen::enter() {
    Scratch* s = arena_.claim<Scratch>(this);
    base_.attach(s->builtin.mask, s->builtin.marks, s->detail.sub, s->detail.marks, arena_, this);
    planes_.attach(s->planes, arena_, this);
    base_.reset(), planes_.reset(), strip_.reset();
}

NavRequest MapScreen::onTap(int x, int y) {
    NavRequest r;
    if (y >= MapBase::kMapH) {  // only the strip itself opens its plane
        const Rect& b = MapStrip::kBox;
        const bool in = x >= b.x && x < b.right() && y >= b.y && y < b.bottom();
        if (uint32_t hex = in ? strip_.shownHex() : 0) r.open = true, r.to = ScreenId::Closest, r.selectHex = hex;
        return r;
    }
    r.selectHex = planes_.hitTest(x, y, 20);
    return r;
}

}  // namespace scr
