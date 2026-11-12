#include "Esp32Speaker.h"

#include <Arduino.h>

#include "audio/Chime.h"
#include "board.h"
#include "services/Log.h"

#if defined(CONTRAIL_BOARD_WS28)
#include <driver/i2s.h>
#include <esp_heap_caps.h>
#endif

namespace hal {

#if defined(CONTRAIL_BOARD_WS28)
namespace {
constexpr i2s_port_t kPort = I2S_NUM_0;
constexpr int kFrames = 256;  // per write: ~12 ms at 22 kHz, so a new chime cuts in quickly
}  // namespace

bool Esp32Speaker::begin() {
    i2s_config_t c = {};
    c.mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_TX);
    c.sample_rate = audio::kChimeRate;
    c.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    c.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    c.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    c.intr_alloc_flags = 0;
    c.dma_buf_count = 6;
    c.dma_buf_len = kFrames;
    c.use_apll = false;
    c.tx_desc_auto_clear = true;  // silence, not a repeated buffer, when nothing is written
    if (i2s_driver_install(kPort, &c, 0, nullptr) != ESP_OK) {
        svc::logf("[audio] I2S driver failed");
        return false;
    }
    i2s_pin_config_t pins = {};
    pins.mck_io_num = I2S_PIN_NO_CHANGE;  // the PCM5101 makes its own clock from BCK
    pins.bck_io_num = board::kI2sBck;
    pins.ws_io_num = board::kI2sLrck;
    pins.data_out_num = board::kI2sDout;
    pins.data_in_num = I2S_PIN_NO_CHANGE;
    if (i2s_set_pin(kPort, &pins) != ESP_OK) {
        i2s_driver_uninstall(kPort);
        svc::logf("[audio] I2S pins failed");
        return false;
    }
    i2s_zero_dma_buffer(kPort);
    len_ = audio::chimeLength();
    pcm_ = static_cast<int16_t*>(heap_caps_malloc(size_t(len_) * 2, MALLOC_CAP_SPIRAM));
    if (!pcm_) {
        svc::logf("[audio] no PSRAM for the chime");
        return false;
    }
    TaskHandle_t h = nullptr;
    // Idle priority on core 0, as the network task: rendering the chime is seconds of CPU once, and at any higher
    // priority it starves IDLE0 and the task watchdog resets the board. Playing is mostly waiting on I2S.
    xTaskCreatePinnedToCore(task, "audio", 4096, this, tskIDLE_PRIORITY, &h, 0);
    handle_ = h;
    ready_ = h != nullptr;
    return ready_;
}

void Esp32Speaker::chime(int volumePercent) {
    if (!ready_) return;
    volume_ = volumePercent;
    xTaskNotifyGive(static_cast<TaskHandle_t>(handle_));
}

void Esp32Speaker::task(void* self) { static_cast<Esp32Speaker*>(self)->run(); }

void Esp32Speaker::run() {
    const uint32_t t0 = millis();
    audio::renderChime(pcm_, len_);
    svc::logf("[audio] chime ready: %d samples in %u ms", len_, unsigned(millis() - t0));
    static int16_t out[kFrames * 2];
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        bool again = true;
        while (again) {
            again = false;
            const float gain = audio::volumeGain(volume_);
            for (int i = 0; i < len_ && !again; i += kFrames) {
                const int n = std::min(kFrames, len_ - i);
                for (int k = 0; k < n; ++k) out[2 * k] = out[2 * k + 1] = int16_t(pcm_[i + k] * gain);
                size_t written = 0;
                i2s_write(kPort, out, size_t(n) * 4, &written, portMAX_DELAY);
                // Another chime asked for while this one plays: start over (the notification count is cleared).
                if (ulTaskNotifyTake(pdTRUE, 0)) again = true;
            }
        }
        i2s_zero_dma_buffer(kPort);
    }
}

#else
bool Esp32Speaker::begin() { return false; }
void Esp32Speaker::chime(int) {}
void Esp32Speaker::task(void*) {}
void Esp32Speaker::run() {}
#endif

}  // namespace hal
