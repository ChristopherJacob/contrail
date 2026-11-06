#include "BoardScreen.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "data/AircraftTypes.h"
#include "data/Airlines.h"
#include "gfx/Font.h"
#include "gfx/Raster.h"
#include "gfx/fonts/Fonts.h"
#include "ui/Format.h"
#include "ui/Identity.h"
#include "ui/Widgets.h"

namespace scr {
using namespace gfx;

namespace {
const Rect kHeaderText{0, 6, 240, 60};
const Rect kListArea{0, BoardList::kHeaderH, 240, 320 - BoardList::kHeaderH};
const Rect kHint{0, 296, 240, 18};  // below the fifth card, where the sixth would be
constexpr int kShadowH = 6;         // the header casts a soft shadow onto the list
constexpr int kTextX = 45, kRight = 224, kGap = 8;  // card text columns, after the badge
constexpr int kBadgeX = 14, kBadgeDy = 6, kBadgeSide = 25;  // the logo, fin or shape at each row's left

// Ease in and out, so a gliding card starts and stops gently.
float ease(float t) {
    if (t < 0.5f) return 4 * t * t * t;
    float u = 2 - 2 * t;
    return 1 - u * u * u / 2;
}
float position(float from, float to, float t) { return t >= 1.f ? to : from + (to - from) * ease(t); }
}  // namespace

// ------------------------------------------------------------------ BoardList

void BoardList::reset() {
    full_ = dirty_ = snap_ = true;
    for (auto& r : rows_) r.hex = 0;
}

BoardList::Row* BoardList::findSlot(uint32_t hex) {
    if (!hex) return nullptr;  // 0 marks an empty slot, so it never matches one
    for (auto& r : rows_)
        if (r.hex == hex) return &r;
    return nullptr;
}

// A slot for a flight that has none: an empty one, else the most faded of the rows leaving.
// Rows still listed (live) are never taken.
BoardList::Row* BoardList::freeSlot() {
    Row* best = nullptr;
    for (auto& r : rows_) {
        if (!r.hex) return &r;
        if (!r.live && (!best || r.alpha < best->alpha)) best = &r;
    }
    return best;
}

void BoardList::fill(Row& r, const SceneFlight& f, bool first) const {
    const data::Airline* al = data::airlineForCallsign(f.callsign);
    const char* tn = data::aircraftTypeName(f.type);
    std::snprintf(r.cs, sizeof r.cs, "%s", f.callsign);
    ui::formatDistance(r.dist, sizeof r.dist, f.distNm);
    char a[12];
    ui::formatThousands(a, sizeof a, f.altFt);
    const char* arrow = ui::trendArrow(f.vrateFpm);
    // Arrow first, so the numbers line up down the right-hand column.
    std::snprintf(r.alt, sizeof r.alt, "%s %s", arrow[0] ? arrow : "→", a);
    // Bold Editorial fills the row with the airline's livery colour (a dark grey row without one, as in
    // mockup 7A), text in black or white, whichever reads. Cinematic rows have no colour of their own (the badge
    // carries the airline).
    r.livery = t_.brandFill ? data::airlineFill(al, t_.rowHighlight) : Rgb{};
    r.ink = t_.brandFill ? ui::textOn(r.livery) : t_.text;
    r.altCol = t_.brandFill ? r.ink : f.vrateFpm >= 300 ? t_.climb : f.vrateFpm <= -300 ? t_.descend : t_.level;
    r.first = first;
    r.id = ui::IdentityKey::of(f.callsign, f.shape);
    if (!data::callsignAirline(f.callsign, r.airline)) r.airline[0] = 0;  // never a partial prefix in the lookups

    // The route follows the callsign when it clears the distance column; codes can't be trimmed, so
    // it is dropped otherwise (only the longest callsigns).
    r.routeX = 0, r.route[0] = 0;
    if (f.hasRoute) {
        char route[16];
        std::snprintf(route, sizeof route, "%s → %s", f.orig, f.dest);
        int x = kTextX + textWidth(*t_.fontLabel, r.cs) + 7;
        if (x + textWidth(kFontSmall, route) <= kRight - textWidth(kFontBody, r.dist) - kGap)
            std::memcpy(r.route, route, sizeof route), r.routeX = int16_t(x);
    }

    // "Airline · type": the full type name, then without the maker ("777-300ER"), then trimmed.
    const char* airline = al ? al->name : f.military ? "Military" : "";
    const char* type = tn ? tn : f.type;
    const char* sep = airline[0] && type[0] ? " · " : "";
    int maxW = kRight - textWidth(kFontSmall, r.alt) - kGap - 2 - kTextX;
    char sub[48];
    std::snprintf(sub, sizeof sub, "%s%s%s", airline, sep, type);
    if (textWidth(kFontSmall, sub) > maxW && tn) {
        // Only airliner makers: "Citation Longitude" or "Cessna 172" mean little without the first word.
        for (const char* maker : {"Airbus ", "Boeing ", "Embraer ", "Bombardier "}) {
            size_t n = std::strlen(maker);
            if (!std::strncmp(tn, maker, n)) std::snprintf(sub, sizeof sub, "%s%s%s", airline, sep, tn + n);
        }
    }
    ui::ellipsize(r.sub, sizeof r.sub, kFontSmall, sub, maxW);
}

void BoardList::sync(DamageList& damage) {
    dirty_ = false;
    for (auto& r : rows_) r.live = false;
    int count = scene_ ? scene_->count : 0;
    int n = count < kRows ? count : kRows;
    // Two passes: flights already on the board keep their rows first, so a new flight can never
    // take the row of one that is still listed.
    Row* match[kRows] = {};
    for (int i = 0; i < n; ++i) {
        Row* row = findSlot(scene_->flights[i].hex);
        if (!row) continue;
        float target = float(kFirstY + i * kPitch);
        if (target != row->target) {
            row->from = position(row->from, row->target, row->t), row->target = target, row->t = 0;
        }
        row->live = true;
        match[i] = row;
    }
    for (int i = 0; i < n; ++i) {
        const SceneFlight& f = scene_->flights[i];
        if (!match[i] && f.hex) {
            Row* row = freeSlot();
            if (!row) continue;  // unreachable: kSlots > kRows
            if (row->hex) damage.add(rowRect(row->y));  // a leaving row taken over mid-fade
            float target = float(kFirstY + i * kPitch);
            *row = Row{};
            row->hex = f.hex, row->from = row->target = target, row->y = int16_t(target);
            row->alpha = snap_ ? 1.f : 0.f;  // new rows appear in place and fade in
            row->live = true;
            match[i] = row;
        }
        Row* row = match[i];
        if (!row) continue;
        fill(*row, f, i == 0);
        uint32_t sig = ui::hashStr(row->cs, ui::hashStr(row->route, ui::hashStr(row->sub, ui::hashStr(row->dist,
                                                         ui::hashStr(row->alt)))));
        sig = ui::hashBytes(&row->livery, sizeof row->livery, ui::hashBytes(&row->altCol, sizeof row->altCol, sig));
        // The badge: which airline (so which logo or fin colours), or the shape for a registration. Its logo slot
        // is compared in assignLogos().
        sig = ui::hashStr(row->airline, sig ^ (uint32_t(row->id.airline) << 8 | row->id.family));
        sig ^= uint32_t(row->first) | uint32_t(row->routeX) << 1;
        row->changed |= sig != row->sig;
        row->sig = sig;
    }
    snap_ = false;
    assignLogos();

    // Empty and non-empty lay the list out differently; going empty drops any rows still fading.
    if ((count == 0) != (count_ == 0)) damage.add(kListArea);
    if (count == 0)
        for (auto& r : rows_) r.hex = 0;
    count_ = count;
    if (count)
        std::snprintf(header_, sizeof header_,
                      t_.upperHeads ? (count == 1 ? "%d FLIGHT" : "%d FLIGHTS") : (count == 1 ? "%d flight" : "%d flights"),
                      count);
    else std::snprintf(header_, sizeof header_, "%s", t_.upperHeads ? "NO FLIGHTS" : "No flights");
    // With room to spare, say what a tap does.
    std::snprintf(hint_, sizeof hint_, "%s", count > 0 && count < kRows ? "Tap a card for details" : "");
    if (scene_ && !scene_->online) std::snprintf(empty_, sizeof empty_, "Offline");
    else std::snprintf(empty_, sizeof empty_, "Nothing within %d nm", scene_ ? int(scene_->radiusNm + 0.5f) : 0);
    if (scene_ && scene_->timeValid) clock_.set(scene_->epoch);
    else clock_.clear();

    // Signatures of the non-row text, so update() need not hash anything per frame. The empty
    // message does not affect the header's pixels.
    uint32_t hs = ui::hashStr(header_, ui::hashStr(clock_.text()));
    if (hs != headerSig_) damage.add(kHeaderText), headerSig_ = hs;
    uint32_t hint = ui::hashStr(hint_);
    if (hint != hintSig_) damage.add(kHint), hintSig_ = hint;
    // The empty message only shows on an empty list (an offline change otherwise goes unseen).
    uint32_t es = ui::hashStr(empty_);
    if (count == 0 && es != emptySig_) damage.add(kListArea);
    emptySig_ = es;
}

int BoardList::logoSlotOf(const char* code) const {
    for (int i = 0; i < kLogoSlots; ++i)
        if (logoSlots_[i].code[0] && !std::strcmp(logoSlots_[i].code, code)) return i;
    return -1;
}

// Each row's airline to a logo slot, loading the badges of airlines new to the board. Only listed rows load: at
// most six airlines, so six slots always hold them. A slot no listed row needs is reused, preferring one never
// used, then one no row shows; a fading row whose slot is reused finds no slot for its airline and shows its fin.
void BoardList::assignLogos() {
    bool needed[kLogoSlots] = {}, shown[kLogoSlots] = {};
    for (const auto& r : rows_) {
        if (!r.hex) continue;
        if (r.logo >= 0) shown[r.logo] = true;
        int i = r.live && r.id.airline ? logoSlotOf(r.airline) : -1;
        if (i >= 0) needed[i] = true;
    }
    for (const auto& r : rows_) {
        if (!r.hex || !r.live || !r.id.airline || logoSlotOf(r.airline) >= 0) continue;
        int pick = -1, best = -1;
        for (int i = 0; i < kLogoSlots; ++i) {
            if (needed[i]) continue;
            const int score = !logoSlots_[i].code[0] ? 2 : !shown[i] ? 1 : 0;
            if (score > best) pick = i, best = score;
        }
        if (pick < 0) continue;  // unreachable: no more airlines are listed than there are slots
        LogoSlot& s = logoSlots_[pick];
        std::memcpy(s.code, r.airline, sizeof s.code);
        s.logo.badge = {};
        if (logos_) logos_->load(s.code, s.logo);
        needed[pick] = true;
    }
    for (auto& r : rows_) {
        if (!r.hex) continue;
        const int8_t logo = int8_t(r.id.airline ? logoSlotOf(r.airline) : -1);
        r.changed |= logo != r.logo;
        r.logo = logo;
    }
}

uint32_t BoardList::hitTest(int x, int y) const {
    if (!rows_ || x < 8 || x >= 232) return 0;
    // Newest-drawn last, so the topmost card wins where cards overlap mid-glide.
    for (int i = kSlots - 1; i >= 0; --i) {
        const Row& r = rows_[i];
        if (r.hex && r.live && r.a > 128 && y >= r.y && y < r.y + kRowH) return r.hex;
    }
    return 0;
}

void BoardList::update(float dt, DamageList& damage) {
    rows_.check();
    if (full_) damage.add(kListArea), damage.add(kHeaderText);
    full_ = false;
    if (dirty_) sync(damage);
    for (auto& r : rows_) {
        if (!r.hex) continue;
        // Nothing moves or fades on a settled row.
        if (r.t >= 1 && r.live && r.alpha >= 1 && r.a == 255 && !r.changed && r.y == int16_t(r.target)) continue;
        r.t = std::fmin(1.f, r.t + dt / kGlideS);
        float da = dt / kFadeS;
        r.alpha = r.live ? std::fmin(1.f, r.alpha + da) : std::fmax(0.f, r.alpha - da);
        int16_t y = int16_t(std::lround(position(r.from, r.target, r.t)));
        uint8_t a = uint8_t(r.alpha * 255 + 0.5f);
        if (y != r.y || a != r.a || r.changed) damage.add(rowRect(r.y).unite(rowRect(y)));
        r.y = y, r.a = a, r.changed = false;
        if (r.t >= 1) r.from = r.target;
        if (!r.live && a == 0) r.hex = 0;
    }
}

void BoardList::drawRow(Canvas& c, const Row& r) const {
    if (!r.a || !c.clip().intersects(rowRect(r.y))) return;
    auto al = [&](int a) { return uint8_t((a * r.a + 127) / 255); };
    const int y = r.y;
    const Rect box{8, y, 224, kRowH};
    // The logo, else the airline's fin, else the aircraft's shape, drawn after the row's fill.
    auto badge = [&] {
        const gfx::Badge* logo = nullptr;
        if (r.logo >= 0) {
            logoSlots_.check();
            const gfx::Badge& b = logoSlots_[r.logo].logo.badge;
            if (!b.empty()) logo = &b;
        }
        ui::drawIdentity(c, kBadgeX, y + kBadgeDy, kBadgeSide, r.id.get(logo), t_, r.a);
    };
    if (t_.brandFill) {
        // Bold Editorial: the whole row in the airline's colour; the nearest gets a bar in the ink colour.
        if (r.a == 255) c.fill(box, rgb565(r.livery));
        else c.fillBlend(box, r.livery, r.a);
        if (r.first) c.fillBlend({8, y, 4, kRowH}, r.ink, r.a);
        badge();
        drawText(c, *t_.fontLabel, kTextX, y + 16, r.cs, r.ink, Align::Left, al(255));
        if (r.routeX) drawText(c, kFontSmall, r.routeX, y + 15, r.route, r.ink, Align::Left, al(230));
        drawText(c, kFontSmall, kTextX, y + 29, r.sub, r.ink, Align::Left, al(215));
        drawText(c, kFontBody, kRight, y + 16, r.dist, r.ink, Align::Right, al(255));
        drawText(c, kFontSmall, kRight, y + 29, r.alt, r.ink, Align::Right, al(255));
        return;
    }
    if (r.first) {
        // The nearest card is lifted: brighter fill and a lit top edge.
        fillRoundRect(c, box, 9, t_.rowHighlight, al(215));
        aaLine(c, box.x + 9, y + 0.5f, box.right() - 9, y + 0.5f, 1, t_.textSoft, al(70));
    } else {
        fillRoundRect(c, box, 9, t_.rowFill, al(170));
    }
    badge();
    drawText(c, *t_.fontLabel, kTextX, y + 16, r.cs, t_.text, Align::Left, al(255));
    if (r.routeX) drawText(c, kFontSmall, r.routeX, y + 15, r.route, t_.accent, Align::Left, al(230));
    drawText(c, kFontSmall, kTextX, y + 29, r.sub, t_.textDim, Align::Left, al(255));
    drawText(c, kFontBody, kRight, y + 16, r.dist, t_.text, Align::Right, al(255));
    drawText(c, kFontSmall, kRight, y + 29, r.alt, r.altCol, Align::Right, al(255));
}

void BoardList::draw(Canvas& c) {
    rows_.check();
    if (c.clip().intersects(kHeaderText)) {
        ui::shadowText(c, kFontSmall, 14, 22, "OVERHEAD NOW", t_.textSoft, Align::Left, t_);
        ui::shadowText(c, kFontBody, 226, 22, clock_.text(), t_.textSoft, Align::Right, t_);
        ui::shadowText(c, *t_.fontTitle, 14, 54, header_, t_.text, Align::Left, t_);
        if (t_.flat) c.fill({14, 62, 212, 2}, rgb565(t_.text));  // the white rule under "N FLIGHTS"
    }
    if (!c.clip().intersects(kListArea)) return;
    Rgb stops[] = {t_.boardBgTop, t_.boardBgBottom};
    float pos[] = {0.f, 1.f};
    ui::vFill(c, kListArea, stops, pos, 2, t_);
    for (int i = 0; i < kShadowH && !t_.flat; ++i) {
        float f = 1 - float(i) / kShadowH;
        c.fillBlend({0, kHeaderH + i, 240, 1}, t_.shadow, uint8_t(150 * f * f));
    }
    if (count_ == 0) {
        planeGlyph(c, 120, 150, 30, 45, t_.textDim, 110);
        drawText(c, *t_.fontTitle, 120, 196, t_.upperHeads ? "QUIET SKIES" : "Quiet skies", t_.text, Align::Center);
        drawText(c, kFontBody, 120, 218, empty_, t_.textDim, Align::Center);
        return;
    }
    if (hint_[0]) drawText(c, kFontSmall, 120, 309, hint_, t_.textDim, Align::Center, 170);
    for (const auto& r : rows_)
        if (r.hex) drawRow(c, r);
}

// ------------------------------------------------------------------ BoardScreen

BoardScreen::BoardScreen(const Theme& t, ScreenArena& a) : t_(t), list_(t), arena_(a) {
    sky_.setArea({0, 0, 240, BoardList::kHeaderH});
    sky_.setRidge(false);
    static const gfx::Rect kNoStars[] = {{12, 10, 90, 18}, {160, 6, 70, 22}, {12, 30, 104, 34}};
    sky_.setStarExclusions(kNoStars, 3);
}

void BoardScreen::setScene(const Scene* s) {
    if (s) sky_.setSun(s->sunElev, s->sunAz);
    list_.setScene(s);
}

NavRequest BoardScreen::onTap(int x, int y) {
    NavRequest r;
    if (uint32_t hex = list_.hitTest(x, y)) r.open = true, r.to = ScreenId::Closest, r.selectHex = hex;
    return r;
}

}  // namespace scr
