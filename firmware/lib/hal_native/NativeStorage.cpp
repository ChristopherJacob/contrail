#include "NativeStorage.h"

#include <sys/stat.h>

namespace hal {

NativeFile::NativeFile(std::FILE* f, bool writable) : f_(f), writable_(writable) {
    std::fseek(f_, 0, SEEK_END);
    const long end = std::ftell(f_);
    // A failed ftell (-1) reads as an empty file rather than a 4 GB one.
    size_ = end < 0 ? 0 : uint32_t(end);
}

NativeFile::~NativeFile() { std::fclose(f_); }

bool NativeFile::readAt(uint32_t offset, void* buf, uint32_t n) {
    if (offset > size_ || size_ - offset < n) return false;
    return std::fseek(f_, long(offset), SEEK_SET) == 0 && std::fread(buf, 1, n, f_) == n;
}

bool NativeFile::writeAt(uint32_t offset, const void* buf, uint32_t n) {
    if (!writable_ || offset > size_ || size_ - offset < n) return false;
    if (std::fseek(f_, long(offset), SEEK_SET) != 0) return false;
    if (std::fwrite(buf, 1, n, f_) != n) return false;
    return std::fflush(f_) == 0;
}

NativeStorage::NativeStorage(std::string dir) : dir_(std::move(dir)) {
    struct stat st;
    mounted_ = ::stat(dir_.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

IFile* NativeStorage::open(const char* path) {
    if (!mounted_ || !path) return nullptr;
    for (auto& s : slots_)
        if (s.file && s.path == path) return s.file.get();
    for (auto& s : slots_) {
        if (s.file) continue;
        const std::string full = dir_ + path;
        struct stat st;
        // Only regular files: fopen succeeds on a directory on POSIX, and its reads then fail confusingly.
        if (::stat(full.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return nullptr;
        // Read-write, for the saved chart in map.pak; a read-only file or folder opens read-only.
        std::FILE* f = std::fopen(full.c_str(), "r+b");
        const bool writable = f != nullptr;
        if (!f) f = std::fopen(full.c_str(), "rb");
        if (!f) return nullptr;
        s.path = path;
        s.file = std::make_unique<NativeFile>(f, writable);
        return s.file.get();
    }
    return nullptr;
}

}  // namespace hal
