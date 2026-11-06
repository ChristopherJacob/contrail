#pragma once
// Test helpers: a display that assembles pushed strips into a frame, and golden-image
// comparison against binary PPMs in test/golden. Run with UPDATE_GOLDEN=1 to rewrite.
#include <unity.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "gfx/Color.h"
#include "hal.h"

class FrameCapture : public hal::IDisplay {
   public:
    FrameCapture(int w = 240, int h = 320) : w_(w), h_(h), px(w * h, 0) {}
    int width() const override { return w_; }
    int height() const override { return h_; }
    void pushRect(int x, int y, int w, int h, const uint16_t* p) override {
        pushedRects.push_back({x, y, w, h});
        for (int r = 0; r < h; ++r) std::copy(p + r * w, p + (r + 1) * w, px.begin() + (y + r) * w_ + x);
    }
    struct Pushed {
        int x, y, w, h;
    };
    int w_, h_;
    std::vector<uint16_t> px;
    std::vector<Pushed> pushedRects;
};

inline bool writePpm(const std::string& path, const std::vector<uint16_t>& px, int w, int h) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (uint16_t v : px) {
        gfx::Rgb c = gfx::unpack565(v);
        std::fputc(c.r, f), std::fputc(c.g, f), std::fputc(c.b, f);
    }
    std::fclose(f);
    return true;
}

inline bool readPpm(const std::string& path, std::vector<gfx::Rgb>& out, int& w, int& h) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    int maxv;
    if (std::fscanf(f, "P6 %d %d %d", &w, &h, &maxv) != 3) return std::fclose(f), false;
    std::fgetc(f);
    out.resize(w * h);
    for (auto& c : out) c = {uint8_t(std::fgetc(f)), uint8_t(std::fgetc(f)), uint8_t(std::fgetc(f))};
    std::fclose(f);
    return true;
}

// Compare a frame with GOLDEN_DIR/<name>.ppm. Each channel may differ by `tol` (8-bit units).
inline void assertGolden(const char* name, const std::vector<uint16_t>& px, int w, int h, int tol = 8) {
    std::string path = std::string(GOLDEN_DIR) + "/" + name + ".ppm";
    if (std::getenv("UPDATE_GOLDEN")) {
        TEST_ASSERT_TRUE_MESSAGE(writePpm(path, px, w, h), "could not write golden");
        return;
    }
    std::vector<gfx::Rgb> gold;
    int gw, gh;
    std::string missing = "missing golden " + path + " (run with UPDATE_GOLDEN=1)";
    TEST_ASSERT_TRUE_MESSAGE(readPpm(path, gold, gw, gh), missing.c_str());
    TEST_ASSERT_EQUAL(w, gw);
    TEST_ASSERT_EQUAL(h, gh);
    int bad = 0;
    for (size_t i = 0; i < px.size(); ++i) {
        gfx::Rgb c = gfx::unpack565(px[i]);
        if (std::abs(c.r - gold[i].r) > tol || std::abs(c.g - gold[i].g) > tol || std::abs(c.b - gold[i].b) > tol)
            ++bad;
    }
    if (bad) {
        writePpm(std::string(GOLDEN_DIR) + "/" + name + ".actual.ppm", px, w, h);
        std::string msg = std::string(name) + ": " + std::to_string(bad) + " pixels differ (see .actual.ppm)";
        TEST_FAIL_MESSAGE(msg.c_str());
    }
}
