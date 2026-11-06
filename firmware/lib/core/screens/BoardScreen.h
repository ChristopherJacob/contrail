#pragma once
// Departure Board (mockup 3B). Rows are keyed by aircraft, so a reorder slides each card
// to its new slot instead of redrawing the list in place.
#include "Screen.h"
#include "ScreenArena.h"
#include "Theme.h"
#include "app/SkyLayer.h"
#include "assets/LogoStore.h"
#include "gfx/Badge.h"
#include "ui/Format.h"
#include "ui/Identity.h"

namespace scr {

class BoardList : public gfx::Layer {
   public:
    static constexpr int kHeaderH = 72, kRows = 6, kRowH = 36, kPitch = 40, kFirstY = 78;
    static constexpr float kGlideS = 0.3f, kFadeS = 0.3f;
    explicit BoardList(const Theme& t) : t_(t) {}
    void setScene(const Scene* s) { scene_ = s, dirty_ = true; }
    // Where logos come from (nullptr: fins only). Read when rows are assigned their badges; the caller re-enters
    // the screen so rows already up load theirs.
    void setLogos(assets::LogoStore* l) { logos_ = l; }
    // Redraw everything and drop the old rows, so the list appears settled (the host's veil
    // already fades the screen in).
    void reset();
    uint32_t hitTest(int x, int y) const;
    // The rows live in the screen arena while the board is up.
    void attach(ScreenArena& a) {
        Scratch* s = a.claim<Scratch>(this);
        rows_.attach(s->rows, a, this), logoSlots_.attach(s->logos, a, this);
    }
    void update(float dt, gfx::DamageList& damage) override;
    void draw(gfx::Canvas& c) override;

   private:
    static constexpr int kSlots = 10;  // six on screen plus rows fading out
    // Logos for the rows on screen, shared by airline (rows of one airline draw one badge). Six slots, one per
    // listed row: a slot no listed row needs is reused, and a fading row whose slot is taken shows its fin.
    static constexpr int kLogoSlots = 6;
    struct LogoSlot {
        char code[4] = "";  // the airline loaded; "" never used
        gfx::Badge25 logo;  // empty when the pack lacks the airline: its rows draw their fins
    };
    struct Row {
        uint32_t hex = 0;
        bool live = false, first = false, changed = false;
        // The badge: whether the callsign names an airline (else a registration, drawn as its shape), the fin's
        // colours and the aircraft's family, so draw() looks nothing up; the airline's code ("" for a
        // registration) and the logoSlots_ slot holding its logo (-1 none).
        ui::IdentityKey id{false, ui::IdentityKey::kNeutral, 0};
        char airline[4] = "";
        int8_t logo = -1;
        float from = 0, target = 0, t = 1;  // glide from `from` to `target`; t in 0..1
        float alpha = 0;
        // What draw() uses, latched in update(): whole-pixel top and quantised opacity.
        int16_t y = 0;
        uint8_t a = 0;
        int16_t routeX = 0;  // the route sits after the callsign; 0 = no room, not drawn
        char cs[9] = "", route[16] = "", sub[48] = "", dist[12] = "", alt[16] = "";
        gfx::Rgb livery{}, altCol{};  // livery: Bold Editorial's row fill
        gfx::Rgb ink{};  // text colour on the row (Editorial: black or white for its livery fill)
        uint32_t sig = 0;
    };
    // In the screen arena (ScreenArena::kBytes 4096): 4016 B on the native build (Row 152 B x 10, LogoSlot
    // 416 B x 6), about 3896 B on the ESP32 (4-byte pointers in each Badge). Every byte added to Row costs ten
    // here, and there are only 80 B to spare natively.
    struct Scratch {
        Row rows[kSlots];
        LogoSlot logos[kLogoSlots];
    };
    static gfx::Rect rowRect(int y) { return {8, y - 1, 224, kRowH + 2}; }
    void sync(gfx::DamageList& damage);
    void fill(Row& r, const SceneFlight& f, bool first) const;
    Row* findSlot(uint32_t hex);
    Row* freeSlot();
    void assignLogos();
    int logoSlotOf(const char* code) const;
    void drawRow(gfx::Canvas& c, const Row& r) const;

    const Theme& t_;
    const Scene* scene_ = nullptr;
    ArenaArray<Row, kSlots> rows_;
    ArenaArray<LogoSlot, kLogoSlots> logoSlots_;
    assets::LogoStore* logos_ = nullptr;
    bool full_ = true, dirty_ = true, snap_ = true;
    int count_ = -1;
    char header_[16] = "", empty_[32] = "", hint_[24] = "";
    ui::MinuteClock clock_;
    uint32_t headerSig_ = 0, emptySig_ = 0, hintSig_ = 0;
};

class BoardScreen : public Screen {
   public:
    BoardScreen(const Theme& t, ScreenArena& a);
    // Where logos come from (nullptr: fins only). The caller re-enters the screen so it loads them.
    void setLogos(assets::LogoStore* l) { list_.setLogos(l); }
    ScreenId id() const override { return ScreenId::Board; }
    void setScene(const Scene* s) override;
    void enter() override { list_.attach(arena_), sky_.setFlat(t_.flat, t_.boardBgTop), sky_.invalidate(), list_.reset(); }
    NavRequest onTap(int x, int y) override;
    int layerCount() const override { return 2; }
    gfx::Layer* const* layers() override { return layers_; }

   private:
    const Theme& t_;
    app::SkyLayer sky_;
    BoardList list_;
    ScreenArena& arena_;
    gfx::Layer* layers_[2] = {&sky_, &list_};
};

}  // namespace scr
