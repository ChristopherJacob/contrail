#pragma once
#include <cstdint>

#include "Scene.h"
#include "gfx/Layer.h"

namespace scr {

enum class ScreenId : uint8_t { Closest, Radar, Board, Sky, Map, Stats, Alert, Settings, Update, Count };

// What a tap asks the host to do: select a flight on every screen, and/or open a screen.
struct NavRequest {
    bool open = false;
    ScreenId to = ScreenId::Closest;
    uint32_t selectHex = 0;  // 0 = leave the selection alone
};

// A full-screen view: a stack of layers drawn back to front, fed by the scene.
class Screen {
   public:
    virtual ~Screen() = default;
    virtual ScreenId id() const = 0;
    // The scene outlives the screen's use of it; it changes in place at 5 Hz.
    virtual void setScene(const Scene* scene) = 0;
    virtual void enter() {}
    virtual void setSelection(uint32_t hex) { selected_ = hex; }
    virtual NavRequest onTap(int, int) { return {}; }
    virtual void onVerticalSwipe(int /*dir: -1 up, +1 down*/) {}
    virtual int layerCount() const = 0;
    virtual gfx::Layer* const* layers() = 0;

   protected:
    uint32_t selected_ = 0;
};

}  // namespace scr
