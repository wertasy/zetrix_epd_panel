/**
 * @file button.h
 * @brief Button widget — C port of rawdraw::Button.
 *
 * Rounded button supporting an icon-font glyph and an optional text label,
 * with normal/pressed visual states (pressed inverts the colors).
 *
 * All draw functions take (fb, fb_width, fb_height) — no global state.
 *
 * Porting notes (C++ -> C):
 *  - class members -> struct fields; text/icon stored in fixed char buffers.
 *  - std::function<void()> callback -> widget_button_callback_t + user_data.
 *  - const lv_font_t* kept as a non-owning pointer (fonts are static const).
 */
#ifndef WIDGETS_BUTTON_H_
#define WIDGETS_BUTTON_H_

#include <stdint.h>
#include <stdbool.h>
#include "../include/rawdraw.h"
#include "../include/font_engine.h"
#include "../include/rawdraw_ext.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Click callback invoked by widget_button_handle_press(). */
typedef void (*widget_button_callback_t)(void *user_data);

/** Capacity of the inline icon-code buffer (UTF-8 glyph code). */
#define WIDGET_BUTTON_ICON_LEN 16
/** Capacity of the inline label buffer. */
#define WIDGET_BUTTON_TEXT_LEN 32

typedef struct {
    int x, y, w, h;
    int radius;

    char             icon_code[WIDGET_BUTTON_ICON_LEN]; /* "" = no icon */
    const lv_font_t *icon_font;

    char             text[WIDGET_BUTTON_TEXT_LEN]; /* "" = no label */
    const lv_font_t *text_font;

    bool                     pressed;
    widget_button_callback_t callback;
    void                    *callback_user_data;

    rawdraw_color_t bg_color;
    rawdraw_color_t fg_color;
    rawdraw_color_t border_color;
} widget_button_t;

/* ---- lifecycle ---- */
void widget_button_init(widget_button_t *btn, int x, int y, int w, int h);

/* ---- configuration ---- */
void widget_button_set_position(widget_button_t *btn, int x, int y);
void widget_button_set_size(widget_button_t *btn, int w, int h);
void widget_button_set_icon(widget_button_t *btn, const char *icon_code);
void widget_button_set_icon_font(widget_button_t *btn, const lv_font_t *font);
void widget_button_set_text(widget_button_t *btn, const char *text);
void widget_button_set_text_font(widget_button_t *btn, const lv_font_t *font);
void widget_button_set_radius(widget_button_t *btn, int radius);
void widget_button_set_callback(widget_button_t *btn, widget_button_callback_t cb, void *user_data);
void widget_button_set_colors(widget_button_t *btn, rawdraw_color_t bg, rawdraw_color_t fg, rawdraw_color_t border);

/* ---- state / input ---- */
bool widget_button_contains(const widget_button_t *btn, int px, int py);
void widget_button_set_pressed(widget_button_t *btn, bool pressed);
bool widget_button_is_pressed(const widget_button_t *btn);
void widget_button_handle_press(widget_button_t *btn);
bool widget_button_handle_input(widget_button_t *btn, const ui_button_event_t *event);

/* ---- geometry / rendering ---- */
rawdraw_rect_t widget_button_get_bounds(const widget_button_t *btn);
void           widget_button_render(const widget_button_t *btn, uint8_t *fb, int fb_width, int fb_height);

#ifdef __cplusplus
}
#endif

#endif /* WIDGETS_BUTTON_H_ */
