#include "../include/rawdraw_util.h"
#include "voice_wakeup.h"
#include "../include/layout.h"
#include "../include/rawdraw_ext.h"
#include <string.h>
#include <stdio.h>

void widget_voice_wakeup_init(widget_voice_wakeup_state_t *state, const lv_font_t *font)
{
    if (!state)
        return;
    state->state           = WIDGET_VOICE_STATE_IDLE;
    state->visible         = false;
    state->overlay_text[0] = '\0';
    state->state_start_us  = 0;
    state->font            = font ? font : &BUILTIN_TEXT_FONT;
    refresh_tracker_init(&state->refresh);
}

void widget_voice_wakeup_start_recording(widget_voice_wakeup_state_t *state)
{
    if (!state)
        return;
    state->state = WIDGET_VOICE_STATE_RECORDING;
    snprintf(state->overlay_text, sizeof(state->overlay_text),
             "\xe5\xbd\x95\xe9\x9f\xb3\xe4\xb8\xad..."); /* "录音中..." */
    state->state_start_us = esp_timer_get_time();
    state->visible        = true;
    refresh_mark_dirty(&state->refresh);
}

void widget_voice_wakeup_waiting(widget_voice_wakeup_state_t *state)
{
    if (!state)
        return;
    state->state = WIDGET_VOICE_STATE_WAITING_RESPONSE;
    snprintf(state->overlay_text, sizeof(state->overlay_text),
             "\xe5\xa4\x84\xe7\x90\x86\xe4\xb8\xad..."); /* "处理中..." */
    state->state_start_us = esp_timer_get_time();
    refresh_mark_dirty(&state->refresh);
}

void widget_voice_wakeup_show_offline(widget_voice_wakeup_state_t *state)
{
    if (!state)
        return;
    state->state = WIDGET_VOICE_STATE_OFFLINE_MSG;
    snprintf(state->overlay_text, sizeof(state->overlay_text),
             "\xe7\xa6\xbb\xe7\xba\xbf\xe7\x8a\xb6\xe6\x80\x81\xe4\xb8\x8b\xe6\x97\xa0\xe6\xb3\x95\xe4\xbd\xbf\xe7\x94"
             "\xa8\xe8\xaf\xad\xe9\x9f\xb3"); /* "离线状态下无法使用语音" */
    state->state_start_us = esp_timer_get_time();
    state->visible        = true;
    refresh_mark_dirty(&state->refresh);
}

void widget_voice_wakeup_done(widget_voice_wakeup_state_t *state)
{
    if (!state)
        return;
    state->state = WIDGET_VOICE_STATE_DONE;
    snprintf(state->overlay_text, sizeof(state->overlay_text), "\xe5\xae\x8c\xe6\x88\x90"); /* "完成" */
    state->state_start_us = esp_timer_get_time();
    refresh_mark_dirty(&state->refresh);
}

void widget_voice_wakeup_reset(widget_voice_wakeup_state_t *state)
{
    if (!state)
        return;
    state->state           = WIDGET_VOICE_STATE_IDLE;
    state->visible         = false;
    state->overlay_text[0] = '\0';
    state->state_start_us  = 0;
    refresh_mark_clean(&state->refresh);
}

bool widget_voice_wakeup_is_visible(const widget_voice_wakeup_state_t *state)
{
    return state && state->visible;
}

void widget_voice_wakeup_tick(widget_voice_wakeup_state_t *state, int64_t now_us)
{
    if (!state || !state->visible)
        return;

    int64_t elapsed_ms = (now_us - state->state_start_us) / 1000;

    switch (state->state) {
    case WIDGET_VOICE_STATE_OFFLINE_MSG:
        if (elapsed_ms >= WIDGET_VOICE_OFFLINE_MSG_MS) {
            widget_voice_wakeup_reset(state);
        }
        break;

    case WIDGET_VOICE_STATE_DONE:
        if (elapsed_ms >= WIDGET_VOICE_DONE_FADE_MS) {
            widget_voice_wakeup_reset(state);
        }
        break;

    case WIDGET_VOICE_STATE_RECORDING:
    case WIDGET_VOICE_STATE_WAITING_RESPONSE:
    case WIDGET_VOICE_STATE_IDLE:
    case WIDGET_VOICE_STATE_CHECKING_NETWORK:
        /* Externally controlled, no auto-transition */
        break;
    }
}

const char *widget_voice_wakeup_state_to_string(widget_voice_state_t state)
{
    switch (state) {
    case WIDGET_VOICE_STATE_IDLE:
        return "IDLE";
    case WIDGET_VOICE_STATE_CHECKING_NETWORK:
        return "CHECKING_NETWORK";
    case WIDGET_VOICE_STATE_RECORDING:
        return "RECORDING";
    case WIDGET_VOICE_STATE_WAITING_RESPONSE:
        return "WAITING_RESPONSE";
    case WIDGET_VOICE_STATE_OFFLINE_MSG:
        return "OFFLINE_MSG";
    case WIDGET_VOICE_STATE_DONE:
        return "DONE";
    default:
        return "UNKNOWN";
    }
}

rawdraw_rect_t widget_voice_wakeup_get_bounds(void)
{
    return (rawdraw_rect_t){WIDGET_VOICE_OVERLAY_X, WIDGET_VOICE_OVERLAY_Y, WIDGET_VOICE_OVERLAY_W,
                            WIDGET_VOICE_OVERLAY_H};
}

bool widget_voice_wakeup_render(widget_voice_wakeup_state_t *state, uint8_t *fb, int width, int height)
{
    if (!fb || !state || !state->visible)
        return false;

    rawdraw_rect_t bounds = widget_voice_wakeup_get_bounds();
    bounds                = rawdraw_clamp_rect(bounds, width, height);
    if (rawdraw_rect_area(bounds) <= 0)
        return false;

    /* Draw rounded rectangle background (white) with black border */
    rawdraw_draw_round_rect(fb, width, height, bounds.x, bounds.y, bounds.w, bounds.h, STYLE_BORDER_RADIUS_MD,
                            (int)RAWDRAW_COLOR_WHITE, (int)RAWDRAW_COLOR_BLACK, STYLE_BORDER_THIN);

    /* Draw state-specific icon + text */
    if (state->font && state->overlay_text[0] != '\0') {
        int text_w = rawdraw_measure_text_width(state->overlay_text, state->font);
        int text_h = state->font->line_height;
        int text_x = bounds.x + (bounds.w - text_w) / 2;
        int text_y = bounds.y + (bounds.h - text_h) / 2;

        rawdraw_draw_text(fb, width, height, text_x, text_y, state->overlay_text, state->font,
                          (int)RAWDRAW_COLOR_BLACK);
    }

    refresh_update_counter(&state->refresh, esp_timer_get_time());

    return true;
}
