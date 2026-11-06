#pragma once
// Display names for common ICAO aircraft type designators. A short table in flash:
// unknown codes are shown as the raw designator.
namespace data {

struct AircraftType {
    const char* code;
    const char* name;
};
extern const AircraftType kAircraftTypes[];
extern const int kAircraftTypeCount;

// nullptr when the code is unknown or empty.
const char* aircraftTypeName(const char* icaoType);

// A well-formed ICAO type designator: 2-4 characters, A-Z and 0-9, starting with a letter; not a
// placeholder or ground-vehicle code. The logbook counts these whether or not the table names them.
bool validTypeCode(const char* code);

}  // namespace data
