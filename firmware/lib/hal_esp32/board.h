#pragma once
// The board this firmware is built for: one CONTRAIL_BOARD_* define per PlatformIO env.
#if defined(CONTRAIL_BOARD_WS28)
#include "board_ws28.h"
#else
#include "board_cyd.h"
#endif
