/**
 * @file card.h
 * @brief Card container widget — C port of rawdraw::Card.
 *
 * A rounded container for grouping related content. Supports an optional
 * shadow (offset filled rect drawn behind), an optional title bar with a
 * separator line, and configurable content padding.
 *
 * All draw functions take (fb, fb_width, fb_height) — no global state.
 */
#ifndef WIDGETS_CARD_H_
#define WIDGETS_CARD_H_

#include <stdint.h>
#include <stdbool.h>
#include "../include/rawdraw.h"
#include "../include/font_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Capacity of the inline title buffer. */
#define WIDGET_CARD_TITLE_LEN 64

typedef struct {
    rawdraw_rect_t bounds;
    int radius;
    int border_width;
    int padding;

    char title[WIDGET_CARD_TITLE_LEN]; /* "" = no title bar text */
    const lv_font_t *title_font;
    int title_height; /* 0 = auto */
    bool title_enabled;

    bool shadow_enabled;
    int shadow_offset;

    rawdraw_color_t bg_color;
    rawdraw_color_t border_color;
    rawdraw_color_t title_bg_color;
    rawdraw_color_t title_text_color;
    rawdraw_color_t shadow_color;
    bool custom_colors;
} widget_card_t;

/* ---- lifecycle ---- */
void widget_card_init(widget_card_t *card, int x, int y, int w, int h, int radius);

/* ---- configuration ---- */
void widget_card_set_bounds(widget_card_t *card, int x, int y, int w, int h);
void widget_card_set_radius(widget_card_t *card, int radius);
void widget_card_set_border_width(widget_card_t *card, int width);
void widget_card_set_padding(widget_card_t *card, int padding);
void widget_card_set_title(widget_card_t *card, const char *title);
void widget_card_set_title_font(widget_card_t *card, const lv_font_t *font);
void widget_card_set_title_height(widget_card_t *card, int height);
void widget_card_set_title_enabled(widget_card_t *card, bool enabled);
void widget_card_set_shadow_enabled(widget_card_t *card, bool enabled);
void widget_card_set_shadow_offset(widget_card_t *card, int offset);
void widget_card_set_colors(widget_card_t *card, rawdraw_color_t bg, rawdraw_color_t border);
void widget_card_set_title_colors(widget_card_t *card, rawdraw_color_t bg, rawdraw_color_t text);
void widget_card_set_shadow_color(widget_card_t *card, rawdraw_color_t color);

/* ---- layout ---- */
rawdraw_rect_t widget_card_get_bounds(const widget_card_t *card);
rawdraw_rect_t widget_card_get_title_bounds(const widget_card_t *card);
rawdraw_rect_t widget_card_get_content_bounds(const widget_card_t *card);
int widget_card_calculate_title_height(const widget_card_t *card);

/* ---- rendering ---- */
void widget_card_render(const widget_card_t *card, uint8_t *fb, int fb_width, int fb_height);

#ifdef __cplusplus
}
#endif

#endif /* WIDGETS_CARD_H_ */
