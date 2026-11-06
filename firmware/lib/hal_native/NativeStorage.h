#pragma once
// The desktop's "SD card": a folder (CONTRAIL_SD, default sim_sd/), so the sim reads the same packs.
#include <cstdio>
#include <memory>
#include <string>

#include "hal.h"

namespace hal {

class NativeFile : public IFile {
   public:
    // writable: opened "r+b", so writeAt can work.
    NativeFile(std::FILE* f, bool writable);
    ~NativeFile() override;
    NativeFile(const NativeFile&) = delete;
    NativeFile& operator=(const NativeFile&) = delete;
    uint32_t size() const override { return size_; }
    bool readAt(uint32_t offset, void* buf, uint32_t n) override;
    bool writeAt(uint32_t offset, const void* buf, uint32_t n) override;

   private:
    std::FILE* f_;
    uint32_t size_ = 0;
    bool writable_ = false;
};

class NativeStorage : public IStorage {
   public:
    explicit NativeStorage(std::string dir);
    bool mounted() const override { return mounted_; }
    // Opening a path that is already open returns the same file.
    IFile* open(const char* path) override;

   private:
    static constexpr int kMaxFiles = 2;
    struct Slot {
        std::string path;
        std::unique_ptr<NativeFile> file;
    };
    std::string dir_;
    bool mounted_ = false;
    Slot slots_[kMaxFiles];
};

}  // namespace hal
