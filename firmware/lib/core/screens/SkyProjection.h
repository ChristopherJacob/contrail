#pragma once
#include "gfx/Canvas.h"

namespace scr {

// The sky dome as seen lying on your back: azimuthal equidistant, zenith at (cx, cy), the
// horizon on the ellipse (rx, ry). North is at the top and east on the LEFT.
struct DomeGeom {
    float cx = 120, cy = 140, rx = 112, ry = 132;
};

// Where a direction (degrees: azimuth clockwise from north, elevation) lands on the dome;
// anything below the horizon is pinned to the horizon.
gfx::Vec2 domeProject(float azDeg, float elevDeg, const DomeGeom& g = DomeGeom{});
// The same projection without the clamp: below the horizon lands outside the ellipse.
gfx::Vec2 domeProjectRaw(float azDeg, float elevDeg, const DomeGeom& g = DomeGeom{});
// Unit screen vector at the first direction pointing along the great circle toward the
// second (the way the moon's lit limb faces the sun). Points straight up if they coincide.
gfx::Vec2 domeDirectionToward(float azDeg, float elevDeg, float toAzDeg, float toElevDeg,
                              const DomeGeom& g = DomeGeom{});

}  // namespace scr
