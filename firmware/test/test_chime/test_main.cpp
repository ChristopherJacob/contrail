#include <unity.h>

#include <cmath>
#include <cstdlib>
#include <vector>

#include "audio/Chime.h"

void setUp() {}
void tearDown() {}

namespace {
constexpr int kRate = audio::kChimeRate;

std::vector<int16_t> chime() {
    std::vector<int16_t> v(audio::chimeLength(kRate));
    audio::renderChime(v.data(), int(v.size()), kRate);
    return v;
}

// The level of `hz` in samples [from, from + n): the Goertzel algorithm, normalised so a full-scale sine is ~1.
double tone(const std::vector<int16_t>& v, int from, int n, double hz) {
    const double w = 2 * M_PI * hz / kRate, c = 2 * std::cos(w);
    double s1 = 0, s2 = 0;
    for (int i = 0; i < n; ++i) {
        const double s = v[from + i] / 32768.0 + c * s1 - s2;
        s2 = s1, s1 = s;
    }
    return std::sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) * 2 / n;
}
}  // namespace

static void test_length_and_level() {
    const auto v = chime();
    TEST_ASSERT_EQUAL_INT(int(std::lround(2.3 * kRate)), int(v.size()));
    int peak = 0;
    for (int16_t s : v) peak = std::max(peak, std::abs(int(s)));
    TEST_ASSERT_INT_WITHIN(400, int(0.78 * 32767), peak);  // ~-2 dBFS: loud, never clipped
}

static void test_starts_and_ends_silent() {
    const auto v = chime();
    TEST_ASSERT_INT_WITHIN(50, 0, v[0]);  // a soft attack: no click
    int first5ms = 0;
    for (int i = 0; i < kRate / 200; ++i) first5ms = std::max(first5ms, std::abs(int(v[i])));
    TEST_ASSERT_LESS_THAN(3000, first5ms);  // still under 10 % of full 5 ms in: a swell, not a strike
    TEST_ASSERT_EQUAL_INT(0, v.back());   // faded to exact silence
    int tail = 0;
    for (size_t i = v.size() - 200; i < v.size(); ++i) tail = std::max(tail, std::abs(int(v[i])));
    TEST_ASSERT_LESS_THAN(200, tail);
}

static void test_high_note_then_low_note() {
    const auto v = chime();
    const int win = kRate / 10;  // 100 ms
    // First note (D5, 587 Hz) before the second starts; the second (B4, 494 Hz) after.
    const int early = int(0.10 * kRate), late = int(1.15 * kRate);
    TEST_ASSERT_TRUE(tone(v, early, win, 587.33) > 20 * tone(v, early, win, 493.88));
    TEST_ASSERT_TRUE(tone(v, late, win, 493.88) > 20 * tone(v, late, win, 587.33));
    // Nothing before the second note at 1.02 s is at its pitch.
    TEST_ASSERT_TRUE(tone(v, int(0.85 * kRate), win, 493.88) < 0.01);
    // Nearly pure: no octave, and the third harmonic far down (the measured chime's is 44 dB under).
    TEST_ASSERT_TRUE(tone(v, early, win, 2 * 587.33) < 0.01 * tone(v, early, win, 587.33));  // -40 dB: window leakage only
    TEST_ASSERT_TRUE(tone(v, early, win, 3 * 587.33) < 0.02 * tone(v, early, win, 587.33));
}

static void test_short_and_long_buffers() {
    const auto v = chime();
    std::vector<int16_t> shorter(1000), longer(v.size() + 500, 123);
    audio::renderChime(shorter.data(), int(shorter.size()), kRate);
    audio::renderChime(longer.data(), int(longer.size()), kRate);
    for (size_t i = 0; i < shorter.size(); ++i) TEST_ASSERT_EQUAL_INT(v[i], shorter[i]);
    for (size_t i = 0; i < v.size(); ++i) TEST_ASSERT_EQUAL_INT(v[i], longer[i]);
    for (size_t i = v.size(); i < longer.size(); ++i) TEST_ASSERT_EQUAL_INT(0, longer[i]);
}

static void test_volume_gain() {
    TEST_ASSERT_EQUAL_FLOAT(0, audio::volumeGain(0));
    TEST_ASSERT_EQUAL_FLOAT(0, audio::volumeGain(-5));
    TEST_ASSERT_EQUAL_FLOAT(0.25f, audio::volumeGain(50));
    TEST_ASSERT_EQUAL_FLOAT(1, audio::volumeGain(100));
    TEST_ASSERT_EQUAL_FLOAT(1, audio::volumeGain(150));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_length_and_level);
    RUN_TEST(test_starts_and_ends_silent);
    RUN_TEST(test_high_note_then_low_note);
    RUN_TEST(test_short_and_long_buffers);
    RUN_TEST(test_volume_gain);
    return UNITY_END();
}
