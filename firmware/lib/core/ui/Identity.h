#pragma once
// What identifies one flight on Closest Flight, the Board and the Alert (5a spec §4, §6): its airline's logo
// when one is loaded, else the airline's tail fin, else the aircraft's shape for a registration.
#include <cstdint>
#include <cstring>

#include "assets/LogoStore.h"
#include "data/Airlines.h"
#include "data/Shapes.h"
#include "gfx/Badge.h"
#include "gfx/Canvas.h"
#include "screens/Theme.h"

namespace ui {

struct Identity {
    const gfx::Badge* logo = nullptr;        // loaded logo, or nullptr
    bool airline = false;                    // the callsign has an ICAO airline prefix
    const data::Airline* colours = nullptr;  // that airline's table entry; nullptr: a neutral fin
    data::Family family = data::Family::None;
};
// The airline and family for a flight, without the logo (callers that load logos set it).
Identity identityOf(const char* callsign, uint8_t shape);

// An Identity without its logo, in three plain bytes: for the structs that are zeroed, compared byte for byte
// or hashed (the screens' Text, the Board's arena rows). get() rebuilds the Identity at draw.
struct IdentityKey {
    static constexpr uint8_t kNeutral = 0xFF;  // colours: no table entry (a neutral fin)
    bool airline;
    uint8_t colours;  // index into data::kAirlines, or kNeutral
    uint8_t family;   // data::Family
    static IdentityKey of(const char* callsign, uint8_t shape);
    Identity get(const gfx::Badge* logo) const;
};

// One flight's identity in a side x side square at (x, y). A logo larger than `side` is skipped. Bold Editorial
// draws its white tile under a logo too, as under a fin or shape.
void drawIdentity(gfx::Canvas& c, int x, int y, int side, const Identity& id, const scr::Theme& t, uint8_t a = 255);

// The logo for `callsign`'s airline into `out`, when the airline differs from `code` (the airline loaded last,
// "" none), which it then records. While the airline stays, what is loaded is kept, and so is the absence of
// one, so a logo the pack lacks is not looked up again. A registration (no airline) leaves `out` empty.
template <class Store>
void reloadLogo(assets::LogoStore* logos, const char* callsign, char code[4], Store& out) {
    char now[4] = "";
    const bool airline = callsign && data::callsignAirline(callsign, now);
    if (!airline) now[0] = 0;  // never a partial prefix
    if (!std::strcmp(now, code)) return;
    std::memcpy(code, now, sizeof now);
    out.badge = gfx::Badge{};
    if (airline && logos) logos->load(now, out);
}

}  // namespace ui
