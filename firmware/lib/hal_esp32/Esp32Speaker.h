#pragma once
// The alert chime through the board's speaker (board::kHasSpeaker): on the Waveshare 2.8, the PCM5101 I2S DAC.
// begin() starts a task on core 0 that renders the chime once into PSRAM and then plays it on request, so the
// render loop never waits for audio. On a board without a speaker every call does nothing.
#include <atomic>
#include <cstdint>

namespace hal {

class Esp32Speaker {
   public:
    // False on a board without a speaker, or when the I2S driver or the chime's buffer could not be set up.
    bool begin();
    bool available() const { return ready_; }
    // Plays the chime at volume 0..100 (the setting); a chime already playing starts over at the new volume.
    // Returns at once; safe from any task.
    void chime(int volumePercent);

   private:
    static void task(void* self);
    void run();
    bool ready_ = false;
    void* handle_ = nullptr;  // the task (TaskHandle_t)
    std::atomic<int> volume_{0};
    int16_t* pcm_ = nullptr;  // the rendered chime, mono
    int len_ = 0;
};

}  // namespace hal
