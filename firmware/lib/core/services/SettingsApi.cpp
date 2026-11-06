#include "SettingsApi.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace svc {

namespace {

constexpr size_t kMaxText = 200;  // Settings::load reads at most 255 bytes back

// The text form Settings::set takes, or false with a reason. Unlike Settings::set, numbers out of
// range are refused, not clamped: the page shows the reason next to the field.
bool toText(const SettingDef& d, JsonVariantConst v, std::string& text, std::string& why) {
    if (v.isNull()) {
        if (d.secret) return text.clear(), true;
        return why = "missing value", false;
    }
    switch (d.type) {
        case SType::Bool:
            if (!v.is<bool>()) return why = "not true or false", false;
            text = v.as<bool>() ? "true" : "false";
            return true;
        case SType::Int:
        case SType::Float: {
            double x = 0;
            if (v.is<const char*>()) {
                const char* s = v.as<const char*>();
                char* end = nullptr;
                x = std::strtod(s, &end);
                if (end == s || *end) return why = "not a number", false;
            } else if (v.is<double>()) {
                x = v.as<double>();
            } else {
                return why = "not a number", false;
            }
            if (std::isnan(x)) return why = "not a number", false;
            if (d.type == SType::Int && x != std::floor(x)) return why = "not a whole number", false;
            if (x < d.min || x > d.max) {
                char b[48];
                std::snprintf(b, sizeof b, "out of range %g–%g", d.min, d.max);
                return why = b, false;
            }
            char b[32];
            std::snprintf(b, sizeof b, d.type == SType::Int ? "%.0f" : "%.6g", x);
            text = b;
            return true;
        }
        case SType::Enum: {
            if (!v.is<const char*>()) return why = "not an option", false;
            const char* s = v.as<const char*>();
            for (int k = 0; k < d.optionCount; ++k)
                if (!std::strcmp(s, d.options[k])) return text = s, true;
            return why = "not an option", false;
        }
        case SType::String:
            if (!v.is<const char*>()) return why = "not text", false;
            text = v.as<const char*>();
            if (text.size() > kMaxText) return why = "too long", false;
            return true;
    }
    return why = "unsupported", false;
}

}  // namespace

void schemaJson(const Settings& s, JsonDocument& out) { s.toJsonSchema(out); }

void valuesJson(const Settings& s, JsonDocument& out) {
    s.toJsonValues(out, false);
    for (int i = 0; i < s.count(); ++i) {
        const SettingDef& d = s.defs()[i];
        if (!d.secret) continue;
        JsonObject o = out[d.key].to<JsonObject>();
        o["set"] = !s.getString(d.key).empty();
    }
}

ApplyResult applyJson(Settings& s, JsonVariantConst in, JsonDocument& errors) {
    ApplyResult r;
    JsonObjectConst obj = in.as<JsonObjectConst>();
    if (obj.isNull()) {
        errors["_"] = "expected an object";
        return r;
    }
    struct Pending {
        const char* key;
        std::string text;
    };
    std::vector<Pending> todo;
    bool restart = false;
    for (JsonPairConst kv : obj) {
        const char* key = kv.key().c_str();
        const SettingDef* d = s.find(key);
        if (!d) {
            errors[key] = "unknown setting";
            continue;
        }
        std::string text, why;
        if (!toText(*d, kv.value(), text, why)) {
            errors[key] = why;
            continue;
        }
        if (d->secret && kv.value().is<const char*>() && text.empty()) continue;  // "" = keep it
        if (!std::strcmp(d->group, "hardware")) restart = true;
        todo.push_back({d->key, std::move(text)});
    }
    if (errors.size() > 0) return r;
    for (const Pending& p : todo) s.set(p.key, p.text.c_str());
    r.ok = true, r.restart = restart;
    return r;
}

}  // namespace svc
