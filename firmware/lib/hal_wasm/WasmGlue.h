#pragma once
// The worker's side of the browser build's HAL (site/sim-worker.js), emscripten only: synchronous XHR for the
// site's files (allowed in a Web Worker), and the frame, the LED and log lines handed to the worker's callbacks
// (Module.onFrame, onLed, onLog).
#include <cstdint>
#include <string>

#include "Replay.h"

namespace hal::wasm {

Fetched fetchFile(const std::string& path);
int64_t fileSize(const std::string& url);
bool fetchRange(const std::string& url, uint32_t offset, uint32_t n, void* out);
void postFrame(const uint16_t* px);  // 240 x 320 RGB565, rows packed
void postLed(uint8_t r, uint8_t g, uint8_t b);
void postLog(const char* line);
uint32_t nowMs();
uint32_t nowUs();

}  // namespace hal::wasm
