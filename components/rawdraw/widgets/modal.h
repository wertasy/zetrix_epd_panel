/**
 * @file modal.h
 * @brief Centered modal container for rawdraw overlays.
 */
#ifndef WIDGETS_MODAL_H_
#define WIDGETS_MODAL_H_

#include <stdint.h>
#include <stdbool.h>
#include "../include/rawdraw.h"
#include "../include/font_engine.h"
#include "../include/style.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIDGET_MODAL_TITLE_LEN 64
#define WIDGET_MODAL_FOOTER_LEN 64

typedef struct {
    rawdraw_rect_t bounds;
    char title[WIDGET_MODAL_TITLE_LEN];
    char footer[WIDGET_MODAL_FOOTER_LEN];
    const lv_font_t *title_font;
    int radius;
    int border_width;
} widget_modal_t;

/* ---- lifecycle ---- */
void widget_modal_init(widget_modal_t *modal);

/* ---- configuration ---- */
void widget_modal_set_bounds(widget_modal_t *modal, rawdraw_rect_t bounds);
void widget_modal_set_bounds_xy(widget_modal_t *modal, int x, int y, int w, int h);
void widget_modal_center_in_screen(widget_modal_t *modal, int screen_width, int screen_height, int inset);
void widget_modal_set_title(widget_modal_t *modal, const char *title);
void widget_modal_set_title_font(widget_modal_t *modal, const lv_font_t *font);
void widget_modal_set_footer(widget_modal_t *modal, const char *footer);
void widget_modal_set_radius(widget_modal_t *modal, int radius);
void widget_modal_set_border_width(widget_modal_t *modal, int border_width);

/* ---- geometry ---- */
rawdraw_rect_t widget_modal_get_bounds(const widget_modal_t *modal);
rawdraw_rect_t widget_modal_get_title_bounds(const widget_modal_t *modal);
rawdraw_rect_t widget_modal_get_content_bounds(const widget_modal_t *modal);
rawdraw_rect_t widget_modal_get_footer_bounds(const widget_modal_t *modal);

/* ---- rendering ---- */
void widget_modal_render(const widget_modal_t *modal, uint8_t *fb, int width, int height);

#ifdef __cplusplus
}
#endif

#endif /* WIDGETS_MODAL_H_ */
