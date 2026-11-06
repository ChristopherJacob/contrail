#pragma once
#include "gfx/Color.h"

namespace data {

struct Airline {
    const char* icao;
    const char* name;  // short display name
    gfx::Rgb color;    // brand colour, lightened where needed to read on a dark card
    gfx::Rgb fill;     // livery colour for areas behind text; zero when the operator has no single livery
};
extern const Airline kAirlines[];
extern const int kAirlineCount;

// The ICAO airline prefix of a callsign: three letters then a digit ("UAL53" → "UAL"), whether or
// not the table knows it, in upper case ("ual53" → "UAL"). False for registrations ("N512KA") and anything else.
bool callsignAirline(const char* callsign, char out[4]);
// The table entry for an ICAO airline code, or nullptr.
const Airline* airlineByCode(const char* icao);

// Airline from a callsign's three-letter ICAO prefix followed by a digit ("UAL53").
// nullptr for registrations ("N512KA") and unknown prefixes.
const Airline* airlineForCallsign(const char* callsign);

// The colour to fill an area with behind text (Bold Editorial's band and rows): the airline's real
// livery colour, which is often too dark for `color`'s use on a dark card; its `color` where it has no
// single livery; `neutral` when there is no airline. Pair it with ui::textOn.
gfx::Rgb airlineFill(const Airline* al, gfx::Rgb neutral);

}  // namespace data
