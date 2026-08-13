/**
 * @file slider.h
 * @brief Horizontal slider widget — C port of rawdraw::Slider.
 *
 * A horizontal track with a draggable diamond thumb indicator.
 * Supports min/max value range and optional label display.
 *
 * All draw functions take (fb, fb_width, fb_height) — no global state.
 *
 * Porting notes (C++ -> C):
 *  - class members -> struct fields; labels stored in fixed char buffers.
 *  - std::function<void(int)> callback -> widget_slider_callback_t + user_data.
 *  - std::clamp / std::max / std::min -> inline clamping.
 *  - const lv_font_t* kept as a non-owning pointer (fonts are static const).
 */
#ifndef WIDGETS_SLIDER_H_
#define WIDGETS_SLIDER_H_

#include <stdint.h>
#include <stdbool.h>
#include "../include/rawdraw.h"
#include "../include/font_engine.h"
#include "../include/rawdraw_ext.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Value change callback invoked when the slider value changes. */
typedef void (*widget_slider_callback_t)(int value, void *user_data);

/** Capacity of inline label buffers. */
#define WIDGET_SLIDER_LABEL_LEN 16

typedef struct {
    int x, y, w, h;
    int min_val, max_val;
    int value;

    char min_label[WIDGET_SLIDER_LABEL_LEN]; /* auto-populated from range */
    char max_label[WIDGET_SLIDER_LABEL_LEN]; /* auto-populated from range */
    char value_label[WIDGET_SLIDER_LABEL_LEN]; /* "" = no value label      */

    const lv_font_t *font;
    widget_slider_callback_t callback;
    void *callback_user_data;

    rawdraw_color_t track_bg_color;
    rawdraw_color_t track_fill_color;
    rawdraw_color_t thumb_color;
    rawdraw_color_t text_color;
    rawdraw_color_t border_color;
    bool custom_colors;
} widget_slider_t;

/* ---- lifecycle ---- */
void widget_slider_init(widget_slider_t *s, int x, int y, int w, int h, int min_val, int max_val);

/* ---- configuration ---- */
void widget_slider_set_position(widget_slider_t *s, int x, int y);
void widget_slider_set_size(widget_slider_t *s, int w, int h);
void widget_slider_set_range(widget_slider_t *s, int min_val, int max_val);
void widget_slider_set_value(widget_slider_t *s, int value);
int widget_slider_get_value(const widget_slider_t *s);
int widget_slider_get_value_percent(const widget_slider_t *s);
void widget_slider_set_labels(widget_slider_t *s, const char *min_label, const char *max_label,
                              const char *value_label);
void widget_slider_set_font(widget_slider_t *s, const lv_font_t *font);
void widget_slider_set_callback(widget_slider_t *s, widget_slider_callback_t cb, void *user_data);
void widget_slider_set_colors(widget_slider_t *s, rawdraw_color_t track_bg, rawdraw_color_t track_fill,
                              rawdraw_color_t thumb, rawdraw_color_t text);

/* ---- state / input ---- */
bool widget_slider_contains(const widget_slider_t *s, int px, int py);
bool widget_slider_handle_drag(widget_slider_t *s, int px);
bool widget_slider_handle_input(widget_slider_t *s, const ui_button_event_t *event);

/* ---- geometry ---- */
rawdraw_rect_t widget_slider_get_bounds(const widget_slider_t *s);
rawdraw_rect_t widget_slider_get_track_bounds(const widget_slider_t *s);
rawdraw_point_t widget_slider_get_thumb_center(const widget_slider_t *s);
int widget_slider_x_to_value(const widget_slider_t *s, int px);

/* ---- rendering ---- */
void widget_slider_render(const widget_slider_t *s, uint8_t *fb, int fb_width, int fb_height);

#ifdef __cplusplus
}
#endif

#endif /* WIDGETS_SLIDER_H_ */
