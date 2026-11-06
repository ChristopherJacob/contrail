#pragma once
#include <SDL.h>

#include "hal.h"

namespace hal {

// 240x320 window (scaled) that shows strips as they are pushed. Also acts as the
// touch device: the left mouse button is the finger.
class SdlDisplay : public IDisplay, public ITouch {
   public:
    explicit SdlDisplay(int scale = 2, int w = 240, int h = 320);
    ~SdlDisplay() override;

    int width() const override { return w_; }
    int height() const override { return h_; }
    void pushRect(int x, int y, int w, int h, const uint16_t* px) override;
    void endFrame() override;

    bool read(int& x, int& y) override;
    // Pump SDL events; returns false once the window is closed. Main thread only.
    bool pumpEvents();

   private:
    int w_, h_, scale_;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    bool down_ = false;
    int mx_ = 0, my_ = 0;
};

}  // namespace hal
