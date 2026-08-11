#ifndef COMPONENTS_RAWDRAW_INCLUDE_RAWDRAW_EXT_H_
#define COMPONENTS_RAWDRAW_INCLUDE_RAWDRAW_EXT_H_

#include <stdint.h>
#include <stdbool.h>
#include "rawdraw.h"
#include "rawdraw_util.h"
#include "font_engine.h"

/* ------------------------------------------------------------------ */
/* Button input events (shared by UI pages and interactive widgets)    */
/* ------------------------------------------------------------------ */

typedef enum {
    BTN_UP_CLICK = 0,
    BTN_DOWN_CLICK,
    BTN_UP_DOUBLE_CLICK,
    BTN_DOWN_DOUBLE_CLICK,
    BTN_UP_LONG_PRESS,
    BTN_DOWN_LONG_PRESS,
    BTN_BOOT_CLICK,
    BTN_BOOT_DOUBLE_CLICK,
    BTN_BOOT_LONG_PRESS,
} button_event_type_t;

typedef struct {
    button_event_type_t type;
} ui_button_event_t;

void rawdraw_draw_rect_border(uint8_t *fb, int width, int height, rawdraw_rect_t r, int thickness,
                              rawdraw_color_t color);
void rawdraw_draw_round_rect_border(uint8_t *fb, int width, int height, rawdraw_rect_t r, int radius, int thickness,
                                    rawdraw_color_t color);
void rawdraw_draw_hline(uint8_t *fb, int width, int height, int y, int x1, int x2, rawdraw_color_t color);
void rawdraw_draw_vline(uint8_t *fb, int width, int height, int x, int y1, int y2, rawdraw_color_t color);
void rawdraw_draw_line(uint8_t *fb, int width, int height, rawdraw_point_t p1, rawdraw_point_t p2,
                       rawdraw_color_t color);
void rawdraw_draw_circle(uint8_t *fb, int width, int height, rawdraw_point_t center, int radius, rawdraw_color_t color);
void rawdraw_draw_circle_border(uint8_t *fb, int width, int height, rawdraw_point_t center, int radius, int thickness,
                                rawdraw_color_t color);
void rawdraw_draw_progress(uint8_t *fb, int width, int height, rawdraw_rect_t r, int value_pct,
                           rawdraw_color_t bg_color, rawdraw_color_t fg_color, int radius);
void rawdraw_draw_progress_with_label(uint8_t *fb, int width, int height, int x, int y, int w, int h, int value_pct,
                                      const char *label, const lv_font_t *font);
int  rawdraw_measure_text_width(const char *text, const lv_font_t *font);
int  rawdraw_measure_text_height(const lv_font_t *font);
rawdraw_rect_t rawdraw_measure_text_bounds(const char *text, const lv_font_t *font, int max_width);
void           rawdraw_fill_rect(uint8_t *fb, int width, int height, rawdraw_rect_t r, rawdraw_color_t color);
void           rawdraw_invert_region(uint8_t *fb, int width, int height, rawdraw_rect_t r);
void rawdraw_copy_region(const uint8_t *src, uint8_t *dst, int width, int height, rawdraw_rect_t src_r, int dst_x,
                         int dst_y);
/* Rotate a 2bpp source (sw x sh) by 90° into dst. Source pixel (sx, sy) maps to
 * dst pixel (dst_x + sh-1-sy, dst_y + sx); the rotated footprint is sh wide and
 * sw tall. Batched 2bpp bit ops, no per-pixel get/set helpers. */
void rawdraw_blit_rotated_90(const uint8_t *src, int sw, int sh, uint8_t *dst, int dw, int dh, int dst_x, int dst_y);
void rawdraw_clear(uint8_t *fb, int width, int height, rawdraw_color_t fill);
void rawdraw_draw_stripe_rect(uint8_t *fb, int width, int height, rawdraw_rect_t r);
rawdraw_color_t rawdraw_get_pixel(const uint8_t *fb, int width, int height, int x, int y);
bool            rawdraw_point_in_rounded_rect(int px, int py, rawdraw_rect_t r, int radius);

rawdraw_rect_t rawdraw_align_x8(rawdraw_rect_t r);
rawdraw_rect_t rawdraw_clamp_rect(rawdraw_rect_t r, int width, int height);
rawdraw_rect_t rawdraw_rect_union(rawdraw_rect_t a, rawdraw_rect_t b);
int            rawdraw_rect_area(rawdraw_rect_t r);

#endif // COMPONENTS_RAWDRAW_INCLUDE_RAWDRAW_EXT_H_
