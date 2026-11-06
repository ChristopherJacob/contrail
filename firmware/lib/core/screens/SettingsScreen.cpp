#include "SettingsScreen.h"

#include <cstdio>
#include <cstring>

#include "gfx/Font.h"
#include "gfx/Raster.h"
#include "gfx/fonts/Fonts.h"
#include "services/CoreSettings.h"
#include "ui/Widgets.h"

namespace scr {

namespace {

constexpr int kBrightMin = 5, kBrightMax = 255;
const DirectorMode kModes[] = {DirectorMode::Auto, DirectorMode::Carousel, DirectorMode::Stay};
const char* const kModeKeys[] = {"director", "carousel", "stay"};  // the screen_mode options
const char* const kModeLabels[] = {"Director", "Carousel", "Stay"};
const char* const kModeCaps[] = {"DIRECTOR", "CAROUSEL", "STAY"};

// Hit test with a little slack for the resistive panel's jitter (gaps between options are ≥ 4 px).
bool hit(const gfx::Rect& r, int x, int y) {
    constexpr int s = 2;
    return x >= r.x - s && x < r.right() + s && y >= r.y - s && y < r.bottom() + s;
}
bool inside(const gfx::Rect& r, int x, int y) { return x >= r.x && x < r.right() && y >= r.y && y < r.bottom(); }
// The baseline that centres one line of `f` in `r`.
int midline(const gfx::FontData& f, const gfx::Rect& r) { return r.y + (r.h + f.ascent - f.descent) / 2; }

void outline(gfx::Canvas& c, const gfx::Rect& r, gfx::Rgb col) {
    gfx::fillRoundRect(c, {r.x, r.y, r.w, 1}, 0, col);
    gfx::fillRoundRect(c, {r.x, r.bottom() - 1, r.w, 1}, 0, col);
    gfx::fillRoundRect(c, {r.x, r.y, 1, r.h}, 0, col);
    gfx::fillRoundRect(c, {r.right() - 1, r.y, 1, r.h}, 0, col);
}

}  // namespace

// ------------------------------------------------------------ SettingsView

int SettingsView::brightnessAt(int x) {
    float f = float(x - kSliderX0) / float(kSliderX1 - kSliderX0);
    f = f < 0 ? 0 : f > 1 ? 1 : f;
    return kBrightMin + int(f * (kBrightMax - kBrightMin) + 0.5f);
}

void SettingsView::setValues(const PanelValues& v) {
    if (v.brightness != v_.brightness) dirty(kBrightCard);
    if (v.editorial != v_.editorial) dirty(kThemeCard);
    if (v.radiusNm != v_.radiusNm) dirty(kRadiusCard);
    if (v.mode != v_.mode) dirty(kModeCard);
    v_ = v;
}

void SettingsView::dirty(const gfx::Rect& r) {
    for (int i = 0; i < nDirty_; ++i)
        if (dirty_[i] == r) return;
    if (nDirty_ < 4) dirty_[nDirty_++] = r;
}

void SettingsView::update(float, gfx::DamageList& damage) {
    if (full_) damage.addAll();
    else
        for (int i = 0; i < nDirty_; ++i) damage.add(dirty_[i]);
    full_ = false, nDirty_ = 0;
}

const gfx::FontData& SettingsView::labelFont() const { return t_.upperHeads ? *t_.fontLabel : gfx::kFontSmall; }

void SettingsView::drawHeader(gfx::Canvas& c) const {
    gfx::drawText(c, *t_.fontTitle, 12, 33, cap("Settings", "SETTINGS"), t_.text);
    gfx::fillRoundRect(c, kDone, t_.flat ? 0 : 15, t_.accent);
    const gfx::FontData& f = t_.upperHeads ? *t_.fontLabel : gfx::kFontBold;
    gfx::drawText(c, f, kDone.x + kDone.w / 2, midline(f, kDone), cap("Done", "DONE"), t_.boardBgTop,
                  gfx::Align::Center);
    if (t_.flat) gfx::fillRoundRect(c, {12, 44, 216, 2}, 0, t_.accent);
}

void SettingsView::cardBg(gfx::Canvas& c, const gfx::Rect& r) const {
    if (t_.flat) gfx::fillRoundRect(c, r, 0, t_.rowFill);
    else ui::card(c, r, 10, t_.rowFill, t_.cardEdge);
}

void SettingsView::rowLabel(gfx::Canvas& c, const gfx::Rect& card, int baseline, const char* mixed,
                            const char* upper) const {
    gfx::drawText(c, labelFont(), card.x + 10, baseline, cap(mixed, upper), t_.upperHeads ? t_.textDim : t_.textSoft);
}

void SettingsView::option(gfx::Canvas& c, const gfx::Rect& r, const char* label, bool on,
                          const gfx::FontData* font) const {
    if (t_.flat) {
        if (on) gfx::fillRoundRect(c, r, 0, t_.text);
        else outline(c, r, t_.cardEdge);
    } else {
        gfx::fillRoundRect(c, r, 8, on ? t_.accent : t_.cardEdge);
        const gfx::Rect in{r.x + 1, r.y + 1, r.w - 2, r.h - 2};
        gfx::fillRoundRect(c, in, 7, t_.rowFill);
        if (on) gfx::fillRoundRect(c, in, 7, t_.accent, 36);
    }
    // The theme's label face when headings are in capitals (Editorial), else kFontBody; the mode buttons pass
    // kFontSmall to fit, −/+ pass kFontTitle.
    const gfx::FontData& f = font ? *font : t_.upperHeads ? *t_.fontLabel : gfx::kFontBody;
    const gfx::Rgb ink = on ? (t_.flat ? t_.boardBgTop : t_.text) : t_.textSoft;
    gfx::drawText(c, f, r.x + r.w / 2, midline(f, r), label, ink, gfx::Align::Center);
}

void SettingsView::draw(gfx::Canvas& c) {
    gfx::fillRoundRect(c, c.clip(), 0, t_.boardBgTop);
    char buf[16];
    if (c.clip().intersects({0, 0, 240, 47})) drawHeader(c);
    if (c.clip().intersects(kBrightCard)) {
        cardBg(c, kBrightCard);
        rowLabel(c, kBrightCard, kBrightCard.y + 18, "Brightness", "BRIGHTNESS");
        std::snprintf(buf, sizeof buf, "%d%%", (v_.brightness * 100 + 127) / 255);
        gfx::drawText(c, *t_.fontLabel, kBrightCard.right() - 10, kBrightCard.y + 18, buf, t_.text, gfx::Align::Right);
        const float frac = float(v_.brightness - kBrightMin) / float(kBrightMax - kBrightMin);
        ui::progressBar(c, kSliderX0, kSliderY, kSliderX1 - kSliderX0, frac, t_.cardEdge, t_.accent, t_.flat);
        const float kx = kSliderX0 + frac * (kSliderX1 - kSliderX0);
        if (t_.flat) gfx::fillRoundRect(c, {int(kx) - 4, kSliderY - 10, 8, 20}, 0, t_.text);
        else gfx::aaCircle(c, kx, float(kSliderY), 9, t_.text);
    }
    if (c.clip().intersects(kThemeCard)) {
        cardBg(c, kThemeCard);
        rowLabel(c, kThemeCard, kThemeCard.y + 18, "Theme", "THEME");
        option(c, kTheme[0], cap("Cinematic", "CINEMATIC"), !v_.editorial);
        option(c, kTheme[1], cap("Editorial", "EDITORIAL"), v_.editorial);
    }
    if (c.clip().intersects(kRadiusCard)) {
        cardBg(c, kRadiusCard);
        rowLabel(c, kRadiusCard, midline(labelFont(), kRadiusCard), "Radius", "RADIUS");
        option(c, kMinus, "-", false, &gfx::kFontTitle);
        option(c, kPlus, "+", false, &gfx::kFontTitle);
        std::snprintf(buf, sizeof buf, t_.upperHeads ? "%d NM" : "%d nm", v_.radiusNm);
        gfx::drawText(c, *t_.fontLabel, (kMinus.right() + kPlus.x) / 2, midline(*t_.fontLabel, kRadiusCard), buf,
                      t_.text, gfx::Align::Center);
    }
    if (c.clip().intersects(kModeCard)) {
        cardBg(c, kModeCard);
        rowLabel(c, kModeCard, kModeCard.y + 18, "Screens", "SCREENS");
        for (int i = 0; i < 3; ++i)
            option(c, kMode[i], t_.upperHeads ? kModeCaps[i] : kModeLabels[i], v_.mode == kModes[i], &gfx::kFontSmall);
    }
    gfx::drawText(c, labelFont(), 120, 312, cap("More at contrail.local", "MORE AT CONTRAIL.LOCAL"), t_.textDim,
                  gfx::Align::Center);
}

// ------------------------------------------------------------ SettingsScreen

void SettingsScreen::open(uint32_t now) {
    armed_ = false, down_ = true, dragging_ = false, close_ = false, lastTouch_ = now;
}

void SettingsScreen::setValues(const PanelValues& v) {
    latest_ = v;
    if (!dragging_) view_.setValues(v);
}

void SettingsScreen::cancel() {
    dragging_ = false, down_ = false, armed_ = true;
    view_.setValues(latest_);
}

void SettingsScreen::raw(bool pressed, int x, int y, uint32_t now) {
    const bool start = pressed && !down_;
    down_ = pressed;
    if (!pressed) {
        if (dragging_) {
            dragging_ = false;
            push(svc::key::kBrightMode, "manual");
            pushInt(svc::key::kBrightness, view_.values().brightness);
        }
        armed_ = true;
        return;
    }
    lastTouch_ = now;
    if (!armed_) return;
    if (start) dragging_ = inside(SettingsView::kSliderHit, x, y);
    if (dragging_) {
        PanelValues v = view_.values();
        v.brightness = SettingsView::brightnessAt(x);
        view_.setValues(v);
    }
}

NavRequest SettingsScreen::onTap(int x, int y) {
    if (!armed_) return {};
    PanelValues v = view_.values();
    if (hit(SettingsView::kDone, x, y)) close_ = true;
    for (int i = 0; i < 2; ++i)
        if (hit(SettingsView::kTheme[i], x, y)) {
            v.editorial = i == 1;
            push(svc::key::kTheme, i ? "editorial" : "cinematic");
        }
    const bool up = hit(SettingsView::kPlus, x, y);
    if (up || hit(SettingsView::kMinus, x, y)) {
        int r = up ? (v.radiusNm + kRadiusStep) / kRadiusStep * kRadiusStep
                   : (v.radiusNm - 1) / kRadiusStep * kRadiusStep;  // to the next multiple of 5
        v.radiusNm = r < kRadiusMin ? kRadiusMin : r > kRadiusMax ? kRadiusMax : r;
        pushInt(svc::key::kRadius, v.radiusNm);
    }
    for (int i = 0; i < 3; ++i)
        if (hit(SettingsView::kMode[i], x, y)) {
            v.mode = kModes[i];
            push(svc::key::kScreenMode, kModeKeys[i]);
        }
    view_.setValues(v);
    return {};
}

void SettingsScreen::push(const char* key, const char* value) {
    int i = 0;
    while (i < nEdits_ && std::strcmp(edits_[i].key, key)) ++i;  // a newer value for a queued key replaces it
    if (i == nEdits_) {
        if (nEdits_ == kMaxEdits) return;
        ++nEdits_;
    }
    edits_[i].key = key;
    std::snprintf(edits_[i].value, sizeof edits_[i].value, "%s", value);
}

void SettingsScreen::pushInt(const char* key, int v) {
    char b[12];
    std::snprintf(b, sizeof b, "%d", v);
    push(key, b);
}

bool SettingsScreen::takeEdit(SettingEdit& out) {
    if (!nEdits_) return false;
    out = edits_[0];
    for (int i = 1; i < nEdits_; ++i) edits_[i - 1] = edits_[i];
    --nEdits_;
    return true;
}

}  // namespace scr
