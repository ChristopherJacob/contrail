#include "AircraftStore.h"

#include <algorithm>

#include "Geo.h"

namespace model {

void deadReckon(Aircraft& a, float seconds) {
    if (seconds <= 0 || a.gsKt <= 0 || a.has(AC_GROUND)) return;
    geo::projectFast(a.lat, a.lon, a.gsKt * seconds / 3600.f, a.trackDeg, a.lat, a.lon);
}

void AircraftStore::configure(double homeLat, double homeLon, float radiusNm) {
    svc::LockGuard g(mu_);
    lat_ = homeLat, lon_ = homeLon, radius_ = radiusNm;
    frame_.set(homeLat, homeLon);
    n_ = 0;
}

void AircraftStore::derive(Aircraft& a) const {
    frame_.distBearing(a.lat, a.lon, a.distNm, a.bearingDeg);
    a.elevDeg = geo::elevationDegFast(a.distNm, float(a.altFt));
}

void AircraftStore::beginUpdate(uint32_t nowMs) {
    mu_.lock();
    updateMs_ = nowMs;
}

bool AircraftStore::upsert(const Aircraft& in) {
    if (!(in.flags & AC_HAS_POS)) return false;
    if ((in.flags & AC_GROUND) && !keepGround_) return false;
    Aircraft a = in;
    derive(a);
    if (a.distNm > radius_) return false;
    for (int i = 0; i < n_; ++i) {
        if (items_[i].hex == a.hex) {
            items_[i] = a;
            return true;
        }
    }
    if (n_ < kCapacity) {
        items_[n_++] = a;
        return true;
    }
    int far = 0;
    for (int i = 1; i < n_; ++i)
        if (items_[i].distNm > items_[far].distNm) far = i;
    if (items_[far].distNm <= a.distNm) return false;
    items_[far] = a;
    return true;
}

void AircraftStore::endUpdate() {
    int w = 0;
    for (int i = 0; i < n_; ++i)
        if (updateMs_ - items_[i].lastSeenMs <= kStaleMs) items_[w++] = items_[i];
    n_ = w;
    lastUpdateMs_ = updateMs_;
    ++generation_;
    mu_.unlock();
}

int AircraftStore::snapshot(Aircraft* out, int max, uint32_t nowMs, uint32_t* generation) const {
    int n;
    {
        svc::LockGuard g(mu_);
        if (generation) *generation = generation_;
        n = std::min(n_, max);
        // Pick the nearest `n` by index so the copy stays small and the lock short.
        uint8_t idx[kCapacity];
        for (int i = 0; i < n_; ++i) idx[i] = uint8_t(i);
        std::partial_sort(idx, idx + n, idx + n_,
                          [this](uint8_t a, uint8_t b) { return items_[a].distNm < items_[b].distNm; });
        for (int i = 0; i < n; ++i) out[i] = items_[idx[i]];
    }
    for (int i = 0; i < n; ++i) {
        float age = (nowMs - out[i].lastSeenMs) / 1000.f;
        deadReckon(out[i], std::min(age, kMaxReckonS));
        derive(out[i]);
    }
    std::sort(out, out + n, [](const Aircraft& a, const Aircraft& b) { return a.distNm < b.distNm; });
    return n;
}

int AircraftStore::size() const {
    svc::LockGuard g(mu_);
    return n_;
}

uint32_t AircraftStore::lastUpdateMs() const {
    svc::LockGuard g(mu_);
    return lastUpdateMs_;
}

uint32_t AircraftStore::generation() const {
    svc::LockGuard g(mu_);
    return generation_;
}

void AircraftStore::clear() {
    svc::LockGuard g(mu_);
    n_ = 0;
}

}  // namespace model
