#ifndef MAIN_AUDIO_PLAYER_H_
#define MAIN_AUDIO_PLAYER_H_

#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>

void audio_player_init(void);
void audio_player_deinit(void);

void audio_player_enable_output(bool enable);
void audio_player_set_volume(int volume); // 0 to 100

// Plays a simple sine-wave tone at specified frequency (Hz) for specified duration (ms)
void audio_player_play_tone(int frequency_hz, int duration_ms);

#endif // MAIN_AUDIO_PLAYER_H_
