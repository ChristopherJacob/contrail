#pragma once
// What identifies a flight when no logo is loaded (5a spec §4): the airline's tail fin in its colours on a
// tile, or the aircraft's shape for a registration. ui/Identity.h picks between a loaded logo, the fin and
// the shape.
#include "data/Airlines.h"
#include "data/Shapes.h"
#include "gfx/Canvas.h"
#include "screens/Theme.h"

namespace ui {

// The tile behind a fin or shape: Cinematic a rounded navy square, Bold Editorial white with 3 px corners.
void identityTile(gfx::Canvas& c, int x, int y, int side, const scr::Theme& t, uint8_t a = 255);
// A fin for `al` (nullptr: a neutral fin for an airline the colour table lacks) on its tile.
void drawFin(gfx::Canvas& c, int x, int y, int side, const data::Airline* al, const scr::Theme& t, uint8_t a = 255);
// The family's top view, nose up, on its tile (a registration or unknown operator).
void drawFamilyTile(gfx::Canvas& c, int x, int y, int side, data::Family f, const scr::Theme& t, uint8_t a = 255);
// The stripe colour for a fin of `fill`: `color`, unless the two are too alike (RGB distance < 80), then
// white or near-black, whichever contrasts more with `fill`.
gfx::Rgb finStripe(gfx::Rgb fill, gfx::Rgb color);

}  // namespace ui
