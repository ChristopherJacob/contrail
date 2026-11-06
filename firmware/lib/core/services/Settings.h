#pragma once
// Typed settings registry. Every setting is declared once (CoreSettings.cpp) and the
// web UI and touch settings are generated from toJsonSchema().
#include <ArduinoJson.h>

#include <functional>
#include <string>
#include <vector>

#include "Lock.h"
#include "hal.h"

namespace svc {

enum class SType { Int, Float, Bool, String, Enum };

struct SettingDef {
    const char* key;  // <= 15 chars (NVS limit)
    SType type;
    const char* group;
    const char* label;
    double min, max;
    double defNum;                     // Int / Float / Bool / Enum index
    const char* defStr;                // String
    const char* const* options;        // Enum
    uint8_t optionCount;
    bool secret;
};

class Settings {
   public:
    using Listener = std::function<void(const char* key)>;

    Settings(hal::IKeyValueStore& store, const SettingDef* defs, int n);

    // Load persisted values; anything missing or invalid falls back to its default and
    // out-of-range numbers are clamped.
    void load();

    int getInt(const char* key) const;
    float getFloat(const char* key) const;
    bool getBool(const char* key) const;
    std::string getString(const char* key) const;
    // Enum value as its option string.
    std::string getEnum(const char* key) const;

    // Validate, persist and notify. Accepts the same text form the web UI posts
    // ("12.5", "true", option names for enums).
    bool set(const char* key, const char* value);
    void onChange(Listener l) { listeners_.push_back(std::move(l)); }

    void toJsonSchema(JsonDocument& doc) const;
    void toJsonValues(JsonDocument& doc, bool includeSecrets) const;

    const SettingDef* defs() const { return defs_; }
    int count() const { return n_; }
    // The definition for `key`, or nullptr. Definitions never change, so no lock is needed.
    const SettingDef* find(const char* key) const {
        int i = indexOf(key);
        return i < 0 ? nullptr : &defs_[i];
    }

   private:
    struct Value {
        double num = 0;
        std::string str;
    };
    int indexOf(const char* key) const;
    bool parse(const SettingDef& d, const char* text, Value& out) const;
    std::string format(const SettingDef& d, const Value& v) const;

    hal::IKeyValueStore& store_;
    const SettingDef* defs_;
    int n_;
    std::vector<Value> values_;
    std::vector<Listener> listeners_;
    mutable Mutex mu_;
};

}  // namespace svc
