#pragma once
// Full-screen message over the sky: used for Wi-Fi setup, safe mode and boot status.
#include <cstring>

#include "SkyLayer.h"
#include "gfx/Layer.h"

namespace app {

class MessageLayer : public gfx::Layer {
   public:
    void set(const char* title, const char* line1, const char* line2 = "", const char* line3 = "");
    void update(float dt, gfx::DamageList& damage) override;
    void draw(gfx::Canvas& c) override;

   private:
    char title_[24] = "", l1_[40] = "", l2_[40] = "", l3_[40] = "";
    bool dirty_ = true;
};

// Sky + message, ready to hand to a StripRenderer.
struct MessageScreen {
    SkyLayer sky;
    MessageLayer msg;
    gfx::Layer* layers[2] = {&sky, &msg};
};

}  // namespace app
