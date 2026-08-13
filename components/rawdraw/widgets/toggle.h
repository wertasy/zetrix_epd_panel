/**
 * @file toggle.h
 * @brief Toggle switch widget — C port of rawdraw::Toggle.
 *
 * A pill-shaped track with a circular thumb indicator for on/off settings.
 *
 * On state: filled black track with white circle thumb on the right.
 * Off state: white track with black outlined circle thumb on the left.
 *
 * All draw functions take (fb, fb_width, fb_height) — no global state.
 *
 * Porting notes (C++ -> C):
 *  - class members -> struct fields; label stored in a fixed char buffer.
 *  - std::function<void(bool)> callback -> widget_toggle_callback_t + user_data.
 *  - const lv_font_t* kept as a non-owning pointer (fonts are static const).
 */
#ifndef WIDGETS_TOGGLE_H_
#define WIDGETS_TOGGLE_H_

#include <stdint.h>
#include <stdbool.h>
#include "../include/rawdraw.h"
#include "../include/font_engine.h"
#include "../include/rawdraw_ext.h"

#ifdef __cplusplus
extern "C" {
#endif

/** State change callback invoked when the toggle is tapped. */
typedef void (*widget_toggle_callback_t)(bool on, void *user_data);

/** Capacity of the inline label buffer. */
#define WIDGET_TOGGLE_LABEL_LEN 32

typedef struct {
    int x, y, w, h;
    bool state;

    char label[WIDGET_TOGGLE_LABEL_LEN]; /* "" = no label */
    const lv_font_t *font;

    widget_toggle_callback_t callback;
    void *callback_user_data;

    rawdraw_color_t track_on_color;
    rawdraw_color_t track_off_color;
    rawdraw_color_t thumb_color;
    rawdraw_color_t border_color;
} widget_toggle_t;

/* ---- lifecycle ---- */
void widget_toggle_init(widget_toggle_t *t, int x, int y, int w, int h);

/* ---- configuration ---- */
void widget_toggle_set_position(widget_toggle_t *t, int x, int y);
void widget_toggle_set_size(widget_toggle_t *t, int w, int h);
void widget_toggle_set_state(widget_toggle_t *t, bool on);
bool widget_toggle_get_state(const widget_toggle_t *t);
void widget_toggle_set_label(widget_toggle_t *t, const char *label);
void widget_toggle_set_font(widget_toggle_t *t, const lv_font_t *font);
void widget_toggle_set_callback(widget_toggle_t *t, widget_toggle_callback_t cb, void *user_data);
void widget_toggle_set_colors(widget_toggle_t *t, rawdraw_color_t track_on, rawdraw_color_t track_off,
                              rawdraw_color_t thumb, rawdraw_color_t border);

/* ---- state / input ---- */
bool widget_toggle_contains(const widget_toggle_t *t, int px, int py);
void widget_toggle_handle_tap(widget_toggle_t *t);
bool widget_toggle_handle_input(widget_toggle_t *t, const ui_button_event_t *event);

/* ---- geometry ---- */
rawdraw_rect_t widget_toggle_get_track_bounds(const widget_toggle_t *t);
rawdraw_rect_t widget_toggle_get_bounds(const widget_toggle_t *t, int screen_width);
rawdraw_point_t widget_toggle_get_thumb_center(const widget_toggle_t *t);

/* ---- rendering ---- */
void widget_toggle_render(const widget_toggle_t *t, uint8_t *fb, int fb_width, int fb_height);

#ifdef __cplusplus
}
#endif

#endif /* WIDGETS_TOGGLE_H_ */
