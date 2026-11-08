#pragma once
// The demo frames behind the simulator's --render (the site's screenshots) and the browser build's parity test
// (tools/sim_node_test.mjs): one demo scene on one screen in one theme, drawn in the golden tests' call order
// (test/common/GoldenRender.h: fill, sceneUpdated, showNow, 75 frames of 1/30 s, no tick), so a frame matches the
// goldens and is the same on every run. As their goldens do, the Map gets 70 s of trails (8 scene updates, as
// test_map_screen's map_busy) and the Sky 100 frames (sky_night: a beacon lit). The caller sets the time zone to
// UTC first: the scenes' clocks are UTC.
#include <cstdint>
#include <cstdio>

#include "hal.h"

namespace assets {
class LogoStore;
}
namespace basemap {
class MapPack;
}

namespace app {

// False, drawing nothing, when a name is unknown. bufA and bufB hold 240 x stripH pixels each. logos and map may be
// null (no card); they must outlive the call.
bool renderDemo(const char* scene, const char* screen, const char* theme, hal::IDisplay& display, uint16_t* bufA,
                uint16_t* bufB, int stripH, assets::LogoStore* logos = nullptr, basemap::MapPack* map = nullptr);
// The names renderDemo accepts, for a usage message.
void printDemoNames(std::FILE* out);

}  // namespace app
