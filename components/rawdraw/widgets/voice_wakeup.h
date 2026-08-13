/**
 * @file voice_wakeup.h
 * @brief Voice wakeup listener with BOOT button long-press trigger.
 */
#ifndef WIDGETS_VOICE_WAKEUP_H_
#define WIDGETS_VOICE_WAKEUP_H_

#include <stdint.h>
#include <stdbool.h>
#include "../include/rawdraw.h"
#include "../include/font_engine.h"
#include "../include/style.h"
#include "../include/refresh.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIDGET_VOICE_OVERLAY_X 140
#define WIDGET_VOICE_OVERLAY_Y 120
#define WIDGET_VOICE_OVERLAY_W 120
#define WIDGET_VOICE_OVERLAY_H 40
#define WIDGET_VOICE_OFFLINE_MSG_MS 3000
#define WIDGET_VOICE_DONE_FADE_MS 1500

typedef enum {
    WIDGET_VOICE_STATE_IDLE,
    WIDGET_VOICE_STATE_CHECKING_NETWORK,
    WIDGET_VOICE_STATE_RECORDING,
    WIDGET_VOICE_STATE_WAITING_RESPONSE,
    WIDGET_VOICE_STATE_OFFLINE_MSG,
    WIDGET_VOICE_STATE_DONE
} widget_voice_state_t;

typedef struct {
    widget_voice_state_t state;
    region_refresh_t refresh;
    const lv_font_t *font;
    char overlay_text[64];
    int64_t state_start_us;
    bool visible;
} widget_voice_wakeup_state_t;

/* ---- lifecycle ---- */
void widget_voice_wakeup_init(widget_voice_wakeup_state_t *state, const lv_font_t *font);

/* ---- control ---- */
void widget_voice_wakeup_start_recording(widget_voice_wakeup_state_t *state);
void widget_voice_wakeup_waiting(widget_voice_wakeup_state_t *state);
void widget_voice_wakeup_show_offline(widget_voice_wakeup_state_t *state);
void widget_voice_wakeup_done(widget_voice_wakeup_state_t *state);
void widget_voice_wakeup_reset(widget_voice_wakeup_state_t *state);

/* ---- state ---- */
bool widget_voice_wakeup_is_visible(const widget_voice_wakeup_state_t *state);
void widget_voice_wakeup_tick(widget_voice_wakeup_state_t *state, int64_t now_us);
const char *widget_voice_wakeup_state_to_string(widget_voice_state_t state);

/* ---- geometry ---- */
rawdraw_rect_t widget_voice_wakeup_get_bounds(void);

/* ---- rendering ---- */
bool widget_voice_wakeup_render(widget_voice_wakeup_state_t *state, uint8_t *fb, int width, int height);

#ifdef __cplusplus
}
#endif

#endif /* WIDGETS_VOICE_WAKEUP_H_ */
