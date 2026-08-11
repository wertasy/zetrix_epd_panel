/**
 * @file panel.h
 * @brief Panel container widget — C port of rawdraw::Panel.
 *
 * A bordered, optionally rounded container with an optional title bar and a
 * content area below it. Child components are placed using
 * widget_panel_get_content_bounds().
 *
 * All draw functions take (fb, fb_width, fb_height) — no global state.
 */
#ifndef WIDGETS_PANEL_H_
#define WIDGETS_PANEL_H_

#include <stdint.h>
#include <stdbool.h>
#include "../include/rawdraw.h"
#include "../include/font_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Capacity of the inline title buffer. */
#define WIDGET_PANEL_TITLE_LEN 64

typedef struct {
    rawdraw_rect_t bounds;
    int            radius;

    char             title[WIDGET_PANEL_TITLE_LEN]; /* "" = no title bar text */
    const lv_font_t *title_font;
    int              title_height; /* 0 = auto (font line_height + 2*padding) */

    int  padding;
    int  border_width;
    bool title_enabled;

    rawdraw_color_t bg_color;
    rawdraw_color_t border_color;
    rawdraw_color_t title_bg_color;
    rawdraw_color_t title_text_color;
} widget_panel_t;

/* ---- lifecycle ---- */
void widget_panel_init(widget_panel_t *panel, int x, int y, int w, int h, int radius);

/* ---- configuration ---- */
void widget_panel_set_bounds(widget_panel_t *panel, int x, int y, int w, int h);
void widget_panel_set_radius(widget_panel_t *panel, int radius);
void widget_panel_set_title(widget_panel_t *panel, const char *title);
void widget_panel_set_title_font(widget_panel_t *panel, const lv_font_t *font);
void widget_panel_set_title_height(widget_panel_t *panel, int height);
void widget_panel_set_padding(widget_panel_t *panel, int padding);
void widget_panel_set_border_width(widget_panel_t *panel, int width);
void widget_panel_set_title_enabled(widget_panel_t *panel, bool enabled);
void widget_panel_set_colors(widget_panel_t *panel, rawdraw_color_t bg, rawdraw_color_t border);
void widget_panel_set_title_colors(widget_panel_t *panel, rawdraw_color_t bg, rawdraw_color_t text);

/* ---- layout ---- */
rawdraw_rect_t widget_panel_get_bounds(const widget_panel_t *panel);
rawdraw_rect_t widget_panel_get_title_bounds(const widget_panel_t *panel);
rawdraw_rect_t widget_panel_get_content_bounds(const widget_panel_t *panel);
int            widget_panel_calculate_title_height(const widget_panel_t *panel);

/* ---- rendering ---- */
void widget_panel_render(const widget_panel_t *panel, uint8_t *fb, int fb_width, int fb_height);

#ifdef __cplusplus
}
#endif

#endif /* WIDGETS_PANEL_H_ */
