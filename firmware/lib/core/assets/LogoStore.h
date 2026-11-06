#pragma once
// Airline logos from /contrail/logos.pak (5a spec §3, §7): an ICAO code to a palette badge at one of the
// pack's three sizes, read into memory the caller provides (the screen arena). Render thread only, except
// the status getters, which the web task may read.
#include <atomic>

#include "Pack.h"
#include "gfx/Badge.h"
#include "hal.h"

namespace assets {

class LogoStore {
   public:
    static constexpr const char* kPath = "/contrail/logos.pak";
    // Opens the pack when the storage is mounted. Safe to call with nullptr (no storage on this platform).
    // Starts over: a read failure or a logged check failure from before is forgotten.
    void begin(hal::IStorage* storage);

    // The logo for `icao` at the store's size, or false (out.badge left empty): no pack, no such airline,
    // or the item failed its bounds or CRC check. This is the single place SD-card headers are checked
    // against the store sizes (drawBadge trusts a Badge).
    bool load(const char* icao, gfx::Badge40& out) { return loadInto(icao, out); }
    bool load(const char* icao, gfx::Badge30& out) { return loadInto(icao, out); }
    bool load(const char* icao, gfx::Badge25& out) { return loadInto(icao, out); }

    bool mounted() const { return mounted_; }
    // Logos in the pack, or -1 when none is open.
    int logoCount() const { return pack_.isOpen() && !failed_ ? int(pack_.count()) : -1; }
    // Why there are no logos ("no logos.pak", "logos.pak is a newer version; update the firmware", …), or ""
    // when the pack is open or there is no card. Always a static literal, so the web task may read it at any
    // time without a lock.
    const char* error() const { return status_.load(); }

   private:
    template <class Store>
    bool loadInto(const char* icao, Store& out) {
        out.badge = gfx::Badge{};
        return loadSize(icao, Store::kSide, Store::kBpp, Store::kMaxColours, out.pal, out.alpha, out.px,
                        sizeof out.px, out.badge);
    }
    bool loadSize(const char* icao, int side, int bpp, int maxColours, uint16_t* pal, uint8_t* alpha, uint8_t* px,
                  size_t pxBytes, gfx::Badge& badge);
    void readFailed();
    void checkFailed(const char* icao);

    Pack pack_;
    bool mounted_ = false;
    std::atomic<bool> failed_{false};  // a read failed (card pulled): no more reads until restart
    std::atomic<bool> checkLogged_{false};  // a CRC or bounds failure has been logged (spec §7: once)
    std::atomic<const char*> status_{""};  // a static literal
};

}  // namespace assets
