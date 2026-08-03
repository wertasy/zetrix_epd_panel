#ifndef MAIN_RAWDRAW_H_
#define MAIN_RAWDRAW_H_

#include <stdint.h>
#include <stdbool.h>
#include "font_engine.h"

typedef enum {
    RAWDRAW_COLOR_BLACK = 0,
    RAWDRAW_COLOR_WHITE = 1,
    RAWDRAW_COLOR_YELLOW = 2,
    RAWDRAW_COLOR_RED = 3,
} rawdraw_color_t;

void rawdraw_set_pixel(uint8_t* fb, int width, int height, int x, int y, int color);
void rawdraw_draw_rect(uint8_t* fb, int w, int h, int rx, int ry, int rw, int rh, int color);
void rawdraw_draw_dither_rect(uint8_t* fb, int w, int h, int rx, int ry, int rw, int rh);
void rawdraw_draw_round_rect(uint8_t* fb, int w, int h, int rx, int ry, int rw, int rh, int radius, int fill_color, int border_color, int thickness);
void rawdraw_draw_text(uint8_t* fb, int w, int h, int x, int y, const char* text, const lv_font_t* font, int color);

#endif // MAIN_RAWDRAW_H_
