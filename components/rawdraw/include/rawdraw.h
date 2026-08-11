#ifndef MAIN_RAWDRAW_H_
#define MAIN_RAWDRAW_H_

#include <stdint.h>
#include <stdbool.h>
#include "font_engine.h"

typedef enum {
    RAWDRAW_COLOR_BLACK  = 0,
    RAWDRAW_COLOR_WHITE  = 1,
    RAWDRAW_COLOR_YELLOW = 2,
    RAWDRAW_COLOR_RED    = 3,
} rawdraw_color_t;

typedef struct {
    int x, y, w, h;
} rawdraw_rect_t;
typedef struct {
    int x, y;
} rawdraw_point_t;

void               rawdraw_set_pixel(uint8_t *fb, int width, int height, int x, int y, int color);

/* Convert a 2bpp color to its packed fill-byte (4 identical pixels per byte). */
static inline uint8_t rd_color_to_fill_byte(int color)
{
    return (uint8_t)((color & 0x03) * 0x55);
}

/* Fill a horizontal run of pixels on a single scanline using memset for the
 * aligned interior and set_pixel_unchecked only for sub-byte edges.  This is
 * the kernel-style cfb_fillrect approach: batch writes to exploit PSRAM cache
 * line prefetching instead of per-pixel read-modify-write. */
void rawdraw_fill_scanline_segment(uint8_t *fb, int fb_width, int fb_height,
                                   int y, int x_start, int x_end, int color);

static inline void rawdraw_set_pixel_unchecked(uint8_t *fb, int width, int height, int x, int y, int color)
{
    (void)height; /* Caller guarantees bounds; height retained for API symmetry */
    uint16_t bytes_per_row = (uint16_t)((width * 2 + 7) >> 3);
    uint32_t index         = (uint32_t)y * bytes_per_row + (uint32_t)(x >> 2);
    uint8_t  shift         = (uint8_t)(6 - ((x & 0x03) << 1));
    uint8_t  mask          = (uint8_t)(0x03U << shift);
    fb[index]              = (uint8_t)((fb[index] & (uint8_t)~mask) | ((uint8_t)(color & 0x03) << shift));
}
void rawdraw_draw_rect(uint8_t *fb, int w, int h, int rx, int ry, int rw, int rh, int color);
void rawdraw_draw_dither_rect(uint8_t *fb, int w, int h, int rx, int ry, int rw, int rh);
void rawdraw_draw_round_rect(uint8_t *fb, int w, int h, int rx, int ry, int rw, int rh, int radius, int fill_color,
                             int border_color, int thickness);
void rawdraw_draw_text(uint8_t *fb, int w, int h, int x, int y, const char *text, const lv_font_t *font, int color);

#endif // MAIN_RAWDRAW_H_
