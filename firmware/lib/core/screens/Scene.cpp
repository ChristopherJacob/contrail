#include "Scene.h"

#include <strings.h>

#include <cstring>

namespace scr {

const SceneFlight* Scene::find(uint32_t hex) const {
    for (int i = 0; i < count; ++i)
        if (flights[i].hex == hex) return &flights[i];
    return nullptr;
}

const SceneRoute* Scene::route(const char* cs) const {
    if (!cs || !cs[0]) return nullptr;
    for (int i = 0; i < routeCount; ++i)
        if (!strcasecmp(routes[i].callsign, cs)) return &routes[i];  // callsigns ignore case
    return nullptr;
}

}  // namespace scr
