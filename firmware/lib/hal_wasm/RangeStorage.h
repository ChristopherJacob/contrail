#pragma once
// The browser simulator's "SD card" (6b spec §3): logos.pak and the place's map.pak read from the site by HTTP
// Range requests in 16 KB blocks, so a visitor downloads only what is drawn. Read-only. Free of emscripten: the
// worker's requests come in as functions (WasmGlue), the native tests pass fakes.
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "hal.h"

namespace hal {

// Reads n bytes at offset of the file at url into out; false when that failed.
using FetchRange = std::function<bool(const std::string& url, uint32_t offset, uint32_t n, void* out)>;
// The file's size, or -1 when it is missing.
using FetchSize = std::function<int64_t(const std::string& url)>;

class RangeFile : public IFile {
   public:
    static constexpr uint32_t kBlock = 16384;
    static constexpr int kBlocks = 64;  // 1 MB kept per file
    RangeFile(std::string url, uint32_t size, FetchRange fetch);
    uint32_t size() const override { return size_; }
    bool readAt(uint32_t offset, void* buf, uint32_t n) override;

   private:
    const uint8_t* block(uint32_t index);  // nullptr when the request failed
    struct Slot {
        int64_t index = -1;
        uint32_t used = 0;  // for least-recently-used replacement
        std::vector<uint8_t> data;
    };
    std::string url_;
    uint32_t size_;
    FetchRange fetch_;
    Slot slots_[kBlocks];
    uint32_t tick_ = 0;
};

class RangeStorage : public IStorage {
   public:
    // urls: card path → the site's URL ("/contrail/map.pak" → "sim/map-kfdk.pak"); an empty URL leaves that file
    // missing (no release, so no logos: the board's drawn fins).
    RangeStorage(std::map<std::string, std::string> urls, FetchSize size, FetchRange fetch);
    bool mounted() const override { return true; }
    // Opening a path that is already open returns the same file.
    IFile* open(const char* path) override;
    bool writable() const override { return false; }

   private:
    std::map<std::string, std::string> urls_;
    FetchSize size_;
    FetchRange fetch_;
    std::map<std::string, std::unique_ptr<RangeFile>> open_;
};

}  // namespace hal
