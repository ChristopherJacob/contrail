#pragma once
#include <cstdint>

#include "Layer.h"
#include "hal.h"

namespace gfx {

// Renders layers strip by strip into two alternating buffers so the CPU draws one
// strip while the display HAL transfers the previous one. Only damaged strips are drawn,
// and within a strip only the damaged column ranges (a few blocks per strip at most), each
// over just the rows its damage covers.
class StripRenderer {
   public:
    StripRenderer(hal::IDisplay& display, uint16_t* bufA, uint16_t* bufB, int stripH);

    void setLayers(Layer* const* layers, int n);
    void invalidateAll() { forceFull_ = true; }
    // Returns the number of strips pushed.
    int frame(float dt);

    float fps() const { return fps_; }  // frame() calls per second, whether or not they drew
    uint32_t lastRenderUs() const { return lastRenderUs_; }
    void setMicrosSource(uint32_t (*micros)()) { micros_ = micros; }
    // Profiling for the last frame (needs a micros source): strips drawn, time spent in
    // each layer's draw() and time waiting for the display transfer.
    static constexpr int kMaxLayers = 8;
    // Column ranges per strip: ranges closer than kMinSpanGap pixels are drawn as one block.
    static constexpr int kMaxSpans = 4, kMinSpanGap = 16;
    int lastStrips() const { return lastStrips_; }  // blocks pushed (a strip may push several)
    uint32_t layerUs(int i) const { return i < kMaxLayers ? layerUs_[i] : 0; }
    uint32_t waitUs() const { return waitUs_; }

   private:
    hal::IDisplay& display_;
    uint16_t* bufs_[2];
    int stripH_;
    Layer* const* layers_ = nullptr;
    int nLayers_ = 0;
    DamageList damage_;
    bool forceFull_ = true;
    float fps_ = 0;
    uint32_t lastRenderUs_ = 0;
    uint32_t (*micros_)() = nullptr;
    int lastStrips_ = 0;
    uint32_t layerUs_[kMaxLayers] = {};
    uint32_t waitUs_ = 0;
};

}  // namespace gfx
