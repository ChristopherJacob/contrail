// DetailPainter: land and water checked pixel by pixel against the rings themselves, over the fixture pack and over
// small packs built here (a lake with an island, rings across the chart's edges, tile seams); lines drawn; labels
// placed in priority order without overlaps; nothing outside the half-strip touched.
#include <unity.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "Fixture.h"
#include "TestStorage.h"
#include "assets/Pack.h"
#include "gfx/Canvas.h"
#include "map/DetailPainter.h"
#include "map/Features.h"
#include "map/LandMask.h"  // viewFor
#include "screens/Theme.h"

void setUp() {}
void tearDown() {}

using namespace basemap;

// ------------------------------------------------------------------ a map pack written here

namespace {

struct Vtx {
    int x, y;
    bool cut;  // the edge from the previous point (for point 0: the closing edge) is a tile cut
};

struct Feat {
    uint8_t kind, rank;
    std::vector<uint8_t> data;
    int16_t ymin, ymax;
};

void put16(std::vector<uint8_t>& o, int v) { o.push_back(uint8_t(v)), o.push_back(uint8_t(v >> 8)); }
void put32(std::vector<uint8_t>& o, uint32_t v) { put16(o, int(v & 0xFFFF)), put16(o, int(v >> 16)); }

// Steps in the long form (0x80, two int16): plain to write, and able to go far off the tile.
void putLongStep(std::vector<uint8_t>& o, int dx, int dy) { o.push_back(0x80), put16(o, dx), put16(o, dy); }

Feat ring(const std::vector<Vtx>& pts) {
    Feat f{kRing, 0, {}, 32767, -32768};
    put16(f.data, int(pts.size()) | (pts[0].cut ? 0x8000 : 0));
    put16(f.data, pts[0].x), put16(f.data, pts[0].y);
    for (size_t i = 1; i < pts.size(); ++i) {
        if (pts[i].cut) f.data.push_back(0x81);
        putLongStep(f.data, pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
    }
    for (const Vtx& p : pts) f.ymin = int16_t(std::min<int>(f.ymin, p.y)), f.ymax = int16_t(std::max<int>(f.ymax, p.y));
    return f;
}

// The whole tile as a ring of cut edges, the way the builder writes land with a lake in it.
Feat fullRing() { return ring({{0, 0, true}, {2048, 0, true}, {2048, 2048, true}, {0, 2048, true}}); }

// A regular polygon, none of its edges a cut.
Feat blob(int cx, int cy, int r) {
    std::vector<Vtx> pts;
    for (int i = 0; i < 12; ++i)
        pts.push_back({cx + int(r * std::cos(i * M_PI / 6)), cy + int(r * std::sin(i * M_PI / 6)), false});
    return ring(pts);
}

Feat line(uint8_t kind, const std::vector<std::pair<int, int>>& pts) {
    Feat f{kind, 0, {}, 32767, -32768};
    put16(f.data, int(pts.size()));
    put16(f.data, pts[0].first), put16(f.data, pts[0].second);
    for (size_t i = 1; i < pts.size(); ++i)
        putLongStep(f.data, pts[i].first - pts[i - 1].first, pts[i].second - pts[i - 1].second);
    for (const auto& p : pts)
        f.ymin = int16_t(std::min(int(f.ymin), p.second)), f.ymax = int16_t(std::max(int(f.ymax), p.second));
    return f;
}

Feat point(uint8_t kind, uint8_t rank, int x, int y, const char* name) {
    Feat f{kind, rank, {}, int16_t(y), int16_t(y)};
    put16(f.data, x), put16(f.data, y);
    f.data.push_back(uint8_t(std::strlen(name)));
    f.data.insert(f.data.end(), name, name + std::strlen(name));
    return f;
}

struct TileSpec {
    bool allLand = false;
    std::vector<Feat> feats;
};

struct LevelSpec {
    int maxRadius, milli, latE5, lonE5, rows, cols;
    std::map<std::pair<int, int>, TileSpec> tiles;
};

// A town or airport put where it should appear on the screen of view v, in whichever tile holds that spot.
void addPoint(LevelSpec& l, const MapView& v, uint8_t kind, uint8_t rank, double sx, double sy, const char* name) {
    const double k = 60.0 * v.pxPerNm, kx = k * v.cosLat, deg = l.milli * 1e-3;
    const double r = (v.lat + (v.cy - sy) / k - l.latE5 * 1e-5) / deg, c = (v.lon + (sx - v.cx) / kx - l.lonE5 * 1e-5) / deg;
    const int row = int(std::floor(r)), col = int(std::floor(c));
    TEST_ASSERT_TRUE(row >= 0 && row < l.rows && col >= 0 && col < l.cols);
    l.tiles[{row, col}].feats.push_back(point(kind, rank, int(std::lround((c - col) * 2048)), int(std::lround((r - row) * 2048)), name));
}

// The pack layout of tools/build_map.py, CRCs included, with the features in the order given.
std::vector<uint8_t> buildPack(int latE5, int lonE5, const std::vector<LevelSpec>& levels) {
    uint32_t at = 64 + 24 * uint32_t(levels.size());
    std::vector<uint32_t> indexAt;
    for (const LevelSpec& l : levels) indexAt.push_back(at), at += uint32_t(l.rows * l.cols) * 8;
    std::vector<uint8_t> body, table;
    std::vector<std::vector<uint8_t>> indexes;
    for (size_t i = 0; i < levels.size(); ++i) {
        const LevelSpec& l = levels[i];
        std::vector<uint8_t> index(size_t(l.rows * l.cols) * 8, 0);
        for (const auto& kv : l.tiles) {
            const size_t e = size_t(kv.first.first * l.cols + kv.first.second) * 8;
            index[e + 6] = kv.second.allLand ? 1 : 0;
            if (kv.second.feats.empty()) continue;
            std::vector<uint8_t> tile;
            put16(tile, int(kv.second.feats.size()));
            uint32_t data = 4 + 8 * uint32_t(kv.second.feats.size());
            for (const Feat& f : kv.second.feats) {
                tile.push_back(f.kind), tile.push_back(f.rank);
                put16(tile, int(data)), put16(tile, f.ymin), put16(tile, f.ymax);
                data += uint32_t(f.data.size());
            }
            for (const Feat& f : kv.second.feats) tile.insert(tile.end(), f.data.begin(), f.data.end());
            const uint16_t crc = assets::crc16(tile.data(), uint32_t(tile.size()));
            const uint32_t off = at + uint32_t(body.size());
            for (int b = 0; b < 4; ++b) index[e + size_t(b)] = uint8_t(off >> (8 * b));
            const uint32_t size = uint32_t(tile.size()) + 2;
            index[e + 4] = uint8_t(size), index[e + 5] = uint8_t(size >> 8);
            put16(body, crc);
            body.insert(body.end(), tile.begin(), tile.end());
        }
        put16(table, l.maxRadius), put16(table, l.milli);
        put32(table, uint32_t(l.latE5)), put32(table, uint32_t(l.lonE5));
        put16(table, l.rows), put16(table, l.cols);
        put32(table, indexAt[i]);
        put16(table, assets::crc16(index.data(), uint32_t(index.size()))), put16(table, 0);
        indexes.push_back(std::move(index));
    }
    const uint32_t cacheAt = (at + uint32_t(body.size()) + 4095) / 4096 * 4096;
    std::vector<uint8_t> o;
    o.insert(o.end(), {'C', 'M', 'A', 'P'});
    put16(o, 1), put16(o, 64);
    put32(o, uint32_t(latE5)), put32(o, uint32_t(lonE5));
    put16(o, 200), o.push_back(uint8_t(levels.size())), o.push_back(0);
    put32(o, 0x12345678), put32(o, cacheAt), put32(o, 135168);
    put16(o, assets::crc16(table.data(), uint32_t(table.size())));
    o.resize(62, 0);
    put16(o, assets::crc16(o.data(), 62));
    o.insert(o.end(), table.begin(), table.end());
    for (const auto& ix : indexes) o.insert(o.end(), ix.begin(), ix.end());
    o.insert(o.end(), body.begin(), body.end());
    o.resize(cacheAt + 135168, 0);
    return o;
}

// ------------------------------------------------------------------ the rings as ground truth

// Land or water at a point of the screen, worked out in double precision straight from the rings: which tile holds
// the point, then even-odd along a ray over that tile's edges. It shares nothing with the painter but the decoders.
class Truth {
   public:
    Truth(MapPack& pack, const MapView& v, int level) {
        const PackLevel& lv = pack.level(level);
        const double k = 60.0 * v.pxPerNm, kx = k * v.cosLat, deg = lv.tileDeg();
        tileW_ = deg * kx, tileH_ = deg * k;
        x0_ = v.cx + (lv.originLonE5 * 1e-5 - v.lon) * kx;
        y0_ = v.cy - (lv.originLatE5 * 1e-5 - v.lat) * k;
        rows_ = lv.rows, cols_ = lv.cols;
        tiles_.resize(size_t(rows_ * cols_));
        for (int r = 0; r < rows_; ++r)
            for (int c = 0; c < cols_; ++c) {
                TileInfo t;
                const bool have = pack.tile(level, r, c, t, true);
                Tile& out = tiles_[size_t(r * cols_ + c)];
                out.allLand = t.allLand;
                if (!have) continue;
                const double ox = x0_ + c * tileW_, oy = y0_ - r * tileH_;
                for (int i = 0; i < t.featureCount; ++i) {
                    FeatureEntry e;
                    TEST_ASSERT_TRUE(pack.feature(t, i, e));
                    if (e.kind != kRing) break;
                    ByteStream s(*pack.file(), e.begin, e.end);
                    StreamRingReader rr(s);
                    RingPoint p;
                    double px = 0, py = 0;
                    bool first = true;
                    while (rr.next(p)) {
                        const double qx = ox + p.x / 2048.0 * tileW_, qy = oy - p.y / 2048.0 * tileH_;
                        if (!first) out.edges.push_back({px, py, qx, qy});
                        px = qx, py = qy, first = false;
                    }
                    TEST_ASSERT_TRUE(s.ok());
                }
            }
    }
    bool land(double x, double y) const {
        const int c = int(std::floor((x - x0_) / tileW_)), r = int(std::floor((y0_ - y) / tileH_));
        if (r < 0 || c < 0 || r >= rows_ || c >= cols_) return false;
        const Tile& t = tiles_[size_t(r * cols_ + c)];
        bool in = t.allLand;
        for (const Edge& e : t.edges)
            if ((e.ay <= y) != (e.by <= y) && e.ax + (y - e.ay) / (e.by - e.ay) * (e.bx - e.ax) > x) in = !in;
        return in;
    }

   private:
    struct Edge {
        double ax, ay, bx, by;
    };
    struct Tile {
        bool allLand = false;
        std::vector<Edge> edges;
    };
    std::vector<Tile> tiles_;
    double x0_, y0_, tileW_, tileH_;
    int rows_, cols_;
};

struct Chart {
    std::vector<uint16_t> px = std::vector<uint16_t>(240 * 270);
    uint8_t scratch[DetailPainter::kScratchBytes];
    DetailMark marks[DetailPainter::kMaxMarks];
};

struct FillStats {
    int land = 0, water = 0, edge = 0, unsure = 0;  // pixels; unsure: too near a boundary to judge, not compared
};

// Paints the land fill of the chart at (lat, lon) and compares every pixel with what the four sample points of the
// rings say. A sample within 0.15 px of a boundary could fall either way by rounding, so that pixel is not judged.
FillStats checkFill(MapPack& pack, double lat, double lon, float radius, const scr::Theme& theme = scr::cinematicSky()) {
    Chart ch;
    DetailPainter p(theme);
    p.setPack(&pack);
    const MapView v = viewFor(lat, lon, radius);
    p.begin(v, radius, ch.scratch, ch.marks);
    p.layout(240, nullptr, 0);
    for (int y = 0; y < 270; y += DetailPainter::kRows) {
        const int rows = std::min(DetailPainter::kRows, 270 - y);
        gfx::Canvas c(ch.px.data() + y * 240, 240, y, rows);
        p.paintFill(c, y, rows);
    }
    TEST_ASSERT_FALSE(p.failed());
    const Truth truth(pack, v, p.level());
    const uint16_t water = gfx::rgb565(theme.mapWater), land = gfx::rgb565(theme.mapLand);
    FillStats st;
    constexpr double kNear = 0.15;
    for (int y = 0; y < 270; ++y)
        for (int x = 0; x < 240; ++x) {
            int cov = 0;
            bool sure = true;
            for (int sy = 0; sy < 2 && sure; ++sy)
                for (int sx = 0; sx < 2 && sure; ++sx) {
                    const double px = x + 0.25 + sx * 0.5, py = y + 0.25 + sy * 0.5;
                    const bool in = truth.land(px, py);
                    sure = truth.land(px - kNear, py) == in && truth.land(px + kNear, py) == in &&
                           truth.land(px, py - kNear) == in && truth.land(px, py + kNear) == in;
                    cov += in;
                }
            if (!sure) {
                ++st.unsure;
                continue;
            }
            const uint16_t want = cov == 4 ? land : cov == 0 ? water : gfx::blend565(water, land, uint8_t(cov * 64));
            if (ch.px[size_t(y * 240 + x)] != want) {
                char msg[96];
                std::snprintf(msg, sizeof msg, "pixel %d,%d: %d of 4 samples land, radius %.1f", x, y, cov, double(radius));
                TEST_ASSERT_EQUAL_HEX16_MESSAGE(want, ch.px[size_t(y * 240 + x)], msg);
            }
            st.land += cov == 4, st.water += cov == 0, st.edge += cov > 0 && cov < 4;
        }
    return st;
}

// The chart in half-strips, as MapBase does, starting from row `start` (and the rows above it first).
void paintAll(DetailPainter& p, Chart& ch, int start = 0) {
    if (start % DetailPainter::kRows) {
        gfx::Canvas c(ch.px.data(), 240, 0, start % DetailPainter::kRows);
        p.paint(c, 0, start % DetailPainter::kRows);
    }
    for (int y = start % DetailPainter::kRows; y < 270; y += DetailPainter::kRows) {
        const int rows = std::min(DetailPainter::kRows, 270 - y);
        gfx::Canvas c(ch.px.data() + y * 240, 240, y, rows);
        p.paint(c, y, rows);
    }
}

MemStorage sd;
MapPack pack;

void openFixture() {
    sd.add(MapPack::kPath, loadFixture("map_small.pak"));
    TEST_ASSERT_TRUE(pack.begin(&sd));
}

void openBuilt(const std::vector<uint8_t>& bytes) {
    sd.addBytes(MapPack::kPath, bytes);
    TEST_ASSERT_TRUE(pack.begin(&sd));
}

constexpr double kFixLat = 39.4176, kFixLon = -77.3743;

// Home for the built packs: tile borders at about x = 30, 86, 142, 198, 254 and y = 69, 141, 213 at radius 25, so
// the columns at cols 1 and 5 straddle the chart's left and right edges and rows 2 and 5 its bottom and top.
constexpr double kLakeLat = 39.02, kLakeLon = -77.1;

// Half a blob against the tile's east (or west) border, its straight side a cut edge along that border.
Feat halfLake(bool east, int y, int r) {
    std::vector<Vtx> pts;
    const int x = east ? 2048 : 0;
    pts.push_back({x, y - r, true});  // the closing edge, from (x, y + r) back here, runs along the border
    for (int i = 1; i < 6; ++i) {
        const double a = i * M_PI / 6;
        pts.push_back({x + int((east ? -1 : 1) * r * std::sin(a)), y - int(r * std::cos(a)), false});
    }
    pts.push_back({x, y + r, false});
    return ring(pts);
}

std::vector<uint8_t> lakePack() {
    LevelSpec l{25, 250, 3800000, -7800000, 8, 8, {}};
    auto full = [&](int r, int c) { l.tiles[{r, c}].allLand = true; };
    auto feats = [&](int r, int c, std::vector<Feat> f) { l.tiles[{r, c}].feats = std::move(f); };
    // The rest of the grid is all land or plain water, off screen or nearly so.
    full(6, 1), full(6, 2), full(6, 3), full(6, 4), full(6, 5);
    // Top row of the chart: land off the left edge, a water tile, land, a lake with an island, land off the right.
    full(5, 1), full(5, 3), full(5, 5);
    feats(5, 4, {fullRing(), blob(1024, 1024, 700), blob(1024, 1024, 250)});
    // A coast off the left edge; a lake and island; land; land; a lake across the right edge (x = 240 is unit 1530).
    feats(4, 1, {ring({{0, 0, true}, {2048, 0, true}, {2048, 2048, true}, {0, 2048, true}, {700, 1100, false}})});
    feats(4, 2, {fullRing(), blob(900, 1100, 600), blob(1000, 1000, 200)});
    full(4, 3), full(4, 4);
    feats(4, 5, {fullRing(), blob(1530, 1024, 300)});
    // A lake across the left edge (x = 0 is unit 934); land; a lake cut in two by a tile border; a coast off the right.
    feats(3, 1, {fullRing(), blob(934, 1024, 320)});
    full(3, 2);
    feats(3, 3, {fullRing(), halfLake(true, 1000, 500)});
    feats(3, 4, {fullRing(), halfLake(false, 1000, 500)});
    feats(3, 5, {ring({{0, 0, true}, {1800, 0, false}, {1200, 2048, true}, {0, 2048, true}})});
    // Bottom row: water, land, water, an island in open water, water.
    full(2, 2);
    feats(2, 4, {blob(1024, 1024, 800)});
    return buildPack(3900000, -7700000, {l});
}

}  // namespace

// ------------------------------------------------------------------ land and water against the rings

static void test_fill_matches_the_rings_around_a_lake_and_island() {
    openBuilt(lakePack());
    const FillStats st = checkFill(pack, kLakeLat, kLakeLon, 25);
    TEST_ASSERT_TRUE(st.land > 20000);
    TEST_ASSERT_TRUE(st.water > 8000);
    TEST_ASSERT_TRUE(st.edge > 100);
    TEST_ASSERT_TRUE(st.unsure < 240 * 270 / 15);  // nearly every pixel was judged
}

// The chart moved a few pixels at a time: rings and rectangles cross its left and right edges, in many positions.
static void test_fill_matches_the_rings_across_the_chart_edges() {
    openBuilt(lakePack());
    for (double dlon : {-0.11, -0.07, -0.03, 0.02, 0.05, 0.09, 0.13})
        for (float radius : {25.f, 19.f, 13.5f}) {
            const FillStats st = checkFill(pack, kLakeLat, kLakeLon + dlon, radius);
            TEST_ASSERT_TRUE(st.land > 1000);
            TEST_ASSERT_TRUE(st.unsure < 240 * 270 / 10);
        }
}

// Rings of the fixture itself: real data with the coast in it (the Potomac and the Bay at 35 nm).
static void test_fill_matches_the_rings_on_the_fixture() {
    openFixture();
    FillStats st = checkFill(pack, kFixLat, kFixLon, 25);
    TEST_ASSERT_TRUE(st.land > 40000);
    st = checkFill(pack, kFixLat, kFixLon, 35);
    TEST_ASSERT_TRUE(st.land > 40000);
    TEST_ASSERT_TRUE(st.water >= 40);  // a sliver at the chart's right edge: why the built packs above exist
    TEST_ASSERT_EQUAL(0, pack.damagedTiles());
}

// Land from wall to wall over tiles of both kinds, at radii and places that put tile borders at awkward fractions: not
// one pixel of the fill may be anything but land, whatever the rounding at the seams.
static void test_no_seams_between_land_tiles() {
    LevelSpec l{25, 250, 3700000, -7900000, 16, 16, {}};
    for (int r = 0; r < 16; ++r)
        for (int c = 0; c < 16; ++c) {
            if ((r + c) % 3 == 0) l.tiles[{r, c}].feats = {fullRing()};
            else l.tiles[{r, c}].allLand = true;
        }
    openBuilt(buildPack(3900000, -7700000, {l}));
    const uint16_t land = gfx::rgb565(scr::cinematicSky().mapLand);
    for (float radius : {25.f, 24.7f, 17.3f, 11.f, 7.77f, 3.1f, 1.f})
        for (double d : {0.0, 0.0123, 0.111, 0.2031, -0.0777}) {
            Chart ch;
            DetailPainter p(scr::cinematicSky());
            p.setPack(&pack);
            p.begin(viewFor(39.0 + d, -77.0 - d, radius), radius, ch.scratch, ch.marks);
            p.layout(240, nullptr, 0);
            for (int y = 0; y < 270; y += DetailPainter::kRows) {
                const int rows = std::min(DetailPainter::kRows, 270 - y);
                gfx::Canvas c(ch.px.data() + y * 240, 240, y, rows);
                p.paintFill(c, y, rows);
            }
            for (int i = 0; i < 240 * 270; ++i)
                if (ch.px[size_t(i)] != land) {
                    char msg[80];
                    std::snprintf(msg, sizeof msg, "pixel %d,%d at radius %.2f, offset %.4f", i % 240, i / 240,
                                  double(radius), d);
                    TEST_ASSERT_EQUAL_HEX16_MESSAGE(land, ch.px[size_t(i)], msg);
                }
        }
}

// The same, with a tile border walked across the centre of a sub-pixel or sub-row in steps of a quarter of the fixed
// point's unit, exactly where the painter's arithmetic puts it: if two tiles' borders differ by the least rounding, or
// a row counts for both tiles, some step here falls into it.
static void test_no_seams_with_borders_on_sub_pixel_centres() {
    LevelSpec l{25, 250, 3700000, -7900000, 16, 16, {}};
    for (int r = 0; r < 16; ++r)
        for (int c = 0; c < 16; ++c) {
            if ((r + c) % 3 == 0) l.tiles[{r, c}].feats = {fullRing()};
            else l.tiles[{r, c}].allLand = true;
        }
    openBuilt(buildPack(3900000, -7700000, {l}));
    const uint16_t land = gfx::rgb565(scr::cinematicSky().mapLand);
    auto check = [&](double lat, double lon, float radius, int y0) {
        std::vector<uint16_t> px(240 * DetailPainter::kRows);
        uint8_t scratch[DetailPainter::kScratchBytes];
        DetailMark marks[DetailPainter::kMaxMarks];
        DetailPainter p(scr::cinematicSky());
        p.setPack(&pack);
        p.begin(viewFor(lat, lon, radius), radius, scratch, marks);
        gfx::Canvas c(px.data(), 240, y0, DetailPainter::kRows);
        p.paintFill(c, y0, DetailPainter::kRows);
        for (uint16_t v : px) TEST_ASSERT_EQUAL_HEX16(land, v);
    };
    constexpr double kOne = 65536, deg = 0.25;
    for (float radius : {24.7f, 17.3f, 11.f, 7.77f})
        for (int n = -16; n <= 16; ++n) {
            const MapView v = viewFor(39.1, -76.9, radius);
            const double k = 60.0 * v.pxPerNm, kx = k * v.cosLat;
            // The border at longitude -76.75 at x = 150.25, the centre of sub-pixel 300; home's tile is just west.
            const double sx = std::llround(deg / 2048 * kx * kOne);
            const double ox = (150.25 * kOne + n / 4.0 - sx * 2048) / kOne;
            check(39.1, -77.0 - (ox - 120) / kx, radius, 64);
            // The border at latitude 39.25 at y = 110.25, the centre of a sub-row; home's tile is just south.
            const double sy = std::llround(-deg / 2048 * k * kOne);
            const double oy = (110.25 * kOne + n / 4.0 - sy * 2048) / kOne;
            check(39.0 - (135 - oy) / k, -76.9, radius, 104);
        }
}

// A home exactly on a tile corner, so borders run through x = 120 and y = 135.
static void test_fill_with_home_on_a_tile_corner() {
    openBuilt(lakePack());
    const FillStats st = checkFill(pack, 39.0, -77.0, 25);
    TEST_ASSERT_TRUE(st.land > 10000);
}

static void test_coverage_blends_are_exact_quarters() {
    const uint16_t w = gfx::rgb565(scr::cinematicSky().mapWater), l = gfx::rgb565(scr::cinematicSky().mapLand);
    TEST_ASSERT_EQUAL_HEX16(w, gfx::blend565(w, l, uint8_t(0)));
    // Alphas 64, 128 and 192 are 8, 16 and 24 of blend565's 32 levels: not rounded to anything else.
    for (int cov = 1; cov < 4; ++cov) {
        const uint16_t b = gfx::blend565(w, l, uint8_t(cov * 64));
        TEST_ASSERT_NOT_EQUAL(l, b);
        TEST_ASSERT_NOT_EQUAL(w, b);
    }
}

// ------------------------------------------------------------------ out-of-range data

// A CRC-good tile whose points run far off it: nothing overflows (an UBSan run keeps this honest), nothing is left
// open, and rows beyond what the clamp lets the ring reach are untouched.
static void test_points_far_off_the_tile_are_harmless() {
    LevelSpec l{25, 250, 3800000, -7800000, 8, 8, {}};
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 8; ++c) l.tiles[{r, c}].allLand = true;
    TileSpec far;
    far.allLand = true;
    // Steps reach ±32767 at a time, so the far corners take a few of them.
    far.feats.push_back(ring({{0, 0, false},
                              {-30000, -30000, false},
                              {0, -30000, false},
                              {30000, -30000, false},
                              {30000, 0, false},
                              {30000, 30000, false},
                              {0, 30000, false},
                              {-30000, 30000, false},
                              {-30000, 0, false}}));
    far.feats.push_back(line(kMajorRoad, {{1229, 0}, {1229, -30000}, {1229, 0}, {1229, 30000}}));  // through home
    far.feats.push_back(line(kRoad, {{0, 30000}, {-30000, 30000}, {0, 30000}, {30000, 30000}}));
    far.feats.push_back(line(kRiver, {{32000, 32000}, {32000, 32000}}));
    far.feats.push_back(point(kTown, 0, 65535, 65535, "Nowhere"));
    far.feats.push_back(point(kAirport, 0, 65535, 0, "KFAR"));
    l.tiles[{4, 3}] = far;
    openBuilt(buildPack(3900000, -7700000, {l}));
    Chart ch;
    DetailPainter p(scr::cinematicSky());
    p.setPack(&pack);
    p.begin(viewFor(kLakeLat, kLakeLon, 3), 3, ch.scratch, ch.marks);
    TEST_ASSERT_EQUAL(0, p.layout(240, nullptr, 0));
    const int reads = static_cast<MemFile*>(pack.file())->reads;
    paintAll(p, ch);
    TEST_ASSERT_FALSE(p.failed());
    TEST_ASSERT_TRUE(static_cast<MemFile*>(pack.file())->reads - reads < 4000);
    int painted = 0;
    const uint16_t water = gfx::rgb565(scr::cinematicSky().mapWater), land = gfx::rgb565(scr::cinematicSky().mapLand);
    for (uint16_t c : ch.px) painted += c != water && c != land;
    TEST_ASSERT_TRUE(painted > 0);  // the road's clamped path crosses the chart

    // The same chart from the far side of the tile: rows wholly outside the clamp stay land.
    Chart ch2;
    DetailPainter q(scr::cinematicSky());
    q.setPack(&pack);
    q.begin(viewFor(39.875, kLakeLon, 3), 3, ch2.scratch, ch2.marks);  // the top row, beyond the clamp
    q.layout(240, nullptr, 0);
    paintAll(q, ch2);
    TEST_ASSERT_FALSE(q.failed());
    for (uint16_t c : ch2.px) TEST_ASSERT_EQUAL_HEX16(land, c);
}

// ------------------------------------------------------------------ painting

static void test_paints_land_roads_and_water() {
    openFixture();
    Chart ch;
    DetailPainter p(scr::cinematicSky());
    p.setPack(&pack);
    const MapView v = viewFor(kFixLat, kFixLon, 25);
    p.begin(v, 25, ch.scratch, ch.marks);
    int marks = p.layout(240, nullptr, 0);
    TEST_ASSERT_TRUE(marks > 3);
    paintAll(p, ch);
    const uint16_t land = gfx::rgb565(scr::cinematicSky().mapLand);
    int landPx = 0, other = 0;
    for (uint16_t c : ch.px) landPx += c == land, other += c != land;
    TEST_ASSERT_TRUE(landPx > 240 * 270 / 2);  // inland Maryland: mostly land
    TEST_ASSERT_TRUE(other > 2000);            // roads and rivers
    TEST_ASSERT_EQUAL(0, pack.damagedTiles());
    TEST_ASSERT_FALSE(p.failed());
}

// The renderer's strips start wherever damage starts: the same rows must come out the same from any half-strip.
static void test_rows_are_the_same_from_any_half_strip() {
    openFixture();
    Chart a, b;
    DetailPainter p(scr::cinematicSky());
    p.setPack(&pack);
    p.begin(viewFor(kFixLat, kFixLon, 25), 25, a.scratch, a.marks);
    p.layout(240, nullptr, 0);
    paintAll(p, a);
    paintAll(p, b, 3);
    TEST_ASSERT_TRUE(a.px == b.px);
}

static void test_half_strip_paints_only_its_rows() {
    openFixture();
    Chart ch;
    const uint16_t sentinel = 0xF81F;
    for (auto& c : ch.px) c = sentinel;
    DetailPainter p(scr::cinematicSky());
    p.setPack(&pack);
    p.begin(viewFor(kFixLat, kFixLon, 25), 25, ch.scratch, ch.marks);
    p.layout(240, nullptr, 0);
    gfx::Canvas c(ch.px.data() + 64 * 240, 240, 64, 8);
    p.paint(c, 64, 8);
    for (int y = 0; y < 270; ++y)
        for (int x = 0; x < 240; ++x) {
            const bool inside = y >= 64 && y < 72;
            if (inside) TEST_ASSERT_NOT_EQUAL(sentinel, ch.px[size_t(y * 240 + x)]);
            else TEST_ASSERT_EQUAL(sentinel, ch.px[size_t(y * 240 + x)]);
        }
}

static void test_card_pulled_mid_paint_stops_cleanly() {
    openFixture();
    Chart ch;
    DetailPainter p(scr::cinematicSky());
    p.setPack(&pack);
    p.begin(viewFor(kFixLat, kFixLon, 25), 25, ch.scratch, ch.marks);
    p.layout(240, nullptr, 0);
    static_cast<MemFile*>(pack.file())->fail = true;
    gfx::Canvas c(ch.px.data(), 240, 0, 8);
    p.paint(c, 0, 8);
    TEST_ASSERT_TRUE(p.failed());
    TEST_ASSERT_FALSE(pack.isOpen());
}

// A damaged tile is skipped and counted once. A chart still comes out; an all-land tile stays land without its roads.
static void test_damaged_tile_is_skipped() {
    openFixture();
    MemFile* f = static_cast<MemFile*>(pack.file());
    const int level = pack.levelFor(25);
    int row = 0, col = 0;
    pack.level(level).cell(kFixLat, kFixLon, row, col);
    TileInfo t;
    TEST_ASSERT_TRUE(pack.tile(level, row, col, t, true));
    TEST_ASSERT_TRUE(t.allLand);
    // Home's tile on the screen, a few pixels inside its borders (neighbours' lines reach that far).
    const MapView v = viewFor(kFixLat, kFixLon, 25);
    const PackLevel& lv = pack.level(level);
    const double k = 60.0 * v.pxPerNm, kx = k * v.cosLat, deg = lv.tileDeg();
    const int x0 = int(v.cx + (lv.originLonE5 * 1e-5 + col * deg - v.lon) * kx) + 4;
    const int x1 = int(v.cx + (lv.originLonE5 * 1e-5 + (col + 1) * deg - v.lon) * kx) - 4;
    const int y0 = int(v.cy - (lv.originLatE5 * 1e-5 + (row + 1) * deg - v.lat) * k) + 4;
    const int y1 = int(v.cy - (lv.originLatE5 * 1e-5 + row * deg - v.lat) * k) - 4;
    const uint16_t land = gfx::rgb565(scr::cinematicSky().mapLand);
    auto notLand = [&](const Chart& ch) {
        int n = 0;
        for (int y = std::max(0, y0); y < std::min(270, y1); ++y)
            for (int x = std::max(0, x0); x < std::min(240, x1); ++x) n += ch.px[size_t(y * 240 + x)] != land;
        return n;
    };
    Chart before;
    DetailPainter p0(scr::cinematicSky());
    p0.setPack(&pack);
    p0.begin(v, 25, before.scratch, before.marks);
    p0.layout(240, nullptr, 0);
    paintAll(p0, before);
    TEST_ASSERT_TRUE(notLand(before) > 50);  // roads and rivers in it

    f->bytes[t.offset + t.size - 1] ^= 0xFF;
    Chart ch;
    DetailPainter p(scr::cinematicSky());
    p.setPack(&pack);
    p.begin(v, 25, ch.scratch, ch.marks);
    p.layout(240, nullptr, 0);
    TEST_ASSERT_EQUAL(1, pack.damagedTiles());
    paintAll(p, ch);
    TEST_ASSERT_FALSE(p.failed());
    TEST_ASSERT_TRUE(pack.isOpen());
    TEST_ASSERT_EQUAL(0, notLand(ch));  // its land is there, its roads are gone
}

// paint() and paintFill() take at most a half-strip, whatever the caller asks: the scratch holds no more.
static void test_more_rows_than_a_half_strip_stay_in_the_scratch() {
    openFixture();
    constexpr uint8_t kGuard = 0xA5;
    struct {
        uint8_t scratch[DetailPainter::kScratchBytes];
        uint8_t guard[2048];
    } mem;
    std::memset(mem.guard, kGuard, sizeof mem.guard);
    DetailMark marks[DetailPainter::kMaxMarks];
    std::vector<uint16_t> px(240 * 16, 0xF81F);
    DetailPainter p(scr::cinematicSky());
    p.setPack(&pack);
    p.begin(viewFor(kFixLat, kFixLon, 25), 25, mem.scratch, marks);
    gfx::Canvas c(px.data(), 240, 64, 16);
    p.paint(c, 64, 16);
    p.paintFill(c, 64, 16);
    for (uint8_t b : mem.guard) TEST_ASSERT_EQUAL_HEX8(kGuard, b);
    for (int i = 0; i < 240 * 16; ++i) {  // the first 8 rows painted, the rest not touched
        if (i < 240 * 8) TEST_ASSERT_NOT_EQUAL(0xF81F, px[size_t(i)]);
        else TEST_ASSERT_EQUAL(0xF81F, px[size_t(i)]);
    }
}

// One read failing, any one, then the card working again: every one of them must end in failed(). A name lost or a
// ring cut short by a transient error would otherwise be saved with the chart.
static void test_any_one_failed_read_is_reported() {
    LevelSpec l{25, 250, 3800000, -7800000, 8, 8, {}};
    for (int r = 3; r < 6; ++r)
        for (int c = 1; c < 6; ++c) l.tiles[{r, c}].allLand = true;
    std::vector<Vtx> big;
    for (int i = 0; i < 40; ++i)
        big.push_back({1229 + int(500 * std::cos(i * M_PI / 20)), 164 + int(500 * std::sin(i * M_PI / 20)), false});
    std::vector<std::pair<int, int>> road;
    for (int i = 0; i < 30; ++i) road.push_back({400 + 40 * i, 100 + (i % 2) * 40});
    TileSpec home;
    home.allLand = false;
    home.feats = {fullRing(), ring(big), line(kRiver, road), line(kRoad, road), point(kAirport, 2, 1400, 300, "KABC"),
                  point(kTown, 3, 1000, 400, "A town with a long name")};
    l.tiles[{4, 3}] = home;
    openBuilt(buildPack(3900000, -7700000, {l}));
    MemFile* f = static_cast<MemFile*>(pack.file());
    int cases = 0;
    for (int n = 1;; ++n) {
        f->failOnRead = 0;
        TEST_ASSERT_TRUE(pack.begin(&sd));
        const int start = f->reads;
        f->failOnRead = start + n;
        Chart ch;
        DetailPainter p(scr::cinematicSky());
        p.setPack(&pack);
        p.begin(viewFor(kLakeLat, kLakeLon, 25), 25, ch.scratch, ch.marks);
        p.layout(240, nullptr, 0);
        for (int y = 120; y < 152 && !p.failed(); y += DetailPainter::kRows) {
            gfx::Canvas c(ch.px.data() + y * 240, 240, y, DetailPainter::kRows);
            p.paint(c, y, DetailPainter::kRows);
        }
        if (f->reads < start + n) break;  // the whole job took fewer reads: every read has had its turn
        ++cases;
        char msg[40];
        std::snprintf(msg, sizeof msg, "read %d of the job", n);
        TEST_ASSERT_TRUE_MESSAGE(p.failed(), msg);
    }
    f->failOnRead = 0;
    TEST_ASSERT_TRUE(cases > 100);
}

// A chart with one colour per kind of line, on black land: each line where it should be, in its colour and its width.
static void test_lines_have_their_colours_and_widths() {
    scr::Theme th = scr::cinematicSky();
    th.mapLand = {0, 0, 0}, th.mapWater = {255, 255, 255};
    th.mapRiver = {255, 0, 0}, th.mapRoadMinor = {0, 255, 0}, th.mapRoad = {0, 0, 255}, th.mapRunway = {255, 255, 0};
    LevelSpec l{25, 250, 3800000, -7800000, 8, 8, {}};
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 8; ++c) l.tiles[{r, c}].allLand = true;
    const int ys[4] = {400, 700, 1000, 1300};
    l.tiles[{4, 3}].feats = {line(kRiver, {{0, ys[0]}, {2048, ys[0]}}), line(kRoad, {{0, ys[1]}, {2048, ys[1]}}),
                             line(kMajorRoad, {{0, ys[2]}, {2048, ys[2]}}), line(kRunway, {{0, ys[3]}, {2048, ys[3]}})};
    l.tiles[{4, 3}].allLand = true;
    openBuilt(buildPack(3900000, -7700000, {l}));
    Chart ch;
    DetailPainter p(th);
    p.setPack(&pack);
    const MapView v = viewFor(kLakeLat, kLakeLon, 10);
    p.begin(v, 10, ch.scratch, ch.marks);
    p.layout(240, nullptr, 0);
    paintAll(p, ch);
    TEST_ASSERT_FALSE(p.failed());
    const double k = 60.0 * v.pxPerNm, deg = 0.25;
    const float widths[4] = {1.2f, 1.0f, 1.5f, 1.5f};
    const uint16_t pure[4] = {0xF800, 0x07E0, 0x001F, 0xFFE0};
    for (int i = 0; i < 4; ++i) {
        const int y = int(v.cy - (39.0 + ys[i] / 2048.0 * deg - v.lat) * k);
        float cover = 0;
        for (int row = y - 4; row <= y + 4; ++row) {
            const uint16_t c = ch.px[size_t(row * 240 + 100)];
            TEST_ASSERT_EQUAL_HEX16(0, c & ~pure[i]);  // nothing but this line's colour on black
            // The share of its colour, by whichever channel it has.
            cover += pure[i] & 0xF800 ? float(c >> 11) / 31 : pure[i] & 0x07E0 ? float((c >> 5) & 63) / 63 : float(c & 31) / 31;
        }
        TEST_ASSERT_FLOAT_WITHIN(0.2f, widths[i], cover);
    }
}

// The coastline is the rings' edges that are not tile cuts: a lake's shore shows; the border between two land tiles, a
// run of cut edges on the tile with the lake, does not.
static void test_coast_on_shores_and_not_on_tile_borders() {
    scr::Theme th = scr::cinematicSky();
    th.mapLand = {0, 255, 0}, th.mapWater = {0, 0, 255}, th.mapCoast = {255, 0, 0};
    LevelSpec l{25, 250, 3800000, -7800000, 8, 8, {}};
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 8; ++c) l.tiles[{r, c}].allLand = true;
    TileSpec lake;
    const int cx = 1229, cy = 600, rad = 300;
    lake.feats = {fullRing(), blob(cx, cy, rad)};
    l.tiles[{4, 3}] = lake;
    openBuilt(buildPack(3900000, -7700000, {l}));
    Chart ch;
    DetailPainter p(th);
    p.setPack(&pack);
    const MapView v = viewFor(kLakeLat, kLakeLon, 10);
    p.begin(v, 10, ch.scratch, ch.marks);
    p.layout(240, nullptr, 0);
    paintAll(p, ch);
    const double k = 60.0 * v.pxPerNm, kx = k * v.cosLat, deg = 0.25;
    std::vector<std::pair<double, double>> shore;  // the blob's points on the screen, as blob() rounds them
    for (int i = 0; i < 12; ++i) {
        const int ux = cx + int(rad * std::cos(i * M_PI / 6)), uy = cy + int(rad * std::sin(i * M_PI / 6));
        shore.push_back({v.cx + (-77.25 + ux / 2048.0 * deg - v.lon) * kx, v.cy - (39.0 + uy / 2048.0 * deg - v.lat) * k});
    }
    auto toShore = [&](double x, double y) {
        double best = 1e9;
        for (size_t i = 0; i < shore.size(); ++i) {
            const auto& a = shore[i];
            const auto& b = shore[(i + 1) % shore.size()];
            const double dx = b.first - a.first, dy = b.second - a.second;
            double t = ((x - a.first) * dx + (y - a.second) * dy) / (dx * dx + dy * dy);
            t = t < 0 ? 0 : t > 1 ? 1 : t;
            best = std::min(best, std::hypot(x - a.first - t * dx, y - a.second - t * dy));
        }
        return best;
    };
    for (int y = 0; y < 270; ++y)
        for (int x = 0; x < 240; ++x)
            if (ch.px[size_t(y * 240 + x)] >> 11) TEST_ASSERT_TRUE(toShore(x + 0.5, y + 0.5) < 2);  // red only on the shore
    for (size_t i = 0; i < shore.size(); ++i) {  // and on all of it: the middle of each edge is at least half red
        const auto& a = shore[i];
        const auto& b = shore[(i + 1) % shore.size()];
        const int x = int((a.first + b.first) / 2), y = int((a.second + b.second) / 2);
        TEST_ASSERT_TRUE((ch.px[size_t(y * 240 + x)] >> 11) >= 12);
    }
}

// ------------------------------------------------------------------ names

static void test_labels_do_not_overlap_or_leave_the_chart() {
    openFixture();
    Chart ch;
    DetailPainter p(scr::cinematicSky());
    p.setPack(&pack);
    p.begin(viewFor(kFixLat, kFixLon, 25), 25, ch.scratch, ch.marks);
    const gfx::Rect keep[] = {{110, 126, 20, 20}};
    const int n = p.layout(240, keep, 1);
    TEST_ASSERT_TRUE(n > 3);
    for (int i = 0; i < n; ++i) {
        const DetailMark& a = ch.marks[i];
        TEST_ASSERT_TRUE(a.labelled);
        const gfx::Rect ra = a.box();
        TEST_ASSERT_TRUE(ra.x >= 0 && ra.right() <= 240 && ra.y >= 0 && ra.bottom() <= 270);
        TEST_ASSERT_FALSE(ra.intersects(keep[0]));
        for (int j = 0; j < i; ++j) TEST_ASSERT_FALSE(ra.intersects(ch.marks[j].box()));
    }
}

static void test_town_caps_by_level() {
    openFixture();
    for (float r : {10.f, 25.f, 30.f}) {
        Chart ch;
        DetailPainter p(scr::cinematicSky());
        p.setPack(&pack);
        p.begin(viewFor(kFixLat, kFixLon, r), r, ch.scratch, ch.marks);
        const int n = p.layout(240, nullptr, 0);
        int towns = 0;
        for (int i = 0; i < n; ++i) towns += ch.marks[i].kind == kTown;
        TEST_ASSERT_TRUE(towns <= (r <= 25 ? 10 : 7));
        TEST_ASSERT_TRUE(towns > 0);
    }
}

// Airports rank 0, 1, 2, then towns rank 0..5, whichever tile they are in: the marks array holds them in that order.
static void test_places_airports_then_towns_by_rank() {
    openFixture();
    for (float r : {10.f, 25.f, 35.f}) {
        Chart ch;
        DetailPainter p(scr::cinematicSky());
        p.setPack(&pack);
        p.begin(viewFor(kFixLat, kFixLon, r), r, ch.scratch, ch.marks);
        const int n = p.layout(240, nullptr, 0);
        int last = -1;
        for (int i = 0; i < n; ++i) {
            const int key = (ch.marks[i].kind == kTown ? 10 : 0) + ch.marks[i].rank;
            TEST_ASSERT_TRUE(key >= last);
            last = key;
        }
    }
}

// Two points on the same spot, in a tile that lists the town first in each pair: the small field takes the first
// position, the big town the next. Priority, not the tile's order, decides. Within a rank the tile's order decides.
static void test_small_field_is_placed_before_a_big_town() {
    LevelSpec l{25, 250, 3800000, -7800000, 8, 8, {}};
    int x = 1024, y = 1024;
    l.tiles[{4, 3}].feats = {point(kTown, 0, x, y, "Bigtown"), point(kAirport, 2, x, y, "KSML"),
                             point(kTown, 5, 1500, 400, "Tiny"), point(kTown, 5, 1500, 1500, "Tinier")};
    openBuilt(buildPack(3900000, -7700000, {l}));
    Chart ch;
    DetailPainter p(scr::cinematicSky());
    p.setPack(&pack);
    p.begin(viewFor(kLakeLat, kLakeLon, 25), 25, ch.scratch, ch.marks);
    const int n = p.layout(240, nullptr, 0);
    TEST_ASSERT_EQUAL(4, n);
    TEST_ASSERT_EQUAL_STRING("KSML", ch.marks[0].name);
    TEST_ASSERT_EQUAL_STRING("Bigtown", ch.marks[1].name);
    TEST_ASSERT_EQUAL_STRING("Tiny", ch.marks[2].name);  // rank 5 is reached, in the tile's order
    TEST_ASSERT_EQUAL_STRING("Tinier", ch.marks[3].name);
    TEST_ASSERT_TRUE(ch.marks[0].lx > ch.marks[0].x);  // right of the dot, the first choice
    TEST_ASSERT_TRUE(ch.marks[1].lx < ch.marks[1].x);  // left: right was taken
}

// Editorial writes town names in capitals; airport idents as they are.
static void test_editorial_town_names_are_capitals() {
    LevelSpec l{25, 250, 3800000, -7800000, 8, 8, {}};
    l.tiles[{4, 3}].feats = {point(kAirport, 2, 500, 500, "Kabc"), point(kTown, 3, 1500, 1500, "Frederick")};
    openBuilt(buildPack(3900000, -7700000, {l}));
    Chart ch;
    DetailPainter p(scr::editorial());
    p.setPack(&pack);
    p.begin(viewFor(kLakeLat, kLakeLon, 25), 25, ch.scratch, ch.marks);
    TEST_ASSERT_EQUAL(2, p.layout(240, nullptr, 0));
    TEST_ASSERT_EQUAL_STRING("Kabc", ch.marks[0].name);
    TEST_ASSERT_EQUAL_STRING("FREDERICK", ch.marks[1].name);
}

// Level 2: with room for them all, exactly 10 medium airports and 5 towns are named, and large airports whatever
// their number; and more points than marks never crowd out the towns.
static void test_caps_at_level_2_and_a_crowd_of_airports() {
    LevelSpec lv0{25, 250, 3800000, -7800000, 2, 2, {}}, lv1{60, 500, 3800000, -7800000, 2, 2, {}};
    LevelSpec lv2{100, 1000, 3600000, -8100000, 8, 8, {}};
    const MapView v2 = viewFor(39.5, -77.5, 100);
    addPoint(lv2, v2, kAirport, 0, 20, 160, "KLRG");
    for (int i = 0; i < 16; ++i)
        addPoint(lv2, v2, kAirport, 1, 20 + 55 * (i % 4), 40 + 30 * (i / 4), "KMED");
    static const char* const kNames[8] = {"Aa", "Bb", "Cc", "Dd", "Ee", "Ff", "Gg", "Hh"};
    for (int i = 0; i < 8; ++i) addPoint(lv2, v2, kTown, 0, 20 + 70 * (i % 3), 190 + 25 * (i / 3), kNames[i]);
    openBuilt(buildPack(3950000, -7750000, {lv0, lv1, lv2}));
    Chart ch;
    DetailPainter p(scr::cinematicSky());
    p.setPack(&pack);
    p.begin(v2, 100, ch.scratch, ch.marks);
    TEST_ASSERT_EQUAL(2, p.level());
    const int n = p.layout(240, nullptr, 0);
    int large = 0, medium = 0, towns = 0;
    for (int i = 0; i < n; ++i) {
        large += ch.marks[i].kind == kAirport && ch.marks[i].rank == 0;
        medium += ch.marks[i].kind == kAirport && ch.marks[i].rank == 1;
        towns += ch.marks[i].kind == kTown;
    }
    TEST_ASSERT_EQUAL(1, large);
    TEST_ASSERT_EQUAL(10, medium);
    TEST_ASSERT_EQUAL(5, towns);

    // Level 0: far more small fields than marks; the towns still get their share.
    LevelSpec crowd{25, 250, 3800000, -7800000, 8, 8, {}};
    std::vector<Feat> g;
    for (int i = 0; i < 60; ++i) g.push_back(point(kAirport, 2, 40 + 32 * (i % 60), 200 + 300 * (i % 3), "K1"));
    for (int i = 0; i < 12; ++i) g.push_back(point(kTown, 2, 400 + 120 * i, 650, "T"));
    crowd.tiles[{4, 3}].feats = g;
    openBuilt(buildPack(3900000, -7700000, {crowd}));
    DetailPainter q(scr::cinematicSky());
    q.setPack(&pack);
    q.begin(viewFor(kLakeLat, kLakeLon, 5), 5, ch.scratch, ch.marks);
    const int m = q.layout(240, nullptr, 0);
    TEST_ASSERT_TRUE(m <= DetailPainter::kMaxMarks);
    towns = 0;
    for (int i = 0; i < m; ++i) towns += ch.marks[i].kind == kTown;
    TEST_ASSERT_TRUE(m - towns <= DetailPainter::kMaxMarks - 10);
    TEST_ASSERT_TRUE(towns > 3);
}

// Points under the header and against the chart's other edges get no mark, though a name would fit.
static void test_no_marks_at_the_chart_edges() {
    LevelSpec l{25, 250, 3800000, -7800000, 8, 8, {}};
    const MapView v = viewFor(kLakeLat, kLakeLon, 5);
    addPoint(l, v, kAirport, 2, 120, 10, "KTOP");
    addPoint(l, v, kAirport, 2, 170, 17, "KT17");
    addPoint(l, v, kAirport, 2, 3, 100, "KLFT");
    addPoint(l, v, kAirport, 2, 237, 100, "KRGT");
    addPoint(l, v, kAirport, 2, 120, 267, "KBOT");
    addPoint(l, v, kAirport, 2, 60, 19, "KOK1");
    addPoint(l, v, kAirport, 2, 120, 100, "KOK2");
    addPoint(l, v, kAirport, 2, 120, 265, "KOK3");
    openBuilt(buildPack(3900000, -7700000, {l}));
    Chart ch;
    DetailPainter p(scr::cinematicSky());
    p.setPack(&pack);
    p.begin(v, 5, ch.scratch, ch.marks);
    TEST_ASSERT_EQUAL(3, p.layout(240, nullptr, 0));
    for (int i = 0; i < 3; ++i) TEST_ASSERT_EQUAL_STRING_LEN("KOK", ch.marks[i].name, 3);
}

// The rank mask: passes skip tiles that lack the rank, which is most of layout's reads, and lose nothing by it.
static void test_rank_mask_skips_tiles_and_keeps_names() {
    openFixture();
    MemFile* f = static_cast<MemFile*>(pack.file());
    Chart ch;
    DetailPainter p(scr::cinematicSky());
    p.setPack(&pack);
    p.begin(viewFor(kFixLat, kFixLon, 25), 25, ch.scratch, ch.marks);
    const int before = f->reads;
    const int n = p.layout(240, nullptr, 0);
    TEST_ASSERT_TRUE(n > 3);
    const int reads = f->reads - before;
    char msg[32];
    std::snprintf(msg, sizeof msg, "%d reads", reads);
    TEST_ASSERT_TRUE_MESSAGE(reads < 5000, msg);  // 8,804 with no mask

    // More tiles than the mask holds (near the pole a view is many columns wide): the ones beyond it still count.
    const MapView v = viewFor(78.0, 10.0, 25);
    LevelSpec l{25, 250, 7600000, 500000, 16, 40, {}};
    addPoint(l, v, kAirport, 2, 220, 40, "KFAR");
    addPoint(l, v, kTown, 3, 20, 240, "Near");
    openBuilt(buildPack(7800000, 1000000, {l}));
    DetailPainter q(scr::cinematicSky());
    q.setPack(&pack);
    q.begin(v, 25, ch.scratch, ch.marks);
    TEST_ASSERT_EQUAL(2, q.layout(240, nullptr, 0));
    TEST_ASSERT_EQUAL_STRING("KFAR", ch.marks[0].name);
    TEST_ASSERT_EQUAL_STRING("Near", ch.marks[1].name);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_fill_matches_the_rings_around_a_lake_and_island);
    RUN_TEST(test_fill_matches_the_rings_across_the_chart_edges);
    RUN_TEST(test_fill_matches_the_rings_on_the_fixture);
    RUN_TEST(test_no_seams_between_land_tiles);
    RUN_TEST(test_no_seams_with_borders_on_sub_pixel_centres);
    RUN_TEST(test_fill_with_home_on_a_tile_corner);
    RUN_TEST(test_coverage_blends_are_exact_quarters);
    RUN_TEST(test_points_far_off_the_tile_are_harmless);
    RUN_TEST(test_paints_land_roads_and_water);
    RUN_TEST(test_rows_are_the_same_from_any_half_strip);
    RUN_TEST(test_half_strip_paints_only_its_rows);
    RUN_TEST(test_card_pulled_mid_paint_stops_cleanly);
    RUN_TEST(test_damaged_tile_is_skipped);
    RUN_TEST(test_more_rows_than_a_half_strip_stay_in_the_scratch);
    RUN_TEST(test_any_one_failed_read_is_reported);
    RUN_TEST(test_lines_have_their_colours_and_widths);
    RUN_TEST(test_coast_on_shores_and_not_on_tile_borders);
    RUN_TEST(test_labels_do_not_overlap_or_leave_the_chart);
    RUN_TEST(test_town_caps_by_level);
    RUN_TEST(test_places_airports_then_towns_by_rank);
    RUN_TEST(test_small_field_is_placed_before_a_big_town);
    RUN_TEST(test_editorial_town_names_are_capitals);
    RUN_TEST(test_caps_at_level_2_and_a_crowd_of_airports);
    RUN_TEST(test_no_marks_at_the_chart_edges);
    RUN_TEST(test_rank_mask_skips_tiles_and_keeps_names);
    return UNITY_END();
}
