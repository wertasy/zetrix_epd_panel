#include "audio_player.h"
#include "board.h"
#include "config.h"
#include <driver/i2s_std.h>
#include <esp_codec_dev.h>
#include <esp_codec_dev_defaults.h>
#include <esp_log.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const char* TAG = "audio_player";

static i2s_chan_handle_t s_tx_handle = NULL;
static i2s_chan_handle_t s_rx_handle = NULL;

static const audio_codec_data_if_t* s_data_if = NULL;
static const audio_codec_ctrl_if_t* s_ctrl_if = NULL;
static const audio_codec_gpio_if_t* s_gpio_if = NULL;
static const audio_codec_if_t* s_codec_if = NULL;
static esp_codec_dev_handle_t s_codec_dev = NULL;

static int s_volume = 70;
static bool s_output_enabled = false;

void audio_player_init(void) {
    ESP_LOGI(TAG, "Initializing Audio Player (I2S and ES8311)");

    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 6,
        .dma_frame_num = 240,
        .auto_clear_after_cb = true,
        .auto_clear_before_cb = false,
        .intr_priority = 0,
    };
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &s_tx_handle, &s_rx_handle));

    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = 16000,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .slot_mask = I2S_STD_SLOT_BOTH,
            .ws_width = I2S_DATA_BIT_WIDTH_16BIT,
            .ws_pol = false,
            .bit_shift = true,
        },
        .gpio_cfg = {
            .mclk = AUDIO_I2S_GPIO_MCLK,
            .bclk = AUDIO_I2S_GPIO_BCLK,
            .ws = AUDIO_I2S_GPIO_WS,
            .dout = AUDIO_I2S_GPIO_DOUT,
            .din = AUDIO_I2S_GPIO_DIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false
            }
        }
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_rx_handle, &std_cfg));

    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = I2S_NUM_0,
        .rx_handle = s_rx_handle,
        .tx_handle = s_tx_handle,
    };
    s_data_if = audio_codec_new_i2s_data(&i2s_cfg);
    assert(s_data_if != NULL);

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = 0,
        .addr = AUDIO_CODEC_ES8311_ADDR,
        .bus_handle = g_board.i2c_bus,
    };
    s_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    assert(s_ctrl_if != NULL);

    s_gpio_if = audio_codec_new_gpio();
    assert(s_gpio_if != NULL);

    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = s_ctrl_if,
        .gpio_if = s_gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,
        .pa_pin = AUDIO_CODEC_PA_PIN,
        .use_mclk = true,
        .hw_gain.pa_voltage = 5.0,
        .hw_gain.codec_dac_voltage = 3.3,
        .pa_reverted = false,
    };
    s_codec_if = es8311_codec_new(&es8311_cfg);
    assert(s_codec_if != NULL);

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
        .codec_if = s_codec_if,
        .data_if = s_data_if,
    };
    s_codec_dev = esp_codec_dev_new(&dev_cfg);
    assert(s_codec_dev != NULL);

    ESP_LOGI(TAG, "Codec ES8311 successfully initialized");
}

void audio_player_deinit(void) {
    if (s_codec_dev) {
        esp_codec_dev_delete(s_codec_dev);
        s_codec_dev = NULL;
    }
    if (s_codec_if) {
        audio_codec_delete_codec_if(s_codec_if);
        s_codec_if = NULL;
    }
    if (s_gpio_if) {
        audio_codec_delete_gpio_if(s_gpio_if);
        s_gpio_if = NULL;
    }
    if (s_ctrl_if) {
        audio_codec_delete_ctrl_if(s_ctrl_if);
        s_ctrl_if = NULL;
    }
    if (s_data_if) {
        audio_codec_delete_data_if(s_data_if);
        s_data_if = NULL;
    }
    if (s_tx_handle) {
        i2s_del_channel(s_tx_handle);
        s_tx_handle = NULL;
    }
    if (s_rx_handle) {
        i2s_del_channel(s_rx_handle);
        s_rx_handle = NULL;
    }
}

void audio_player_enable_output(bool enable) {
    if (!s_codec_dev) return;
    if (enable == s_output_enabled) return;

    if (enable) {
        esp_codec_dev_sample_info_t fs = {
            .bits_per_sample = 16,
            .channel = 1,
            .channel_mask = 0,
            .sample_rate = 16000,
            .mclk_multiple = 0,
        };
        ESP_ERROR_CHECK(esp_codec_dev_open(s_codec_dev, &fs));
        ESP_ERROR_CHECK(esp_codec_dev_set_out_vol(s_codec_dev, s_volume));
        i2s_channel_enable(s_tx_handle);
    } else {
        i2s_channel_disable(s_tx_handle);
        esp_codec_dev_close(s_codec_dev);
    }
    s_output_enabled = enable;
}

void audio_player_set_volume(int volume) {
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    s_volume = volume;
    if (s_codec_dev && s_output_enabled) {
        esp_codec_dev_set_out_vol(s_codec_dev, s_volume);
    }
}

void audio_player_play_tone(int frequency_hz, int duration_ms) {
    if (!s_codec_dev) return;

    ESP_LOGI(TAG, "Playing tone: freq=%dHz, dur=%dms", frequency_hz, duration_ms);

    audio_player_enable_output(true);

    const int sample_rate = 16000;
    const int total_samples = (sample_rate * duration_ms) / 1000;
    
    const int chunk_samples = 1000;
    int16_t* buffer = malloc(chunk_samples * sizeof(int16_t));
    if (!buffer) return;

    int samples_played = 0;
    while (samples_played < total_samples) {
        int to_play = total_samples - samples_played;
        if (to_play > chunk_samples) {
            to_play = chunk_samples;
        }

        for (int i = 0; i < to_play; i++) {
            int t = samples_played + i;
            double angle = 2.0 * M_PI * frequency_hz * t / sample_rate;
            buffer[i] = (int16_t)(10000.0 * sin(angle));
        }

        esp_codec_dev_write(s_codec_dev, buffer, to_play * sizeof(int16_t));
        samples_played += to_play;
    }

    free(buffer);
    
    vTaskDelay(pdMS_TO_TICKS(100));

    audio_player_enable_output(false);
}
