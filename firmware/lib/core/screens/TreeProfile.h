#pragma once
// The treeline's silhouette round the Sky View rim: a radius and an anti-aliasing scale for
// each of 1024 angles. It never changes, so it lives in flash as a generated const table
// (TreeProfileData.cpp, written by tools/gen_trees.py) instead of 2 KB of RAM built at boot.
// buildTreeProfile() is the generator's source of truth; test_sky checks the table matches it.
#include <cstdint>

namespace scr {
namespace trees {
constexpr int kBins = 1024;
// rho is stored as kRhoLo + rho * kRhoStep (a fraction of the tree ellipse's radius).
constexpr float kRhoLo = 0.76f, kRhoStep = (0.97f - kRhoLo) / 255.f;
constexpr int kMinScale = 16;  // keeps the anti-aliased edge within a known distance of the profile
}  // namespace trees

struct TreeProfile {
    uint8_t rho[trees::kBins];    // silhouette radius (see trees::kRhoLo)
    uint8_t scale[trees::kBins];  // pixels per unit of radius across the silhouette edge (anti-aliasing)
    float rhoMin;                 // the lowest tip, as a radius
};

extern const TreeProfile kTreeProfile;
void buildTreeProfile(TreeProfile& out);

}  // namespace scr
