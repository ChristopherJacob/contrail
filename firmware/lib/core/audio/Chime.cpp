#include "Chime.h"

#include <cmath>

namespace audio {
namespace {

// One partial of a note: a multiple of the note's frequency, its level, and how fast it dies (seconds to fall to
// 1/e). The measured chime is almost a pure sine: its third harmonic sits 44 dB down, and there is no octave.
struct Partial {
    float ratio, level, tau;
};
constexpr Partial kPartials[] = {
    {1.f, 1.000f, 0.18f},  // ~48 dB a second, as measured on both notes
    {3.f, 0.006f, 0.15f},
};

struct Note {
    float hz, startS, level;
};
// D5 then B4, the second 1.02 s after the first and as loud.
constexpr Note kNotes[] = {
    {587.33f, 0.00f, 1.00f},
    {493.88f, 1.02f, 1.00f},
};

// A soft onset, not a strike: the recording takes ~29 ms from 10 % to 90 %, which a raised cosine does over 50 ms.
constexpr float kAttackS = 0.050f;
constexpr float kLengthS = 2.3f;
constexpr float kFadeS = 0.25f;  // the last of the ring fades to exact silence
constexpr float kPeak = 0.78f;   // about -2.2 dBFS

// Calls emit(v) for each sample of the chime, before the level. Dry: the recording has no room in it.
//
// Each partial is a two-pole resonator, y[k] = c*y[k-1] - d*y[k-2] with c = 2 r cos(w) and d = r^2, which rings
// as r^k sin(k w): an exponentially decaying sine for two multiplies a sample. sin and exp per sample were ~100x
// slower, long seconds of CPU on the board. Double precision keeps the 50,000-step recurrences exact enough.
template <class Emit>
void generate(int rate, Emit emit) {
    constexpr int kNotesN = sizeof kNotes / sizeof kNotes[0], kPartialsN = sizeof kPartials / sizeof kPartials[0];
    struct Osc {
        double c = 0, d = 0, y1 = 0, y2 = 0;
    } osc[kNotesN][kPartialsN];
    int startAt[kNotesN];
    const int attackN = int(kAttackS * rate);
    for (int j = 0; j < kNotesN; ++j) {
        startAt[j] = int(std::lround(kNotes[j].startS * rate));
        for (int p = 0; p < kPartialsN; ++p) {
            const double w = 2 * 3.14159265358979 * kNotes[j].hz * kPartials[p].ratio / rate;
            const double r = std::exp(-1.0 / (kPartials[p].tau * rate));
            Osc& o = osc[j][p];
            o.c = 2 * r * std::cos(w), o.d = r * r;
            o.y1 = r * std::sin(w), o.y2 = 0;  // k = 1 and k = 0: the sine starts at phase 0
        }
    }
    const int len = chimeLength(rate);
    for (int i = 0; i < len; ++i) {
        double v = 0;
        for (int j = 0; j < kNotesN; ++j) {
            const int k = i - startAt[j];
            if (k < 0) continue;
            double note = 0;
            for (int p = 0; p < kPartialsN; ++p) {
                Osc& o = osc[j][p];
                double y;
                if (k == 0) y = o.y2;
                else if (k == 1) y = o.y1;
                else y = o.c * o.y1 - o.d * o.y2, o.y2 = o.y1, o.y1 = y;
                note += kPartials[p].level * y;
            }
            const double attack = k < attackN ? 0.5 - 0.5 * std::cos(3.14159265358979 * k / attackN) : 1.0;
            v += kNotes[j].level * attack * note;
        }
        emit(v);
    }
}

}  // namespace

int chimeLength(int rate) { return int(std::lround(kLengthS * rate)); }

void renderChime(int16_t* out, int n, int rate) {
    // Twice: once for the peak, so the level puts it at kPeak, then for the samples.
    double peak = 0;
    generate(rate, [&](double v) { peak = std::fmax(peak, std::fabs(v)); });
    const double scale = kPeak / peak * 32767.0;
    const int len = chimeLength(rate);
    const int fadeFrom = len - int(kFadeS * rate);
    int i = 0;
    generate(rate, [&](double v) {
        if (i < n) {
            v *= scale;
            if (i > fadeFrom) v *= double(len - i) / double(len - fadeFrom);
            out[i] = int16_t(std::lround(std::fmax(-32767.0, std::fmin(32767.0, v))));
        }
        ++i;
    });
    for (int k = len; k < n; ++k) out[k] = 0;
}

float volumeGain(int percent) {
    if (percent <= 0) return 0;
    if (percent >= 100) return 1;
    const float v = percent / 100.f;
    return v * v;
}

}  // namespace audio
