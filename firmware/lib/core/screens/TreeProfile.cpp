// Builds the treeline silhouette (see TreeProfile.h). Not called by the firmware, which
// uses the generated table; tools/gen_trees.py runs it on the host and test_sky checks the
// committed table still matches it. The linker drops it from device builds.
#include "TreeProfile.h"

#include <cmath>
#include <cstring>

#include "SkyViewScreen.h"
#include "gfx/Math.h"

namespace scr {
using namespace trees;

namespace {
constexpr float k2Pi = 2 * gfx::kPi;
float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
}  // namespace

void buildTreeProfile(TreeProfile& out) {
    constexpr float kRx = SkyTrees::kRx, kRy = SkyTrees::kRy;
    // Build the silhouette in pixels along the rim: a low canopy of brush, with spruces and
    // rounder broadleaf crowns standing up from it, taller on some sides than others. Heights go
    // in out.scale (1/8 px) while building, then become radii.
    uint8_t* h8 = out.scale;
    std::memset(h8, 0, kBins);
    const float binRad = k2Pi / kBins;
    auto pxPerRad = [](float th) {
        float s = std::sin(th), c = std::cos(th);
        return 0.9f * std::sqrt(kRx * kRx * s * s + kRy * kRy * c * c);
    };
    auto raise = [&](int bin, float hPx) {
        bin = ((bin % kBins) + kBins) % kBins;
        int v = int(std::lround(clampf(hPx, 0, 31.8f) * 8));
        if (v > h8[bin]) h8[bin] = uint8_t(v);
    };
    uint32_t seed = 0x74EE5u;
    auto rnd = [&seed] {
        seed = seed * 1664525u + 1013904223u;
        return float(seed >> 8) / 16777216.f;
    };
    float th = 0;
    while (th < k2Pi) {
        float region = 0.9f + 0.14f * std::sin(th * 2 + 0.6f) + 0.08f * std::sin(th * 5 + 2.1f);
        float ppr = pxPerRad(th);
        float kind = rnd();
        if (kind < 0.05f) {  // a gap in the trees: only brush
            th += (6 + rnd() * 10) / ppr;
            continue;
        }
        if (kind < 0.9f) {
            // Spruce: a slim spire whose sides step out in drooping tiers.
            float H = (7.f + rnd() * 10.f) * region, W = H * (0.27f + rnd() * 0.1f);
            int tiers = 4 + int(rnd() * 3);
            float lean = (rnd() - 0.5f) * 0.1f;
            int c0 = int(th / binRad), half = int(W / (ppr * binRad)) + 2;
            for (int b = -half; b <= half; ++b) {
                float dx = b * binRad * ppr;
                for (float y = 0; y <= H; y += 0.2f) {
                    float f = y / H;
                    float w = W * f * (0.62f + 0.38f * (tiers * f - std::floor(tiers * f)));
                    if (std::fabs(dx - lean * y) <= w) {
                        raise(c0 + b, H - y);
                        break;
                    }
                }
            }
            th += W * (1.f + rnd() * 0.8f) / ppr;
        } else {
            // Broadleaf: a cluster of three round lobes.
            float H = (6.f + rnd() * 5.f) * region, W = 5.f + rnd() * 3.f;
            int c0 = int(th / binRad), half = int(W * 1.3f / (ppr * binRad)) + 2;
            float lx[3] = {-0.5f * W, 0, 0.5f * W}, lr[3], ltop[3];
            for (int i = 0; i < 3; ++i) {
                lr[i] = W * (0.5f + rnd() * 0.25f);
                ltop[i] = H * (i == 1 ? 1.f : 0.7f + rnd() * 0.25f);
            }
            for (int b = -half; b <= half; ++b) {
                float dx = b * binRad * ppr, h = 0;
                for (int i = 0; i < 3; ++i) {
                    float q = lr[i] * lr[i] - (dx - lx[i]) * (dx - lx[i]);
                    if (q > 0) h = std::fmax(h, ltop[i] - lr[i] + std::sqrt(q));
                }
                raise(c0 + b, h);
            }
            th += W * (1.f + rnd() * 0.6f) / ppr;
        }
    }
    // Heights to radii: the brush line sits at the dome's horizon, gently uneven.
    float rhoMin = 1;
    for (int k = 0; k < kBins; ++k) {
        float t = k * binRad, s = std::sin(t), c = std::cos(t);
        float radialPx = std::sqrt(kRx * kRx * c * c + kRy * kRy * s * s);
        float brush = 1.2f + 0.5f * std::sin(t * 7 + 0.4f) + 0.4f * std::sin(t * 19 + 1.3f);
        // 0.968 of the tree ellipse is about the dome's horizon (112/116 across, 132/136 up and down).
        float rho = 0.968f - (h8[k] / 8.f + brush) / radialPx;
        int q = int(std::lround((rho - kRhoLo) / kRhoStep));
        out.rho[k] = uint8_t(q < 0 ? 0 : q > 255 ? 255 : q);
    }
    // Anti-aliasing: pixels across the edge per unit of radius, from the profile's slope.
    for (int k = 0; k < kBins; ++k) {
        float t = k * binRad, s = std::sin(t), c = std::cos(t);
        float rho = kRhoLo + out.rho[k] * kRhoStep;
        float slope = (float(out.rho[(k + 1) % kBins]) - float(out.rho[(k + kBins - 1) % kBins])) * kRhoStep / (2 * binRad);
        float radialPx = std::sqrt(kRx * kRx * c * c + kRy * kRy * s * s);
        float tangPx = rho * std::sqrt(kRx * kRx * s * s + kRy * kRy * c * c);
        float g = std::sqrt(1 / (radialPx * radialPx) + slope * slope / (tangPx * tangPx));
        out.scale[k] = uint8_t(clampf(1 / g, kMinScale, 255));
        rhoMin = std::fmin(rhoMin, rho);
    }
    out.rhoMin = rhoMin;
}

}  // namespace scr
