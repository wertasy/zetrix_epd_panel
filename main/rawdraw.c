#include "rawdraw.h"
#include <string.h>

#define CLAMP(x, min, max) ((x) < (min) ? (min) : ((x) > (max) ? (max) : (x)))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

void rawdraw_set_pixel(uint8_t* fb, int width, int height, int x, int y, int color) {
    if (!fb || x < 0 || y < 0 || x >= width || y >= height) return;

    uint16_t bytes_per_row = (width * 2 + 7) >> 3;
    uint32_t index = (uint32_t)y * bytes_per_row + (uint32_t)(x >> 2);
    uint8_t shift = (uint8_t)(6 - ((x & 0x03) << 1));
    uint8_t mask = (uint8_t)(0x03U << shift);
    fb[index] = (uint8_t)((fb[index] & (uint8_t)~mask) | ((uint8_t)(color & 0x03) << shift));
}

void rawdraw_draw_rect(uint8_t* fb, int w, int h, int rx, int ry, int rw, int rh, int color) {
    if (!fb || rw <= 0 || rh <= 0) return;

    for (int y = ry; y < ry + rh; y++) {
        for (int x = rx; x < rx + rw; x++) {
            rawdraw_set_pixel(fb, w, h, x, y, color);
        }
    }
}

void rawdraw_draw_dither_rect(uint8_t* fb, int w, int h, int rx, int ry, int rw, int rh) {
    if (!fb || rw <= 0 || rh <= 0) return;

    for (int y = ry; y < ry + rh; y++) {
        for (int x = rx; x < rx + rw; x++) {
            int color = ((x + y) & 1) ? RAWDRAW_COLOR_WHITE : RAWDRAW_COLOR_BLACK;
            rawdraw_set_pixel(fb, w, h, x, y, color);
        }
    }
}

static bool point_in_rounded_rect(int px, int py, int rx, int ry, int rw, int rh, int radius) {
    if (px < rx || py < ry || px >= rx + rw || py >= ry + rh) {
        return false;
    }
    if (radius <= 0) {
        return true;
    }

    const int left = rx + radius;
    const int right = rx + rw - 1 - radius;
    const int top = ry + radius;
    const int bottom = ry + rh - 1 - radius;

    if ((px >= left && px <= right) || (py >= top && py <= bottom)) {
        return true;
    }

    const int cx = CLAMP(px, left, right);
    const int cy = CLAMP(py, top, bottom);
    const int dx = px - cx;
    const int dy = py - cy;
    return dx * dx + dy * dy <= radius * radius;
}

void rawdraw_draw_round_rect(uint8_t* fb, int w, int h, int rx, int ry, int rw, int rh, int radius, int fill_color, int border_color, int thickness) {
    if (!fb || rw <= 0 || rh <= 0) return;

    int max_radius = MIN(rw, rh) / 2;
    radius = MIN(radius, max_radius);
    if (radius < 0) radius = 0;

    for (int y = ry; y < ry + rh; y++) {
        for (int x = rx; x < rx + rw; x++) {
            if (point_in_rounded_rect(x, y, rx, ry, rw, rh, radius)) {
                if (thickness > 0) {
                    int inner_rx = rx + thickness;
                    int inner_ry = ry + thickness;
                    int inner_rw = rw - thickness * 2;
                    int inner_rh = rh - thickness * 2;
                    int inner_radius = radius - thickness;
                    if (inner_radius < 0) inner_radius = 0;

                    if (inner_rw > 0 && inner_rh > 0 && point_in_rounded_rect(x, y, inner_rx, inner_ry, inner_rw, inner_rh, inner_radius)) {
                        rawdraw_set_pixel(fb, w, h, x, y, fill_color);
                    } else {
                        rawdraw_set_pixel(fb, w, h, x, y, border_color);
                    }
                } else {
                    rawdraw_set_pixel(fb, w, h, x, y, fill_color);
                }
            }
        }
    }
}

void rawdraw_draw_text(uint8_t* fb, int w, int h, int x, int y, const char* text, const lv_font_t* font, int color) {
    if (!fb || !text || !font) return;

    int cursor_x = x;
    int cursor_y = y;
    const char* p = text;

    while (*p) {
        uint32_t ch = utf8_next(&p);
        if (ch == 0) break;

        if (ch == '\n') {
            cursor_x = x;
            cursor_y += font->line_height;
            continue;
        }

        lv_font_glyph_dsc_t g = {0};
        g.resolved_font = font;
        if (!lv_font_get_glyph_dsc(font, &g, ch, 0)) {
            cursor_x += font->line_height / 2;
            continue;
        }

        g.req_raw_bitmap = 1;
        const uint8_t* bitmap = (const uint8_t*)lv_font_get_glyph_bitmap(&g, NULL);
        g.req_raw_bitmap = 0;

        if (!bitmap) {
            cursor_x += g.adv_w;
            continue;
        }

        int gx = cursor_x + g.ofs_x;
        int gy = cursor_y + font->line_height - font->base_line - g.ofs_y - g.box_h;
        int row_bits = (g.stride > 0) ? (int)(g.stride * 8) : (int)g.box_w;

        for (int row = 0; row < (int)g.box_h; row++) {
            for (int col = 0; col < (int)g.box_w; col++) {
                int bit_idx = row * row_bits + col;
                bool pixel = (bitmap[bit_idx >> 3] >> (7 - (bit_idx & 7))) & 1;

                if (pixel) {
                    int px = gx + col;
                    int py = gy + row;
                    if (px >= 0 && px < w && py >= 0 && py < h) {
                        rawdraw_set_pixel(fb, w, h, px, py, color);
                    }
                }
            }
        }

        cursor_x += g.adv_w;
    }
}
