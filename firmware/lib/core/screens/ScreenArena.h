#pragma once
// One scratch buffer the screens share (spec §2). Only the active screen updates and draws (the
// host dips through the veil to switch), so the screen that entered last owns it; the others
// must not touch it until they enter again, and every screen rebuilds what it keeps here in
// enter(). The largest user (the Live Map) sets the size; each user static_asserts its fit.
#include <cassert>
#include <cstddef>
#include <new>
#include <type_traits>

// Native test builds check on every update and draw that a screen only touches the arena while
// it owns it; the device build compiles the check (and the owner it needs) out.
#if defined(CONTRAIL_TEST) || defined(CONTRAIL_ARENA_CHECKS)
#define CONTRAIL_ARENA_CHECKS_ON 1
#endif

namespace scr {

class ScreenArena {
   public:
    static constexpr size_t kBytes = 4096;
    // A freshly constructed T for `owner`, ending any earlier owner's claim.
    template <class T>
    T* claim(const void* owner) {
        static_assert(sizeof(T) <= kBytes, "ScreenArena::kBytes is too small for this screen's scratch");
        static_assert(alignof(T) <= alignof(std::max_align_t), "over-aligned scratch");
        static_assert(std::is_trivially_destructible<T>::value, "the arena never runs destructors");
        owner_ = owner;
        return new (buf_) T();
    }
    bool owns(const void* owner) const { return owner_ == owner; }

   private:
    alignas(std::max_align_t) unsigned char buf_[kBytes];
    const void* owner_ = nullptr;
};

// A fixed-size array living in the arena. It indexes and range-fors like the member array it
// replaces, and is empty (no elements) until its screen attaches it.
template <class T, int N>
class ArenaArray {
   public:
    void attach(T* p) { p_ = p; }
    // Attach scratch that `owner` claimed from `a`, so check() can tell when the claim has passed on.
    void attach(T* p, const ScreenArena& a, const void* owner) {
        p_ = p;
#ifdef CONTRAIL_ARENA_CHECKS_ON
        arena_ = &a, owner_ = owner;
#else
        (void)a, (void)owner;
#endif
    }
    // Asserts (in checked builds) that the scratch is still this owner's: only the screen that
    // entered last may use the arena.
    void check() const {
#ifdef CONTRAIL_ARENA_CHECKS_ON
        assert(!arena_ || arena_->owns(owner_));
#endif
    }
    T* data() const { return p_; }
    T& operator[](int i) const { return p_[i]; }
    T* begin() const { return p_; }
    T* end() const { return p_ ? p_ + N : p_; }
    explicit operator bool() const { return p_ != nullptr; }

   private:
    T* p_ = nullptr;
#ifdef CONTRAIL_ARENA_CHECKS_ON
    const ScreenArena* arena_ = nullptr;
    const void* owner_ = nullptr;
#endif
};

}  // namespace scr
