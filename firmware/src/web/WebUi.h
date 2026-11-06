#pragma once
// The settings page and its JSON API at http://contrail.local (config spec §2), served from its own
// task on core 0, and firmware upload into the spare OTA slot (§6). No password (the home network
// is trusted); both POSTs need an X-Contrail header so another website can't send them.
#include <ArduinoJson.h>

#include <functional>

#include "services/Settings.h"

namespace web {

struct Hooks {
    std::function<void(JsonDocument&)> info;          // fills /api/info
    std::function<void(int percent)> updateProgress;  // 0..100 while an upload runs, -1 when it ends or fails
    std::function<void()> restart;                    // after a reply that needs a restart
    std::function<void()> idle;                       // every pass of the task (saves panel edits)
};

void start(svc::Settings& settings, const Hooks& hooks);
// The web task's stack high-water mark in bytes (never used since boot); 0 before start().
unsigned stackFree();

}  // namespace web
