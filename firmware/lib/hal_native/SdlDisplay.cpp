#include "SdlDisplay.h"

namespace hal {

SdlDisplay::SdlDisplay(int scale, int w, int h) : w_(w), h_(h), scale_(scale) {
    SDL_Init(SDL_INIT_VIDEO);
    window_ = SDL_CreateWindow("Contrail", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w * scale, h * scale, 0);
    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED);
    SDL_RenderSetLogicalSize(renderer_, w, h);
    texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, w, h);
}

SdlDisplay::~SdlDisplay() {
    SDL_DestroyTexture(texture_);
    SDL_DestroyRenderer(renderer_);
    SDL_DestroyWindow(window_);
    SDL_Quit();
}

void SdlDisplay::pushRect(int x, int y, int w, int h, const uint16_t* px) {
    SDL_Rect r{x, y, w, h};
    SDL_UpdateTexture(texture_, &r, px, w * 2);
}

void SdlDisplay::endFrame() {
    SDL_RenderClear(renderer_);
    SDL_RenderCopy(renderer_, texture_, nullptr, nullptr);
    SDL_RenderPresent(renderer_);
}

bool SdlDisplay::read(int& x, int& y) {
    x = mx_, y = my_;
    return down_;
}

bool SdlDisplay::pumpEvents() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_QUIT:
                return false;
            case SDL_MOUSEBUTTONDOWN:
                if (e.button.button == SDL_BUTTON_LEFT) down_ = true;
                break;
            case SDL_MOUSEBUTTONUP:
                if (e.button.button == SDL_BUTTON_LEFT) down_ = false;
                break;
            default:
                break;
        }
        if (e.type == SDL_MOUSEMOTION || e.type == SDL_MOUSEBUTTONDOWN) {
            int x = e.type == SDL_MOUSEMOTION ? e.motion.x : e.button.x;
            int y = e.type == SDL_MOUSEMOTION ? e.motion.y : e.button.y;
            mx_ = x, my_ = y;  // logical size already maps window coords to panel coords
        }
    }
    return true;
}

}  // namespace hal
