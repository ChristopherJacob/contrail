#include "Theme.h"

#include <cstring>

#include "gfx/fonts/Fonts.h"

namespace scr {

const Theme& cinematicSky() {
    static const Theme t = {
        {255, 255, 255}, {223, 229, 255}, {169, 179, 214},  // text, soft, dim
        {255, 210, 154},                                     // accent
        {139, 227, 184}, {255, 194, 154}, {169, 179, 214},  // climb, descend, level
        {13, 19, 48}, {43, 54, 112}, {20, 28, 66}, {48, 62, 140}, {26, 33, 66},  // card…rowFill
        {5, 7, 26},                                          // veil
        {0, 0, 10},                                          // shadow
        {11, 16, 41}, {7, 10, 26},                           // board background
        {{11, 42, 36}, {4, 17, 15}, {2, 8, 7}},              // radar background
        {90, 255, 170}, {191, 255, 224}, {157, 255, 208}, {141, 255, 198}, {11, 35, 30}, {31, 90, 73},
        {255, 255, 255},                                     // radar blip core
        {2, 4, 13}, {243, 245, 255}, {159, 180, 255}, {255, 107, 107}, {255, 210, 154},
        {255, 170, 90}, {255, 220, 160}, {255, 244, 214},
        {222, 230, 255}, {34, 40, 64}, {226, 232, 255}, {150, 170, 255}, {255, 80, 80}, {90, 255, 150},
        {250, 252, 255}, {255, 186, 150}, {160, 172, 214},
        {34, 42, 78}, {236, 150, 176},
        &gfx::kFontHero, &gfx::kFontTitle, &gfx::kFontBold,  // display roles: Inter
        false, false, false,                                 // gradients and glows; stripes only; sentence case
        {110, 120, 150},                                     // brand neutral
        // Map: deep blue water, slate land, a pale blue coastline, warm airports.
        {8, 16, 38}, {24, 36, 62}, {96, 140, 200}, {70, 100, 160}, {200, 214, 255}, {255, 210, 154}, {150, 170, 220},
        {255, 170, 90}, {139, 227, 184}, {190, 215, 255},    // low, mid, high planes
        {13, 19, 48}, {43, 54, 112},                         // map strip
        {58, 74, 112}, {42, 56, 90}, {44, 72, 128}, {150, 162, 200},  // map pack: roads, minor roads, rivers, towns
        // Stats (mockup 6A): type bars and the current hour in the accent, past hours periwinkle,
        // future hours faint.
        {255, 210, 154}, {91, 111, 214}, {255, 210, 154}, {34, 42, 78}, {26, 33, 66},  // bar, past, now, future, track
        {20, 28, 66}, {43, 54, 112},                                                   // card, card edge
    };
    return t;
}

const Theme& editorial() {
    // Bold Editorial (mockup 7A): flat black and greys, white type in a condensed display face,
    // airline colours as fills, a signal-yellow accent.
    static const Theme t = {
        {255, 255, 255}, {228, 228, 232}, {150, 150, 158},  // text, soft, dim
        {255, 210, 0},                                       // accent
        {120, 230, 140}, {255, 140, 90}, {170, 170, 176},    // climb, descend, level
        {14, 14, 16}, {64, 64, 70}, {51, 51, 56}, {44, 44, 50}, {24, 24, 28},  // card…rowFill (tile: bar tracks)
        {0, 0, 0},                                           // veil
        {0, 0, 0},                                           // shadow
        {10, 10, 12}, {10, 10, 12},                          // board background (flat)
        {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}},                   // radar: a black scope
        {255, 255, 255}, {220, 220, 224}, {255, 210, 0}, {255, 255, 255}, {18, 18, 20}, {70, 70, 76},
        {255, 210, 0},                                       // radar blip core
        {0, 0, 0}, {245, 245, 245}, {245, 245, 245}, {255, 60, 60}, {255, 210, 0},  // tree, moon, glow, beacon, home
        {255, 200, 80}, {255, 220, 120}, {255, 236, 160},    // sun
        {255, 255, 255}, {30, 30, 34}, {255, 255, 255}, {255, 255, 255}, {255, 70, 70}, {80, 230, 120},
        {240, 240, 240}, {255, 190, 150}, {170, 170, 176},   // contrails: day, dusk, night
        {24, 24, 28}, {200, 120, 150},                       // airglow, belt of Venus
        &gfx::kFontCondHero, &gfx::kFontCondTitle, &gfx::kFontCondLabel,
        true, true, true,                                    // flat; airline fills; capital headings
        {96, 100, 110},                                      // brand neutral
        {16, 22, 30}, {44, 48, 54}, {150, 156, 166}, {110, 110, 118}, {220, 220, 224}, {200, 200, 206}, {160, 160, 168},
        {255, 150, 70}, {120, 220, 140}, {170, 210, 255},    // (planes take airline colours here)
        {18, 18, 20}, {70, 70, 76},                          // map strip
        {84, 88, 96}, {64, 68, 74}, {52, 66, 84}, {170, 170, 176},    // map pack: roads, minor roads, rivers, towns
        // Stats: flat greys, the current hour in the accent.
        {200, 200, 206}, {150, 150, 158}, {255, 210, 0}, {50, 50, 56}, {38, 38, 42},  // bar, past, now, future, track
        {18, 18, 20}, {70, 70, 76},                                                   // card, card edge
    };
    return t;
}

const Theme& themeByName(const char* name) {
    return name && !std::strcmp(name, "editorial") ? editorial() : cinematicSky();
}

}  // namespace scr
