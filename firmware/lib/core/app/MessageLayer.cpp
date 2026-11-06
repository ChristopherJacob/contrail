#include "MessageLayer.h"

#include "gfx/Raster.h"
#include "gfx/fonts/Fonts.h"
#include "util/Str.h"

namespace app {
using util::copyStr;
using namespace gfx;

void MessageLayer::set(const char* title, const char* l1, const char* l2, const char* l3) {
    copyStr(title_, sizeof title_, title);
    copyStr(l1_, sizeof l1_, l1);
    copyStr(l2_, sizeof l2_, l2);
    copyStr(l3_, sizeof l3_, l3);
    dirty_ = true;
}

void MessageLayer::update(float, DamageList& damage) {
    if (dirty_) damage.add({0, 60, 240, 200});
    dirty_ = false;
}

void MessageLayer::draw(Canvas& c) {
    const Rgb white{255, 255, 255}, soft{205, 220, 255};
    c.fillBlend({16, 80, 208, 150}, {4, 8, 24}, 140);
    planeGlyph(c, 120, 106, 28, 45, white);
    drawText(c, kFontTitle, 120, 158, title_, white, Align::Center);
    drawText(c, kFontBody, 120, 182, l1_, soft, Align::Center);
    drawText(c, kFontBody, 120, 200, l2_, soft, Align::Center);
    drawText(c, kFontSmall, 120, 220, l3_, soft, Align::Center);
}

}  // namespace app
