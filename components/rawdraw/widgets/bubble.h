/**
 * @file bubble.h
 * @brief Chat bubble component for dialogue UI.
 */
#ifndef WIDGETS_BUBBLE_H_
#define WIDGETS_BUBBLE_H_

#include <stdint.h>
#include <stdbool.h>
#include "../include/rawdraw.h"
#include "../include/font_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIDGET_BUBBLE_ALIGN_LEFT,
    WIDGET_BUBBLE_ALIGN_RIGHT,
    WIDGET_BUBBLE_ALIGN_CENTER
} widget_bubble_align_t;

#define WIDGET_BUBBLE_TEXT_LEN 1024

typedef struct {
    widget_bubble_align_t align;
    int                   margin;
    int                   max_width;
    int                   radius;
    const lv_font_t      *font;
    int                   line_spacing;
    int                   padding;

    char text[WIDGET_BUBBLE_TEXT_LEN];
    int  y;

    rawdraw_color_t fill_color;
    rawdraw_color_t text_color;
    rawdraw_color_t border_color;
    int             border_width;
    bool            custom_colors;
} widget_bubble_t;

/* ---- lifecycle ---- */
void widget_bubble_init(widget_bubble_t *bubble, widget_bubble_align_t align, int margin, int max_width, int radius);

/* ---- configuration ---- */
void widget_bubble_set_align(widget_bubble_t *bubble, widget_bubble_align_t align);
void widget_bubble_set_margin(widget_bubble_t *bubble, int margin);
void widget_bubble_set_max_width(widget_bubble_t *bubble, int max_width);
void widget_bubble_set_radius(widget_bubble_t *bubble, int radius);
void widget_bubble_set_font(widget_bubble_t *bubble, const lv_font_t *font);
void widget_bubble_set_line_spacing(widget_bubble_t *bubble, int spacing);
void widget_bubble_set_padding(widget_bubble_t *bubble, int padding);
void widget_bubble_set_colors(widget_bubble_t *bubble, rawdraw_color_t fill, rawdraw_color_t text,
                              rawdraw_color_t border, int border_width);

/* ---- content ---- */
void        widget_bubble_set_text(widget_bubble_t *bubble, const char *text);
void        widget_bubble_append_text(widget_bubble_t *bubble, const char *chunk);
void        widget_bubble_clear(widget_bubble_t *bubble);
const char *widget_bubble_get_text(const widget_bubble_t *bubble);
bool        widget_bubble_has_content(const widget_bubble_t *bubble);

/* ---- layout ---- */
void           widget_bubble_set_y(widget_bubble_t *bubble, int y);
rawdraw_rect_t widget_bubble_get_bounds(const widget_bubble_t *bubble, int screen_width);
int            widget_bubble_calculate_height(const widget_bubble_t *bubble);
int            widget_bubble_calculate_width(const widget_bubble_t *bubble);

/* ---- rendering ---- */
void widget_bubble_render(widget_bubble_t *bubble, uint8_t *fb, int width, int height);

#ifdef __cplusplus
}
#endif

#endif /* WIDGETS_BUBBLE_H_ */
