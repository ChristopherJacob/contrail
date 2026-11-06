#pragma once
// The web page's view of the settings registry (config spec §2–3): JSON out, with secrets never
// sent back, and JSON in, stored all or nothing.
#include <ArduinoJson.h>

#include "Settings.h"

namespace svc {

struct ApplyResult {
    bool ok = false;
    bool restart = false;  // a hardware setting changed: the board restarts after replying
};

// {"settings": [...]}, as Settings::toJsonSchema.
void schemaJson(const Settings& s, JsonDocument& out);
// Every value by key; a secret is {"set": true|false} instead of its text.
void valuesJson(const Settings& s, JsonDocument& out);
// `in` is {"key": value, ...}; values may be strings, numbers or booleans. For a secret, "" leaves
// it as it is and null clears it. Every value is checked first: on any error nothing is stored and
// `errors` gets {"key": "reason"} ("_" when `in` is not an object).
ApplyResult applyJson(Settings& s, JsonVariantConst in, JsonDocument& errors);

}  // namespace svc
