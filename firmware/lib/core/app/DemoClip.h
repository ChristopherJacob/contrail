#pragma once
// The site's animated previews (6d): a screen's loop, or a tour of the screens, frame by frame at 30 fps. Drawn on the
// demo scenes without a clock, alert engine, director or network, so a clip is the same on every run; flights can run
// at 10x (the Map, the Board, the tour). localtime must give UTC, as for renderDemo. Desktop only in practice:
// a crossfading clip holds 2.3 MB.
#include <cstdint>
#include <functional>

namespace app {

// Called with each finished 240 x 320 RGB565 frame, in order.
using ClipSink = std::function<void(const uint16_t* px)>;

// The frame count, or -1 (no frames) for an unknown name or Stats, which has no motion. Screens: closest radar sky alert
// map board. bufA and bufB hold 240 x stripH pixels each.
int renderClip(const char* screen, const char* theme, const ClipSink& sink, uint16_t* bufA, uint16_t* bufB, int stripH);
// Every screen in turn with the firmware's own fades, flights at 10x. Starts and ends on the solid veil colour, so it loops
// inside a fade. The frame count, or -1 for an unknown theme.
int renderTour(const char* theme, const ClipSink& sink, uint16_t* bufA, uint16_t* bufB, int stripH);

}  // namespace app
