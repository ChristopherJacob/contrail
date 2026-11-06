#pragma once
#include "hal.h"
#include "model/AircraftStore.h"

namespace prov {

struct ParseResult {
    bool ok = false;
    int seen = 0;      // elements in the response
    int accepted = 0;  // stored (had a position, within radius)
};

// Parses readsb/tar1090 JSON: the community v2/v3 APIs (`ac`) and a local receiver's
// aircraft.json (`aircraft`). Writes into `store` as one batch.
ParseResult parseReadsb(hal::IByteSource& src, model::AircraftStore& store, uint32_t nowMs, model::Source source);

// Shared helpers used by other parsers.
uint32_t parseHex(const char* s);
void copyTrimmed(char* dst, size_t cap, const char* src);
bool isEmergencySquawk(const char* sq);
uint8_t parseCategory(const char* s);

}  // namespace prov
