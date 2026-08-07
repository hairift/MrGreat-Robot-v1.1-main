#include "audio_codec.h"
#include "board.h"
#include "settings.h"

#include <esp_log.h>
#include <cmath>
#include <cstring>
#include <driver/i2s_common.h>

#define TAG "AudioCodec"

AudioCodec::AudioCodec() {
}

AudioCodec::~AudioCodec() {
}

void AudioCodec::OutputData(std::vector<int16_t>& data) {
    Write(data.data(), data.size());
}

bool AudioCodec::InputData(std::vector<int16_t>& data) {
    int samples = Read(data.data(), data.size());
    if (samples <= 0) {
        return false;
    }
    if (input_shelf_enabled_) {
        // Hanya cuplikan yang benar-benar terbaca yang disaring. Ukuran vektor
        // sengaja tidak diubah karena pemanggilnya memakai panjang tetap.
        ApplyInputShelf(data.data(), samples);
    }
    return true;
}

void AudioCodec::SetInputHighShelf(float cutoff_hz, float gain_db, float pre_gain) {
    if (gain_db == 0.0f || cutoff_hz <= 0.0f || input_sample_rate_ <= 0) {
        input_shelf_enabled_ = false;
        ESP_LOGI(TAG, "Penegas nada tinggi mikrofon dimatikan");
        return;
    }

    // Rak nada tinggi biquad, mengikuti audio cookbook Robert Bristow-Johnson.
    const float A = powf(10.0f, gain_db / 40.0f);
    const float w0 = 2.0f * (float)M_PI * cutoff_hz / (float)input_sample_rate_;
    const float cos0 = cosf(w0);
    // Q = 0,707 memberi lereng rak yang halus tanpa gundukan di sekitar sudut.
    const float alpha = sinf(w0) / 2.0f * sqrtf((A + 1.0f / A) * (1.0f / 0.707f - 1.0f) + 2.0f);
    const float dua_akar = 2.0f * sqrtf(A) * alpha;

    const float a0 = (A + 1.0f) - (A - 1.0f) * cos0 + dua_akar;
    shelf_b0_ = A * ((A + 1.0f) + (A - 1.0f) * cos0 + dua_akar) / a0;
    shelf_b1_ = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cos0) / a0;
    shelf_b2_ = A * ((A + 1.0f) + (A - 1.0f) * cos0 - dua_akar) / a0;
    shelf_a1_ = 2.0f * ((A - 1.0f) - (A + 1.0f) * cos0) / a0;
    shelf_a2_ = ((A + 1.0f) - (A - 1.0f) * cos0 - dua_akar) / a0;

    shelf_x1_ = shelf_x2_ = shelf_y1_ = shelf_y2_ = 0.0f;
    input_shelf_pre_gain_ = pre_gain;
    input_shelf_enabled_ = true;
    ESP_LOGI(TAG, "Penegas nada tinggi mikrofon: %.0f Hz, +%.1f dB, ruang puncak %.2f",
             cutoff_hz, gain_db, pre_gain);
}

void AudioCodec::ApplyInputShelf(int16_t* data, int samples) {
    for (int i = 0; i < samples; ++i) {
        const float x = (float)data[i] * input_shelf_pre_gain_;
        float y = shelf_b0_ * x + shelf_b1_ * shelf_x1_ + shelf_b2_ * shelf_x2_ -
                  shelf_a1_ * shelf_y1_ - shelf_a2_ * shelf_y2_;
        shelf_x2_ = shelf_x1_;
        shelf_x1_ = x;
        shelf_y2_ = shelf_y1_;
        shelf_y1_ = y;

        if (y > 32767.0f) {
            y = 32767.0f;
        } else if (y < -32768.0f) {
            y = -32768.0f;
        }
        data[i] = (int16_t)y;
    }
}

void AudioCodec::Start() {
    Settings settings("audio", false);
    output_volume_ = settings.GetInt("output_volume", output_volume_);
    if (output_volume_ <= 0) {
        ESP_LOGW(TAG, "Output volume value (%d) is too small, setting to default (10)", output_volume_);
        output_volume_ = 10;
    }

    ESP_LOGI(TAG, "Audio codec started");
}

void AudioCodec::SetOutputVolume(int volume) {
    output_volume_ = volume;
    ESP_LOGI(TAG, "Set output volume to %d", output_volume_);
    
    Settings settings("audio", true);
    settings.SetInt("output_volume", output_volume_);
}

void AudioCodec::SetInputGain(float gain) {
    input_gain_ = gain;
    ESP_LOGI(TAG, "Set input gain to %.1f", input_gain_);
}

void AudioCodec::EnableInput(bool enable) {
    if (enable == input_enabled_) {
        return;
    }
    input_enabled_ = enable;
    ESP_LOGI(TAG, "Set input enable to %s", enable ? "true" : "false");
}

void AudioCodec::EnableOutput(bool enable) {
    if (enable == output_enabled_) {
        return;
    }
    output_enabled_ = enable;
    ESP_LOGI(TAG, "Set output enable to %s", enable ? "true" : "false");
}
