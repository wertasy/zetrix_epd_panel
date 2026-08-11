/**
 * @file list_item.h
 * @brief List item widget — C port of rawdraw::ListItem.
 *
 * A horizontal row with optional icon, label, value, and chevron arrow,
 * designed for settings menus and list views. Supports a pressed state
 * (inverted colors) and an optional separator line at the bottom.
 *
 * Layout:
 *   [Icon] Label            Value  [Chevron]
 *   ─────────────────────────────────────
 *
 * All draw functions take (fb, fb_width, fb_height) — no global state.
 *
 * Porting notes (C++ -> C):
 *  - class members -> struct fields; text stored in fixed char buffers.
 *  - std::function<void()> callback -> widget_list_item_callback_t + user_data.
 *  - const lv_font_t* kept as a non-owning pointer (fonts are static const).
 */
#ifndef WIDGETS_LIST_ITEM_H_
#define WIDGETS_LIST_ITEM_H_

#include <stdint.h>
#include <stdbool.h>
#include "../include/rawdraw.h"
#include "../include/font_engine.h"
#include "../include/rawdraw_ext.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Tap callback invoked by widget_list_item_handle_tap(). */
typedef void (*widget_list_item_callback_t)(void *user_data);

/** Capacity of the inline label buffer. */
#define WIDGET_LIST_ITEM_LABEL_LEN 64
/** Capacity of the inline value buffer. */
#define WIDGET_LIST_ITEM_VALUE_LEN 64
/** Capacity of the inline icon-code buffer (UTF-8 glyph code). */
#define WIDGET_LIST_ITEM_ICON_LEN 16

typedef struct {
    int x, y, w, h;
    int padding;

    char             label[WIDGET_LIST_ITEM_LABEL_LEN]; /* "" = no label */
    const lv_font_t *label_font;

    char             value[WIDGET_LIST_ITEM_VALUE_LEN]; /* "" = no value */
    const lv_font_t *value_font;

    char             icon_code[WIDGET_LIST_ITEM_ICON_LEN]; /* "" = no icon */
    const lv_font_t *icon_font;

    bool show_chevron;
    bool show_separator;
    bool pressed;

    widget_list_item_callback_t callback;
    void                       *callback_user_data;

    rawdraw_color_t bg_color;
    rawdraw_color_t text_color;
    rawdraw_color_t value_text_color;
    rawdraw_color_t separator_color;
    bool            custom_colors;
} widget_list_item_t;

/* ---- lifecycle ---- */
void widget_list_item_init(widget_list_item_t *item, int x, int y, int w, int h);

/* ---- configuration ---- */
void widget_list_item_set_bounds(widget_list_item_t *item, int x, int y, int w, int h);
void widget_list_item_set_label(widget_list_item_t *item, const char *label);
void widget_list_item_set_label_font(widget_list_item_t *item, const lv_font_t *font);
void widget_list_item_set_value(widget_list_item_t *item, const char *value);
void widget_list_item_set_value_font(widget_list_item_t *item, const lv_font_t *font);
void widget_list_item_set_icon(widget_list_item_t *item, const char *icon_code);
void widget_list_item_set_icon_font(widget_list_item_t *item, const lv_font_t *font);
void widget_list_item_set_show_chevron(widget_list_item_t *item, bool show);
void widget_list_item_set_show_separator(widget_list_item_t *item, bool show);
void widget_list_item_set_padding(widget_list_item_t *item, int padding);
void widget_list_item_set_callback(widget_list_item_t *item, widget_list_item_callback_t cb, void *user_data);
void widget_list_item_set_colors(widget_list_item_t *item, rawdraw_color_t bg, rawdraw_color_t text,
                                 rawdraw_color_t value_text, rawdraw_color_t separator);

/* ---- state / input ---- */
bool widget_list_item_contains(const widget_list_item_t *item, int px, int py);
void widget_list_item_set_pressed(widget_list_item_t *item, bool pressed);
bool widget_list_item_is_pressed(const widget_list_item_t *item);
void widget_list_item_handle_tap(widget_list_item_t *item);
bool widget_list_item_handle_input(widget_list_item_t *item, const ui_button_event_t *event);

/* ---- geometry / rendering ---- */
rawdraw_rect_t widget_list_item_get_bounds(const widget_list_item_t *item);
void           widget_list_item_render(const widget_list_item_t *item, uint8_t *fb, int fb_width, int fb_height);

#ifdef __cplusplus
}
#endif

#endif /* WIDGETS_LIST_ITEM_H_ */
