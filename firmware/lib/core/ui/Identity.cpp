#include "Identity.h"

#include "ui/TailFin.h"

namespace ui {
using namespace gfx;

Identity identityOf(const char* callsign, uint8_t shape) { return IdentityKey::of(callsign, shape).get(nullptr); }

IdentityKey IdentityKey::of(const char* callsign, uint8_t shape) {
    IdentityKey k{false, kNeutral, 0};
    char code[4];
    if (callsign && data::callsignAirline(callsign, code)) {
        k.airline = true;
        const data::Airline* al = data::airlineByCode(code);
        const int i = al ? int(al - data::kAirlines) : -1;
        if (i >= 0 && i < kNeutral) k.colours = uint8_t(i);
    }
    k.family = shape <= data::kFamilyCount ? shape : 0;  // an out-of-range family draws as unknown
    return k;
}

Identity IdentityKey::get(const gfx::Badge* logo) const {
    Identity id;
    id.logo = logo;
    id.airline = airline;
    id.colours = airline && colours != kNeutral ? &data::kAirlines[colours] : nullptr;
    id.family = data::Family(family);
    return id;
}

void drawIdentity(Canvas& c, int x, int y, int side, const Identity& id, const scr::Theme& t, uint8_t a) {
    if (id.logo && !id.logo->empty() && id.logo->w <= side && id.logo->h <= side) {
        // Bold Editorial sets every badge on its white tile, logos too: a logo in the airline's own colour
        // (Delta's red) would vanish into a band or row filled with that airline's livery.
        if (t.flat) identityTile(c, x, y, side, t, a);
        // A smaller image is centred (a 30 px logo in a 30 px slot is exact).
        drawBadge(c, *id.logo, x + (side - id.logo->w) / 2, y + (side - id.logo->h) / 2, a);
    } else if (id.airline) {
        drawFin(c, x, y, side, id.colours, t, a);
    } else {
        drawFamilyTile(c, x, y, side, id.family, t, a);
    }
}

}  // namespace ui
