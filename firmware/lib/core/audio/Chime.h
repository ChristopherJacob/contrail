#pragma once
// The alert chime: the cabin "ding-dong", two soft, nearly pure tones a minor third apart (D5, then B4 a second
// later), synthesised rather than recorded. Its pitches, attack, decay and spacing were measured from a
// royalty-free recording of an airliner chime (Pixabay, "airplane-chime" by saboteurcomics); no audio is copied.
// Plain functions of the sample rate, so the tests and the WAV preview (tools/chime_wav.cpp) hear exactly what
// the board plays.
#include <cstdint>

namespace audio {

constexpr int kChimeRate = 22050;  // Hz: the chime has nothing above ~5 kHz

// The chime's length in samples at `rate` (2.3 s).
int chimeLength(int rate = kChimeRate);

// Writes the chime, mono 16-bit, into out[0..n): n is normally chimeLength(rate); a shorter n gets the start,
// a longer one silence after. Peaks just under -2 dBFS. Allocates nothing.
void renderChime(int16_t* out, int n, int rate = kChimeRate);

// The chime's gain for a volume setting of 0..100: a square law, so the steps sound even; 0 is silent.
float volumeGain(int percent);

}  // namespace audio
