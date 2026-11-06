#include "Settings.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace svc {

Settings::Settings(hal::IKeyValueStore& store, const SettingDef* defs, int n)
    : store_(store), defs_(defs), n_(n), values_(n) {
    for (int i = 0; i < n_; ++i) {
        values_[i].num = defs_[i].defNum;
        values_[i].str = defs_[i].defStr ? defs_[i].defStr : "";
    }
}

int Settings::indexOf(const char* key) const {
    for (int i = 0; i < n_; ++i)
        if (!std::strcmp(defs_[i].key, key)) return i;
    return -1;
}

bool Settings::parse(const SettingDef& d, const char* text, Value& out) const {
    if (!text) return false;
    switch (d.type) {
        case SType::String:
            out.str = text;
            return true;
        case SType::Bool:
            if (!std::strcmp(text, "true") || !std::strcmp(text, "1") || !std::strcmp(text, "on")) return out.num = 1, true;
            if (!std::strcmp(text, "false") || !std::strcmp(text, "0") || !std::strcmp(text, "off")) return out.num = 0, true;
            return false;
        case SType::Enum:
            for (int i = 0; i < d.optionCount; ++i)
                if (!std::strcmp(text, d.options[i])) return out.num = i, true;
            return false;
        case SType::Int:
        case SType::Float: {
            char* end = nullptr;
            double v = std::strtod(text, &end);
            if (end == text || *end || std::isnan(v)) return false;
            if (d.type == SType::Int) v = std::round(v);
            out.num = v < d.min ? d.min : v > d.max ? d.max : v;
            return true;
        }
    }
    return false;
}

std::string Settings::format(const SettingDef& d, const Value& v) const {
    char buf[32];
    switch (d.type) {
        case SType::String:
            return v.str;
        case SType::Bool:
            return v.num ? "true" : "false";
        case SType::Enum:
            return d.options[int(v.num)];
        case SType::Int:
            std::snprintf(buf, sizeof buf, "%d", int(v.num));
            return buf;
        case SType::Float:
            std::snprintf(buf, sizeof buf, "%.6g", v.num);
            return buf;
    }
    return "";
}

void Settings::load() {
    LockGuard g(mu_);
    char buf[256];
    for (int i = 0; i < n_; ++i) {
        Value v;
        v.num = defs_[i].defNum;
        v.str = defs_[i].defStr ? defs_[i].defStr : "";
        if (store_.getString(defs_[i].key, buf, sizeof buf)) parse(defs_[i], buf, v);
        values_[i] = v;
    }
}

int Settings::getInt(const char* key) const {
    LockGuard g(mu_);
    int i = indexOf(key);
    return i < 0 ? 0 : int(values_[i].num);
}

float Settings::getFloat(const char* key) const {
    LockGuard g(mu_);
    int i = indexOf(key);
    return i < 0 ? 0.f : float(values_[i].num);
}

bool Settings::getBool(const char* key) const { return getInt(key) != 0; }

std::string Settings::getString(const char* key) const {
    LockGuard g(mu_);
    int i = indexOf(key);
    return i < 0 ? std::string() : values_[i].str;
}

std::string Settings::getEnum(const char* key) const {
    LockGuard g(mu_);
    int i = indexOf(key);
    return i < 0 || defs_[i].type != SType::Enum ? std::string() : std::string(defs_[i].options[int(values_[i].num)]);
}

bool Settings::set(const char* key, const char* value) {
    {
        LockGuard g(mu_);
        int i = indexOf(key);
        if (i < 0) return false;
        Value v = values_[i];
        if (!parse(defs_[i], value, v)) return false;
        values_[i] = v;
        store_.putString(key, format(defs_[i], v).c_str());
    }
    for (auto& l : listeners_) l(key);
    return true;
}

void Settings::toJsonSchema(JsonDocument& doc) const {
    static const char* const kTypes[] = {"int", "float", "bool", "string", "enum"};
    JsonArray arr = doc["settings"].to<JsonArray>();
    for (int i = 0; i < n_; ++i) {
        const SettingDef& d = defs_[i];
        JsonObject o = arr.add<JsonObject>();
        o["key"] = d.key;
        o["type"] = kTypes[int(d.type)];
        o["group"] = d.group;
        o["label"] = d.label;
        if (d.type == SType::Int || d.type == SType::Float) o["min"] = d.min, o["max"] = d.max;
        if (d.type == SType::Enum) {
            JsonArray opts = o["options"].to<JsonArray>();
            for (int k = 0; k < d.optionCount; ++k) opts.add(d.options[k]);
        }
        if (d.secret) o["secret"] = true;
        else {
            Value def{d.defNum, d.defStr ? d.defStr : ""};
            o["default"] = format(d, def);
        }
    }
}

void Settings::toJsonValues(JsonDocument& doc, bool includeSecrets) const {
    LockGuard g(mu_);
    for (int i = 0; i < n_; ++i) {
        const SettingDef& d = defs_[i];
        if (d.secret && !includeSecrets) {
            doc[d.key] = values_[i].str.empty() ? "" : "••••";
            continue;
        }
        switch (d.type) {
            case SType::Int: doc[d.key] = int(values_[i].num); break;
            case SType::Float: doc[d.key] = values_[i].num; break;
            case SType::Bool: doc[d.key] = values_[i].num != 0; break;
            default: doc[d.key] = format(d, values_[i]); break;
        }
    }
}

}  // namespace svc
