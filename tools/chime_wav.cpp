// The alert chime as a WAV file, from the firmware's own synthesiser (firmware/lib/core/audio/Chime.cpp), for
// listening to it on a computer:
//   c++ -std=c++17 -O2 -I firmware/lib/core tools/chime_wav.cpp firmware/lib/core/audio/Chime.cpp -o chime_wav
//   ./chime_wav chime.wav
#include <cstdint>
#include <cstdio>
#include <vector>

#include "audio/Chime.h"

namespace {
void put16(FILE* f, uint16_t v) { std::fputc(v & 0xFF, f), std::fputc(v >> 8, f); }
void put32(FILE* f, uint32_t v) { put16(f, uint16_t(v)), put16(f, uint16_t(v >> 16)); }
}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <out.wav>\n", argv[0]);
        return 2;
    }
    const int rate = audio::kChimeRate, n = audio::chimeLength(rate);
    std::vector<int16_t> pcm(n);
    audio::renderChime(pcm.data(), n, rate);
    FILE* f = std::fopen(argv[1], "wb");
    if (!f) {
        std::perror(argv[1]);
        return 1;
    }
    const uint32_t bytes = uint32_t(n) * 2;
    std::fwrite("RIFF", 1, 4, f), put32(f, 36 + bytes), std::fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16), put16(f, 1), put16(f, 1), put32(f, rate), put32(f, rate * 2), put16(f, 2), put16(f, 16);
    std::fwrite("data", 1, 4, f), put32(f, bytes);
    std::fwrite(pcm.data(), 2, n, f);
    std::fclose(f);
    std::printf("%s: %d samples at %d Hz (%.2f s)\n", argv[1], n, rate, double(n) / rate);
    return 0;
}
