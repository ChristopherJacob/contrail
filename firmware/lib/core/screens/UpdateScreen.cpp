#include "UpdateScreen.h"

#include <cstdio>

#include "gfx/Font.h"
#include "gfx/Raster.h"
#include "gfx/fonts/Fonts.h"
#include "ui/Widgets.h"

namespace scr {

void UpdateView::update(float, gfx::DamageList& damage) {
    if (full_) damage.addAll();
    else if (dirty_) damage.add(kLive);
    full_ = dirty_ = false;
}

void UpdateView::draw(gfx::Canvas& c) {
    gfx::fillRoundRect(c, c.clip(), 0, t_.boardBgTop);
    const bool caps = t_.upperHeads;
    gfx::drawText(c, gfx::kFontSmall, 120, 118, "CONTRAIL", t_.textDim, gfx::Align::Center);
    gfx::drawText(c, *t_.fontTitle, 120, 150, caps ? "UPDATING" : "Updating", t_.text, gfx::Align::Center);
    char buf[8];
    std::snprintf(buf, sizeof buf, "%d%%", pct_);
    gfx::drawText(c, *t_.fontDisplay, 120, 205, buf, t_.accent, gfx::Align::Center);
    ui::progressBar(c, 30, 226, 180, pct_ / 100.f, t_.cardEdge, t_.accent, t_.flat);
    gfx::drawText(c, gfx::kFontSmall, 120, 258,
                  caps ? "DON'T UNPLUG. RESTARTS WHEN DONE." : "Don't unplug. Restarts when done.", t_.textSoft,
                  gfx::Align::Center);
}

}  // namespace scr
