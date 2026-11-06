#pragma once
// Live Map (mockup 5A, "night chart"): coastlines, lakes and airports from the built-in base map
// around home, north up, the radius setting across half the width; planes with trails over it; a
// strip along the bottom like the Radar's. The mask, airport marks and plane states live in the
// screen arena while the map is up. With a map pack on the card (5b) the chart is the pack's,
// drawn over frames and saved on the card; frames read it back from there.
#include "MapPlanes.h"
#include "MapTrails.h"
#include "Screen.h"
#include "ScreenArena.h"
#include "Theme.h"
#include "map/Airports.h"
#include "map/ChartCache.h"
#include "map/DetailPainter.h"
#include "map/LandMask.h"
#include "map/MapPack.h"
#include "ui/Format.h"

namespace scr {

// The chart: land and water from the mask (rebuilt only when the map opens or home or the radius
// change), the coastline where the smoothed mask changes, range rings at ½ and full radius,
// airports, the scale bar and the north arrow. Nothing here changes between rebuilds. A rebuild of
// the detailed chart runs over frames (see update()); until then ready() is false.
class MapBase : public gfx::Layer {
   public:
    static constexpr int kMapH = 270, kMaxMarks = 20;
    explicit MapBase(const Theme& t) : t_(t), painter_(t) {}
    // The mask, marks and the detailed chart's sub-pixel rows and marks are scratch `owner` claimed from the arena.
    // `mask` and `sub`, and `marks` and `dmarks`, may overlap: only the map in use touches its half.
    void attach(uint8_t* mask, basemap::AirportMark* marks, uint8_t* sub, basemap::DetailMark* dmarks,
                const ScreenArena& a, const void* owner) {
        mask_ = basemap::LandMask(mask), marks_.attach(marks, a, owner), nMarks_ = 0;
        sub_ = sub, dmarks_.attach(dmarks, a, owner);
    }
    void setPack(basemap::MapPack* p) { pack_ = p, painter_.setPack(p), full_ = true; }
    // A microsecond clock: the detailed chart's job then paints for about kJobFrameUs a frame, and its log line has
    // times. Without one (tests) it paints kJobBands bands a frame.
    void setClock(uint32_t (*us)()) { clock_ = us; }
    void setScene(const Scene* s) { scene_ = s, dirty_ = true; }
    void reset() { full_ = true; }
    const basemap::MapView& view() const { return view_; }
    bool located() const { return located_; }
    uint32_t version() const { return version_; }  // bumped by every rebuild: planes move with it
    // False while a detailed chart is being drawn: no planes, and "Drawing map…" in the strip.
    bool ready() const { return !detailed_ || ready_; }
    // The chart comes from the card: planes move a whole pixel at a time, as each redraw reads the chart back.
    bool detailed() const { return detailed_; }
    // What labels keep clear of: the airport (and town) marks of the map in use, home and its halo,
    // and the chart's own text (header, north arrow, scale bar).
    struct ChartMark {
        int16_t x, y;
        bool labelled;
        gfx::Rect box;
    };
    int chartMarkCount() const { return nMarks_; }
    ChartMark chartMark(int i) const {
        if (detailed_) {
            const basemap::DetailMark& m = dmarks_[i];
            return {m.x, m.y, m.labelled, m.box()};
        }
        const basemap::AirportMark& m = marks_[i];
        return {m.x, m.y, m.labelled, m.box()};
    }
    gfx::Rect homeBox() const { return {int(view_.cx) - 9, int(view_.cy) - 9, 19, 19}; }
    static constexpr int kFixedText = 3;
    const gfx::Rect* fixedText() const { return fixed_; }
    void update(float dt, gfx::DamageList& damage) override;
    void draw(gfx::Canvas& c) override;

   private:
    struct ViewKey {
        double lat = 0, lon = 0;
        float radius = 0;
        bool located = false;
        bool operator==(const ViewKey& o) const {
            return lat == o.lat && lon == o.lon && radius == o.radius && located == o.located;
        }
    };
    static constexpr int kJobBands = 2;              // of the job, a frame, without a clock
    static constexpr uint32_t kJobFrameUs = 60000;  // with one: bands until a frame has spent this (at least one)
    static constexpr float kRadiusSettleS = 1.5f;
    void rebuild();
    void buildShades();
    basemap::ChartKey chartKey() const;
    bool startDetail();  // false: a card error, and the built-in map it is
    void detailDraw(gfx::Canvas& c, const gfx::Rect& chart);
    void overlay(gfx::Canvas& c, const gfx::Rect& chart);  // rings, marks, names, home, scale bar, header
    void dropDetail();                                     // a card error in the job or a read-back
    const Theme& t_;
    const Scene* scene_ = nullptr;
    basemap::LandMask mask_;
    ArenaArray<basemap::AirportMark, kMaxMarks> marks_;
    basemap::MapPack* pack_ = nullptr;
    basemap::DetailPainter painter_;
    uint8_t* sub_ = nullptr;
    ArenaArray<basemap::DetailMark, basemap::DetailPainter::kMaxMarks> dmarks_;
    bool detailed_ = false, ready_ = true;
    bool dropped_ = false;  // the detailed map went this frame: nothing is built until the next update()
    bool full_ = true, dirty_ = true, located_ = false;
    uint8_t jobBands_ = 0, jobAsk_ = 1;  // bands the job took this frame; bands its next damage asks for
    int jobRow_ = 0;        // rows above it are drawn and saved; below it the screen shows water
    uint32_t (*clock_)() = nullptr;
    uint32_t now() const { return clock_ ? clock_() : 0; }
    uint32_t frameUs_ = 0;  // when this frame's update() ran
    // Where a job's time went, for its log line.
    struct JobStats {
        uint32_t startUs, layoutUs, paintUs, frames;
        hal::IoStats io;
    } job_{};
    template <class F>
    uint32_t cpuUs(F&& f);
    float settle_ = 0, pendingRadius_ = 0;
    int nMarks_ = 0;
    basemap::MapView view_;
    ViewKey key_;
    uint32_t version_ = 0;
    float radius_ = 25, scalePx_ = 0;
    char scale_[12] = "", header_[24] = "";
    gfx::Rect fixed_[kFixedText];
    // The chart's colour for every pixel, by the four mask cells around it (4 bits) and where the
    // pixel sits between their centres (2 bits): see buildShades().
    uint16_t shade_[64] = {};
};

// The bottom strip: the selected plane, else the closest (callsign, route, airline · type,
// distance, altitude in its trend colour). Tapping it opens the plane in Closest Flight.
class MapStrip : public gfx::Layer {
   public:
    static constexpr gfx::Rect kBox{10, 272, 220, 38};
    MapStrip(const Theme& t, const MapBase& base) : t_(t), base_(base) {}
    void setScene(const Scene* s) { scene_ = s, dirty_ = true; }
    void setSelection(uint32_t hex) { selected_ = hex, dirty_ = true; }
    void reset() { sig_ = 0, dirty_ = true; }
    uint32_t shownHex() const { return shown_; }
    void update(float dt, gfx::DamageList& damage) override;
    void draw(gfx::Canvas& c) override;

   private:
    struct Text {
        char callsign[9] = "", sub[48] = "", alt[24] = "", dist[24] = "";
        uint8_t trend = 0;  // 0 level, 1 climbing, 2 descending
    };
    const SceneFlight* pick() const;
    void compose(const SceneFlight& f, Text& t) const;
    const Theme& t_;
    const MapBase& base_;
    const Scene* scene_ = nullptr;
    // What draw() shows, composed when the scene or the selection changes (not once per strip).
    Text text_;
    bool shows_ = false, located_ = true, dirty_ = true, drawing_ = false;
    uint32_t selected_ = 0, shown_ = 0, sig_ = 0;
};

class MapScreen : public Screen {
   public:
    MapScreen(const Theme& t, ScreenArena& a, const MapTrails& trails)
        : base_(t), planes_(t, base_, trails), strip_(t, base_), arena_(a) {}
    ScreenId id() const override { return ScreenId::Map; }
    void setScene(const Scene* s) override { base_.setScene(s), planes_.setScene(s), strip_.setScene(s); }
    void setSelection(uint32_t hex) override;
    void setPack(basemap::MapPack* p) { base_.setPack(p); }
    void setClock(uint32_t (*us)()) { base_.setClock(us); }
    void enter() override;
    NavRequest onTap(int x, int y) override;
    int layerCount() const override { return 3; }
    gfx::Layer* const* layers() override { return layers_; }

   private:
    struct Scratch {
        union {  // one map or the other; rebuild() fills what it uses
            struct {
                uint8_t mask[basemap::LandMask::kBytes];
                basemap::AirportMark marks[MapBase::kMaxMarks];
            } builtin;
            struct {
                uint8_t sub[basemap::DetailPainter::kScratchBytes];
                basemap::DetailMark marks[basemap::DetailPainter::kMaxMarks];
            } detail;
        };
        MapPlanes::Plane planes[Scene::kMaxFlights];
        Scratch() : builtin{} {}
    };
    MapBase base_;
    MapPlanes planes_;
    MapStrip strip_;
    ScreenArena& arena_;
    gfx::Layer* layers_[3] = {&base_, &planes_, &strip_};
};

}  // namespace scr
