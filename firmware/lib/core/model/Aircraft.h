#pragma once
#include <cstdint>

namespace model {

enum AcFlags : uint8_t {
    AC_MILITARY = 1,
    AC_EMERGENCY = 2,  // a declared emergency or an emergency squawk
    AC_GROUND = 4,
    AC_HAS_POS = 8,
    AC_EMERG_STATUS = 16,  // the declared emergency alone (readsb "emergency" field)
};

enum class Source : uint8_t { None, Community, Local, OpenSky };

struct Aircraft {
    uint32_t hex = 0;  // ICAO 24-bit address; bit 24 set for non-ICAO (~) addresses
    char callsign[9] = "";
    char reg[9] = "";
    char type[5] = "";
    char squawk[5] = "";
    float lat = 0, lon = 0;
    int32_t altFt = 0;
    float gsKt = 0, trackDeg = 0;
    int16_t vrateFpm = 0;
    uint8_t flags = 0;
    uint8_t category = 0;  // e.g. 0xA3
    Source source = Source::None;
    uint32_t lastSeenMs = 0;
    // Derived relative to home.
    float distNm = 0, bearingDeg = 0, elevDeg = 0;

    bool has(AcFlags f) const { return flags & f; }
};

}  // namespace model
