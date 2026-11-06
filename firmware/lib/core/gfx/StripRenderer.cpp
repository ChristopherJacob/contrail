#include "StripRenderer.h"

#include <algorithm>

namespace gfx {

namespace {
long area(const Rect& r) { return long(r.w) * r.h; }
long waste(const Rect& a, const Rect& b) { return area(a.unite(b)) - area(a) - area(b); }
}  // namespace

void DamageList::add(const Rect& r) {
    if (all_ || r.empty()) return;
    for (int i = 0; i < n_; ++i) {
        Rect& o = rects_[i];
        if (!o.intersects(r)) continue;
        Rect u = o.unite(r);
        if (area(u) == area(o)) return;  // already covered
        // United only when the union is no bigger than the two apart (overlap pays for the corners).
        if (area(u) <= area(o) + area(r) - area(o.intersect(r))) {
            o = u;
            return;
        }
    }
    if (n_ < kMax) {
        rects_[n_++] = r;
        return;
    }
    int best = 0;
    for (int i = 1; i < n_; ++i)
        if (waste(rects_[i], r) < waste(rects_[best], r)) best = i;
    rects_[best] = rects_[best].unite(r);
}

void DamageList::addMerged(Rect* r, int n, int budget) {
    // First unite overlapping rects that waste nothing (a moved item's old and new boxes),
    // in one cheap pass: the least-waste search below is cubic in the count.
    int m = 0;
    for (int i = 0; i < n; ++i) {
        Rect c = r[i];
        if (c.empty()) continue;
        for (int j = 0; j < m;) {
            if (c.intersects(r[j]) && waste(c, r[j]) <= 0) c = c.unite(r[j]), r[j] = r[--m], j = 0;
            else ++j;
        }
        r[m++] = c;
    }
    while (m > budget && m > 1) {
        // Waste is negative for overlapping pairs, so the first pair seeds the search (a
        // negative "unset" marker here once merged arbitrary pairs into one huge rect).
        int bi = 0, bj = 1;
        long best = waste(r[0], r[1]);
        for (int i = 0; i < m; ++i)
            for (int j = i + 1; j < m; ++j) {
                long w = waste(r[i], r[j]);
                if (w < best) best = w, bi = i, bj = j;
            }
        r[bi] = r[bi].unite(r[bj]);
        r[bj] = r[--m];
    }
    for (int i = 0; i < m; ++i) add(r[i]);
}

bool DamageList::touchesBand(int y, int h) const {
    if (all_) return true;
    for (int i = 0; i < n_; ++i)
        if (rects_[i].y < y + h && rects_[i].bottom() > y) return true;
    return false;
}

int DamageList::spansInBand(int y, int h, int width, int minGap, int maxSpans, int* x0, int* x1, int* y0,
                            int* y1) const {
    if (all_) {
        if (width <= 0) return 0;
        x0[0] = 0, x1[0] = width;
        if (y0) y0[0] = y, y1[0] = y + h;
        return 1;
    }
    struct Span {
        int lo, hi, top, bot;  // columns [lo, hi), rows [top, bot)
    } sp[kMax];
    int n = 0;
    for (int i = 0; i < n_; ++i) {
        const Rect& r = rects_[i];
        if (r.y >= y + h || r.bottom() <= y) continue;
        int a = std::max(r.x, 0), b = std::min(r.right(), width);
        if (a >= b) continue;
        int k = n++;  // insertion sort by start
        while (k > 0 && sp[k - 1].lo > a) sp[k] = sp[k - 1], --k;
        sp[k] = {a, b, std::max(r.y, y), std::min(r.bottom(), y + h)};
    }
    if (n == 0) return 0;
    auto join = [](Span& into, const Span& from) {
        into.hi = std::max(into.hi, from.hi);
        into.top = std::min(into.top, from.top), into.bot = std::max(into.bot, from.bot);
    };
    int m = 0;  // join overlapping and near ranges, in place
    for (int i = 1; i < n; ++i) {
        if (sp[i].lo - sp[m].hi < minGap) join(sp[m], sp[i]);
        else sp[++m] = sp[i];
    }
    ++m;
    while (m > maxSpans) {  // join across the smallest gap
        int best = 1;
        for (int i = 2; i < m; ++i)
            if (sp[i].lo - sp[i - 1].hi < sp[best].lo - sp[best - 1].hi) best = i;
        join(sp[best - 1], sp[best]);
        for (int i = best; i + 1 < m; ++i) sp[i] = sp[i + 1];
        --m;
    }
    for (int i = 0; i < m; ++i) {
        x0[i] = sp[i].lo, x1[i] = sp[i].hi;
        if (y0) y0[i] = sp[i].top, y1[i] = sp[i].bot;
    }
    return m;
}

StripRenderer::StripRenderer(hal::IDisplay& display, uint16_t* bufA, uint16_t* bufB, int stripH)
    : display_(display), bufs_{bufA, bufB}, stripH_(stripH) {}

void StripRenderer::setLayers(Layer* const* layers, int n) {
    layers_ = layers;
    nLayers_ = n;
    forceFull_ = true;
}

int StripRenderer::frame(float dt) {
    uint32_t t0 = micros_ ? micros_() : 0;
    damage_.clear();
    if (forceFull_) damage_.addAll();
    forceFull_ = false;
    for (int i = 0; i < nLayers_; ++i) layers_[i]->update(dt, damage_);
    if (dt > 0) fps_ = fps_ == 0 ? 1.f / dt : fps_ * 0.9f + (1.f / dt) * 0.1f;
    if (!damage_.any()) return 0;

    const int w = display_.width(), h = display_.height();
    int pushed = 0, which = 0;
    auto now = [this] { return micros_ ? micros_() : 0u; };
    for (uint32_t& us : layerUs_) us = 0;
    waitUs_ = 0;
    display_.beginFrame();
    for (int y = 0; y < h; y += stripH_) {
        int bh = y + stripH_ > h ? h - y : stripH_;
        // Redraw only the damaged blocks of this band (their columns, and the rows the damage
        // covers there) and push just those.
        int xs0[kMaxSpans], xs1[kMaxSpans], ys0[kMaxSpans], ys1[kMaxSpans];
        int spans = damage_.spansInBand(y, bh, w, kMinSpanGap, kMaxSpans, xs0, xs1, ys0, ys1);
        for (int s = 0; s < spans; ++s) {
            const int x0 = xs0[s], x1 = xs1[s], top = ys0[s], rows = ys1[s] - ys0[s];
            Canvas c(bufs_[which], x0, x1 - x0, top, rows);
            for (int i = 0; i < nLayers_; ++i) {
                c.resetClip();
                uint32_t t = now();
                layers_[i]->draw(c);
                if (i < kMaxLayers) layerUs_[i] += now() - t;
            }
            uint32_t tw = now();
            display_.waitDma();  // the other buffer's transfer must finish before we queue this one
            waitUs_ += now() - tw;
            display_.pushRect(x0, top, x1 - x0, rows, bufs_[which]);
            which ^= 1;
            ++pushed;
        }
    }
    uint32_t tw = now();
    display_.waitDma();
    display_.endFrame();
    waitUs_ += now() - tw;
    lastStrips_ = pushed;
    if (micros_) lastRenderUs_ = micros_() - t0;
    return pushed;
}

}  // namespace gfx
