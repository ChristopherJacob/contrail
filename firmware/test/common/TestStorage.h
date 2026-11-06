#pragma once
// Storage for tests: files held in memory, so a test can build, truncate or corrupt a pack.
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "hal.h"

struct MemFile : hal::IFile {
    std::vector<uint8_t> bytes;
    bool fail = false;       // every read and write fails, as when the card is pulled
    bool writeFail = false;  // writes fail, reads do not
    int reads = 0, writes = 0;
    int failOnRead = 0;      // this read (the count after it) alone fails: a transient error on the card
    uint32_t size() const override { return uint32_t(bytes.size()); }
    bool readAt(uint32_t off, void* buf, uint32_t n) override {
        ++reads;
        if (fail || reads == failOnRead || off > bytes.size() || bytes.size() - off < n) return false;
        std::memcpy(buf, bytes.data() + off, n);
        return true;
    }
    bool writeAt(uint32_t off, const void* buf, uint32_t n) override {
        ++writes;
        if (fail || writeFail || off > bytes.size() || bytes.size() - off < n) return false;
        std::memcpy(bytes.data() + off, buf, n);
        return true;
    }
};

struct MemStorage : hal::IStorage {
    bool isMounted = true, isWritable = true;
    std::map<std::string, std::unique_ptr<MemFile>> files;
    MemFile& add(const std::string& path, const std::string& data) {
        auto& f = files[path];
        f = std::make_unique<MemFile>();
        f->bytes.assign(data.begin(), data.end());
        return *f;
    }
    // For binary data (a std::string holds it too, as add() does, but this says so).
    MemFile& addBytes(const std::string& path, std::vector<uint8_t> data) {
        auto& f = files[path];
        f = std::make_unique<MemFile>();
        f->bytes = std::move(data);
        return *f;
    }
    bool mounted() const override { return isMounted; }
    bool writable() const override { return isWritable; }
    hal::IFile* open(const char* path) override {
        if (!isMounted) return nullptr;
        auto it = files.find(path);
        return it == files.end() ? nullptr : it->second.get();
    }
};
