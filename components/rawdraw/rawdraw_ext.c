#include "rawdraw_ext.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "rawdraw_util.h"

static inline void set_pixel(uint8_t *fb, int width, int height, int x, int y, rawdraw_color_t color)
{
    rawdraw_set_pixel_unchecked(fb, width, height, x, y, (int)color);
}

int rawdraw_rect_area(rawdraw_rect_t r)
{
    return display_rect_area(r);
}

rawdraw_rect_t rawdraw_clamp_rect(rawdraw_rect_t r, int width, int height)
{
    int x1 = RD_MAX(0, r.x);
    int y1 = RD_MAX(0, r.y);
    int x2 = RD_MIN(width, r.x + r.w);
    int y2 = RD_MIN(height, r.y + r.h);
    rawdraw_rect_t out = {x1, y1, RD_MAX(0, x2 - x1), RD_MAX(0, y2 - y1)};
    return out;
}

rawdraw_rect_t rawdraw_align_x8(rawdraw_rect_t r)
{
    return display_align_x8(r);
}

rawdraw_rect_t rawdraw_rect_union(rawdraw_rect_t a, rawdraw_rect_t b)
{
    return display_rect_union(a, b);
}

bool rawdraw_point_in_rounded_rect(int px, int py, rawdraw_rect_t r, int radius)
{
    if (px < r.x || py < r.y || px >= r.x + r.w || py >= r.y + r.h) {
        return false;
    }
    if (radius <= 0) {
        return true;
    }

    const int left = r.x + radius;
    const int right = r.x + r.w - 1 - radius;
    const int top = r.y + radius;
    const int bottom = r.y + r.h - 1 - radius;

    if ((px >= left && px <= right) || (py >= top && py <= bottom)) {
        return true;
    }

    const int cx = RD_CLAMP(px, left, right);
    const int cy = RD_CLAMP(py, top, bottom);
    const int dx = px - cx;
    const int dy = py - cy;
    return dx * dx + dy * dy <= radius * radius;
}

static inline uint8_t ColorToFillByte(rawdraw_color_t color)
{
    switch (color) {
    case RAWDRAW_COLOR_BLACK:
        return 0x00;
    case RAWDRAW_COLOR_WHITE:
        return 0x55;
    case RAWDRAW_COLOR_YELLOW:
        return 0xAA;
    case RAWDRAW_COLOR_RED:
        return 0xFF;
    default:
        return 0x55;
    }
}

void rawdraw_draw_rect_border(uint8_t *fb, int width, int height, rawdraw_rect_t r, int thickness,
                              rawdraw_color_t color)
{
    if (!fb || r.w <= 0 || r.h <= 0 || thickness <= 0)
        return;

    for (int t = 0; t < thickness; t++) {
        rawdraw_draw_hline(fb, width, height, r.y + t, r.x, r.x + r.w - 1, color);
        rawdraw_draw_hline(fb, width, height, r.y + r.h - 1 - t, r.x, r.x + r.w - 1, color);
    }

    int y1 = r.y + thickness;
    int y2 = r.y + r.h - thickness - 1;
    if (y1 <= y2) {
        for (int t = 0; t < thickness; t++) {
            rawdraw_draw_vline(fb, width, height, r.x + t, y1, y2, color);
            rawdraw_draw_vline(fb, width, height, r.x + r.w - 1 - t, y1, y2, color);
        }
    }
}

void rawdraw_draw_round_rect_border(uint8_t *fb, int width, int height, rawdraw_rect_t r, int radius, int thickness,
                                    rawdraw_color_t color)
{
    if (!fb || r.w <= 0 || r.h <= 0 || thickness <= 0 || height <= 0)
        return;

    int max_radius = RD_MIN(r.w, r.h) / 2;
    radius = RD_MIN(radius, max_radius);
    if (radius < 0)
        radius = 0;

    rawdraw_rect_t clipped = rawdraw_clamp_rect(r, width, height);
    rawdraw_rect_t inner = {r.x + thickness, r.y + thickness, r.w - thickness * 2, r.h - thickness * 2};
    int inner_radius = RD_MAX(0, radius - thickness);

    for (int y = clipped.y; y < clipped.y + clipped.h; ++y) {
        for (int x = clipped.x; x < clipped.x + clipped.w; ++x) {
            if (!rawdraw_point_in_rounded_rect(x, y, r, radius))
                continue;
            if (inner.w > 0 && inner.h > 0 && rawdraw_point_in_rounded_rect(x, y, inner, inner_radius))
                continue;
            set_pixel(fb, width, height, x, y, color);
        }
    }
}

void rawdraw_draw_hline(uint8_t *fb, int width, int height, int y, int x1, int x2, rawdraw_color_t color)
{
    if (!fb)
        return;
    if (x1 > x2) {
        int tmp = x1;
        x1 = x2;
        x2 = tmp;
    }
    /* P0: Use memset scanline instead of per-pixel set_pixel */
    rawdraw_fill_scanline_segment(fb, width, height, y, x1, x2 + 1, (int)color);
}

void rawdraw_draw_vline(uint8_t *fb, int width, int height, int x, int y1, int y2, rawdraw_color_t color)
{
    if (!fb)
        return;
    if (y1 > y2) {
        int tmp = y1;
        y1 = y2;
        y2 = tmp;
    }
    for (int y = y1; y <= y2; y++)
        rawdraw_set_pixel_unchecked(fb, width, height, x, y, (int)color);
}

void rawdraw_draw_line(uint8_t *fb, int width, int height, rawdraw_point_t p1, rawdraw_point_t p2,
                       rawdraw_color_t color)
{
    if (!fb)
        return;

    int dx = abs(p2.x - p1.x);
    int dy = abs(p2.y - p1.y);
    int sx = (p1.x < p2.x) ? 1 : -1;
    int sy = (p1.y < p2.y) ? 1 : -1;
    int err = dx - dy;

    int x = p1.x, y = p1.y;

    while (true) {
        set_pixel(fb, width, height, x, y, color);

        if (x == p2.x && y == p2.y)
            break;

        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x += sx;
        }
        if (e2 < dx) {
            err += dx;
            y += sy;
        }
    }
}

void rawdraw_draw_circle(uint8_t *fb, int width, int height, rawdraw_point_t center, int radius, rawdraw_color_t color)
{
    if (!fb || radius <= 0)
        return;

    int cx = center.x, cy = center.y;

    int y_start = RD_MAX(-radius, -cy);
    int y_end = RD_MIN(radius, height - 1 - cy);
    int x_start = RD_MAX(-radius, -cx);
    int x_end = RD_MIN(radius, width - 1 - cx);

    for (int y = y_start; y <= y_end; y++) {
        for (int x = x_start; x <= x_end; x++) {
            if (x * x + y * y <= radius * radius) {
                set_pixel(fb, width, height, cx + x, cy + y, color);
            }
        }
    }
}

void rawdraw_draw_circle_border(uint8_t *fb, int width, int height, rawdraw_point_t center, int radius, int thickness,
                                rawdraw_color_t color)
{
    if (!fb || radius <= 0)
        return;

    for (int r = radius - thickness + 1; r <= radius; r++) {
        if (r <= 0)
            continue;

        int cx = center.x, cy = center.y;
        int x = r, y = 0;
        int err = 1 - r;

        while (x >= y) {
            set_pixel(fb, width, height, cx + x, cy + y, color);
            set_pixel(fb, width, height, cx + y, cy + x, color);
            set_pixel(fb, width, height, cx - y, cy + x, color);
            set_pixel(fb, width, height, cx - x, cy + y, color);
            set_pixel(fb, width, height, cx - x, cy - y, color);
            set_pixel(fb, width, height, cx - y, cy - x, color);
            set_pixel(fb, width, height, cx + y, cy - x, color);
            set_pixel(fb, width, height, cx + x, cy - y, color);

            y++;
            if (err < 0) {
                err += 2 * y + 1;
            } else {
                x--;
                err += 2 * (y - x) + 1;
            }
        }
    }
}

void rawdraw_draw_progress(uint8_t *fb, int width, int height, rawdraw_rect_t r, int value_pct,
                           rawdraw_color_t bg_color, rawdraw_color_t fg_color, int radius)
{
    if (!fb || r.w <= 0 || r.h <= 0)
        return;

    value_pct = RD_CLAMP(value_pct, 0, 100);

    if (radius < 0) {
        radius = r.h / 2;
    }

    int max_radius = RD_MIN(r.w, r.h) / 2;
    radius = RD_MIN(radius, max_radius);

    rawdraw_draw_round_rect(fb, width, height, r.x, r.y, r.w, r.h, radius, (int)bg_color, (int)bg_color, 0);

    if (value_pct > 0) {
        int fg_w = (r.w * value_pct) / 100;
        if (fg_w > 0) {
            rawdraw_draw_round_rect(fb, width, height, r.x, r.y, fg_w, r.h, radius, (int)fg_color, (int)fg_color, 0);
        }
    }
}

void rawdraw_draw_progress_with_label(uint8_t *fb, int width, int height, int x, int y, int w, int h, int value_pct,
                                      const char *label, const lv_font_t *font)
{
    rawdraw_rect_t r = {x, y, w, h};
    rawdraw_draw_progress(fb, width, height, r, value_pct, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_BLACK, -1);

    if (label && font) {
        int text_w = rawdraw_measure_text_width(label, font);
        int text_h = font->line_height;
        int label_x = x + (w - text_w) / 2;
        int label_y = y + (h - text_h) / 2;

        int progress_x = x + (w * value_pct) / 100;

        int cursor_x = label_x;
        int cursor_y = label_y;
        const char *p = label;

        const int letter_spacing = (font->line_height + 8) / 16;
        const int space_width = font->line_height / 4;

        while (*p) {
            const char *char_start = p;
            uint32_t ch = utf8_next(&p);
            if (ch == 0)
                break;

            if (ch == '\n') {
                cursor_x = label_x;
                cursor_y += font->line_height;
                continue;
            }

            int char_w = 0;
            if (ch == ' ') {
                char_w = space_width;
            } else {
                lv_font_glyph_dsc_t g = {0};
                g.resolved_font = font;
                if (!lv_font_get_glyph_dsc(font, &g, ch, 0)) {
                    char_w = font->line_height / 2;
                } else {
                    if (ch >= 0x20 && ch <= 0x7E) {
                        int prop_adv = (int)g.box_w + (int)g.ofs_x + letter_spacing;
                        if (prop_adv < 2)
                            prop_adv = 2;
                        char_w = prop_adv;
                    } else {
                        char_w = g.adv_w;
                    }
                }
            }

            // Choose white if the character's center position is less than the progress threshold X, black otherwise.
            int char_center = cursor_x + char_w / 2;
            rawdraw_color_t color = (char_center < progress_x) ? RAWDRAW_COLOR_WHITE : RAWDRAW_COLOR_BLACK;

            if (ch != ' ') {
                int char_len = p - char_start;
                char tmp[8];
                if (char_len > 0 && char_len < (int)sizeof(tmp)) {
                    memcpy(tmp, char_start, char_len);
                    tmp[char_len] = '\0';
                    rawdraw_draw_text(fb, width, height, cursor_x, cursor_y, tmp, font, (int)color);
                }
            }

            cursor_x += char_w;
        }
    }
}

int rawdraw_measure_text_width(const char *text, const lv_font_t *font)
{
    if (!text || !font)
        return 0;

    int width = 0;
    const char *p = text;

    const int letter_spacing = (font->line_height + 8) / 16;
    const int space_width = font->line_height / 4;

    while (*p) {
        uint32_t ch = utf8_next(&p);
        if (ch == 0)
            break;

        if (ch == '\n') {
            continue;
        }

        if (ch == ' ') {
            width += space_width;
            continue;
        }

        lv_font_glyph_dsc_t g = {0};
        g.resolved_font = font;
        if (lv_font_get_glyph_dsc(font, &g, ch, 0)) {
            if (ch >= 0x20 && ch <= 0x7E) {
                int prop_adv = (int)g.box_w + (int)g.ofs_x + letter_spacing;
                if (prop_adv < 2)
                    prop_adv = 2;
                width += prop_adv;
            } else {
                width += g.adv_w;
            }
        } else {
            width += font->line_height / 2;
        }
    }

    return width;
}

int rawdraw_measure_text_height(const lv_font_t *font)
{
    return font ? font->line_height : 16;
}

rawdraw_rect_t rawdraw_measure_text_bounds(const char *text, const lv_font_t *font, int max_width)
{
    if (!text || text[0] == '\0') {
        return (rawdraw_rect_t){0, 0, 0, 0};
    }
    if (!font) {
        return (rawdraw_rect_t){0, 0, 0, 0};
    }

    int max_line_w = 0;
    int current_line_w = 0;
    int line_count = 1;
    const char *p = text;

    const int letter_spacing = (font->line_height + 8) / 16;
    const int space_width = font->line_height / 4;

    while (*p) {
        uint32_t ch = utf8_next(&p);
        if (ch == 0)
            break;

        if (ch == '\n') {
            max_line_w = RD_MAX(max_line_w, current_line_w);
            current_line_w = 0;
            line_count++;
            continue;
        }

        int char_w = 0;
        if (ch == ' ') {
            char_w = space_width;
        } else {
            lv_font_glyph_dsc_t g = {0};
            g.resolved_font = font;
            if (lv_font_get_glyph_dsc(font, &g, ch, 0)) {
                if (ch >= 0x20 && ch <= 0x7E) {
                    char_w = (int)g.box_w + (int)g.ofs_x + letter_spacing;
                    if (char_w < 2)
                        char_w = 2;
                } else {
                    char_w = g.adv_w;
                }
            } else {
                char_w = font->line_height / 2;
            }
        }

        if (max_width > 0 && current_line_w + char_w > max_width && current_line_w > 0) {
            max_line_w = RD_MAX(max_line_w, current_line_w);
            current_line_w = char_w;
            line_count++;
        } else {
            current_line_w += char_w;
        }
    }

    max_line_w = RD_MAX(max_line_w, current_line_w);

    rawdraw_rect_t out = {0, 0, max_line_w, line_count * font->line_height};
    return out;
}

/* Delegates to rawdraw_draw_rect(), which fills each scanline through
 * rawdraw_fill_scanline_segment(). That helper already memsets the
 * byte-aligned interior in one shot and only falls back to per-pixel writes
 * for sub-byte head/tail edges, so a __builtin_constant_p(color) fast path
 * here would be redundant: rd_color_to_fill_byte() collapses a compile-time
 * color into a single fill byte that memset then broadcasts. */
void rawdraw_fill_rect(uint8_t *fb, int width, int height, rawdraw_rect_t r, rawdraw_color_t color)
{
    rawdraw_draw_rect(fb, width, height, r.x, r.y, r.w, r.h, (int)color);
}

void rawdraw_invert_region(uint8_t *fb, int width, int height, rawdraw_rect_t r)
{
    if (!fb || r.w <= 0 || r.h <= 0)
        return;
    rawdraw_rect_t clipped = rawdraw_clamp_rect(r, width, height);
    if (clipped.w <= 0 || clipped.h <= 0)
        return;

    /* Row-wise: hoist row base index out of inner loop.
     * Only swap black<->white; yellow/red left unchanged. */
    uint16_t bpr = (uint16_t)((width * 2 + 7) >> 3);
    for (int y = clipped.y; y < clipped.y + clipped.h; y++) {
        uint32_t row_base = (uint32_t)y * bpr;
        for (int x = clipped.x; x < clipped.x + clipped.w; x++) {
            uint32_t index = row_base + (uint32_t)(x >> 2);
            uint8_t shift = (uint8_t)(6 - ((x & 0x03) << 1));
            uint8_t bits = (uint8_t)((fb[index] >> shift) & 0x03);
            uint8_t inv;
            if (bits == RAWDRAW_COLOR_BLACK) {
                inv = (uint8_t)RAWDRAW_COLOR_WHITE;
            } else if (bits == RAWDRAW_COLOR_WHITE) {
                inv = (uint8_t)RAWDRAW_COLOR_BLACK;
            } else {
                continue; /* yellow/red unchanged */
            }
            uint8_t mask = (uint8_t)(0x03U << shift);
            fb[index] = (uint8_t)((fb[index] & (uint8_t)~mask) | (inv << shift));
        }
    }
}

void rawdraw_copy_region(const uint8_t *src, uint8_t *dst, int width, int height, rawdraw_rect_t src_r, int dst_x,
                         int dst_y)
{
    if (!src || !dst)
        return;
    rawdraw_rect_t clipped = rawdraw_clamp_rect(src_r, width, height);
    if (clipped.w <= 0 || clipped.h <= 0)
        return;

    int sx = clipped.x;
    int dx = dst_x;
    int copy_w = clipped.w;

    /* Clip destination horizontally */
    if (dx < 0) {
        sx -= dx;
        copy_w += dx;
        dx = 0;
    }
    if (dx + copy_w > width)
        copy_w = width - dx;
    if (copy_w <= 0)
        return;

    /* Clip destination vertically */
    int dy_start = dst_y;
    int copy_h = clipped.h;
    if (dy_start < 0) {
        copy_h += dy_start;
        dy_start = 0;
    }
    if (dy_start + copy_h > height)
        copy_h = height - dy_start;
    if (copy_h <= 0)
        return;

    int src_y_start = clipped.y + (dy_start - dst_y);
    uint16_t bpr = (uint16_t)((width * 2 + 7) >> 3);

    /* Fast path: byte-aligned copy (both x offsets divisible by 4,
     * copy width divisible by 4 → whole 2bpp bytes per row) */
    if ((sx & 3) == 0 && (dx & 3) == 0 && (copy_w & 3) == 0) {
        size_t bytes_per_row_copy = (size_t)(copy_w / 4);
        for (int i = 0; i < copy_h; i++) {
            const uint8_t *sp = src + (size_t)(src_y_start + i) * bpr + (size_t)(sx >> 2);
            uint8_t *dp = dst + (size_t)(dy_start + i) * bpr + (size_t)(dx >> 2);
            memcpy(dp, sp, bytes_per_row_copy);
        }
    } else {
        /* Per-pixel fallback (row base hoisted out of inner loop) */
        for (int i = 0; i < copy_h; i++) {
            uint32_t src_row = (uint32_t)(src_y_start + i) * bpr;
            uint32_t dst_row = (uint32_t)(dy_start + i) * bpr;
            for (int x = 0; x < copy_w; x++) {
                uint32_t si = src_row + (uint32_t)((sx + x) >> 2);
                uint8_t sshift = (uint8_t)(6 - (((sx + x) & 0x03) << 1));
                uint8_t bits = (uint8_t)((src[si] >> sshift) & 0x03);
                uint32_t di = dst_row + (uint32_t)((dx + x) >> 2);
                uint8_t dshift = (uint8_t)(6 - (((dx + x) & 0x03) << 1));
                uint8_t dmask = (uint8_t)(0x03U << dshift);
                dst[di] = (uint8_t)((dst[di] & (uint8_t)~dmask) | (bits << dshift));
            }
        }
    }
}
/* ------------------------------------------------------------------ */
/* 90° rotation blit for 2bpp framebuffers                             */
/* ------------------------------------------------------------------ */
void rawdraw_blit_rotated_90(const uint8_t *src, int sw, int sh, uint8_t *dst, int dw, int dh, int dst_x, int dst_y)
{
    if (!src || !dst || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0)
        return;

    /* Rotated footprint is (sh) wide x (sw) tall, placed at (dst_x, dst_y).
     * Source pixel (sx, sy) -> dst pixel (dst_x + sh-1-sy, dst_y + sx).
     * Iterating by source row keeps the destination x-column constant, so the
     * dst byte offset and 2-bit shift are hoisted out of the inner loop. */
    const uint32_t src_bpr = (uint32_t)((sw * 2 + 7) >> 3);
    const uint32_t dst_bpr = (uint32_t)((dw * 2 + 7) >> 3);

    for (int sy = 0; sy < sh; ++sy) {
        int dx = dst_x + (sh - 1 - sy);
        if (dx < 0 || dx >= dw)
            continue; /* whole column off-screen */

        const uint8_t *src_row = src + (uint32_t)sy * src_bpr;
        uint32_t dst_col_byte = (uint32_t)(dx >> 2);
        uint8_t shift = (uint8_t)(6 - ((dx & 0x03) << 1));
        uint8_t dmask = (uint8_t)(0x03U << shift);
        uint8_t keep = (uint8_t)~dmask;

        /* Batched reads: each source byte holds 4 packed 2bpp pixels. */
        int sx = 0;
        for (; sw - sx >= 4; sx += 4) {
            uint8_t sb = src_row[(uint32_t)sx >> 2];
            for (int i = 0; i < 4; ++i) {
                int dy = dst_y + sx + i;
                if ((unsigned)dy >= (unsigned)dh)
                    continue;
                uint8_t color = (uint8_t)((sb >> (6 - (i << 1))) & 0x03U);
                uint8_t *dp = dst + (uint32_t)dy * dst_bpr + dst_col_byte;
                *dp = (uint8_t)((*dp & keep) | (color << shift));
            }
        }
        for (; sx < sw; ++sx) {
            int dy = dst_y + sx;
            if ((unsigned)dy >= (unsigned)dh)
                continue;
            uint8_t sb = src_row[(uint32_t)sx >> 2];
            uint8_t color = (uint8_t)((sb >> (6 - ((sx & 0x03) << 1))) & 0x03U);
            uint8_t *dp = dst + (uint32_t)dy * dst_bpr + dst_col_byte;
            *dp = (uint8_t)((*dp & keep) | (color << shift));
        }
    }
}

void rawdraw_clear(uint8_t *fb, int width, int height, rawdraw_color_t fill)
{
    if (!fb || width <= 0 || height <= 0)
        return;

    size_t bytes_per_row = (width * 2 + 7) >> 3;
    size_t total = bytes_per_row * height;
    memset(fb, ColorToFillByte(fill), total);
}

void rawdraw_draw_stripe_rect(uint8_t *fb, int width, int height, rawdraw_rect_t r)
{
    if (!fb || r.w <= 0 || r.h <= 0)
        return;
    for (int y = r.y; y < r.y + r.h; ++y) {
        const rawdraw_color_t line_color = ((y - r.y) & 1) ? RAWDRAW_COLOR_WHITE : RAWDRAW_COLOR_BLACK;
        rawdraw_draw_hline(fb, width, height, y, r.x, r.x + r.w - 1, line_color);
    }
}

rawdraw_color_t rawdraw_get_pixel(const uint8_t *fb, int width, int height, int x, int y)
{
    if (!fb || x < 0 || y < 0 || x >= width || y >= height)
        return RAWDRAW_COLOR_WHITE;

    uint16_t bytes_per_row = (width * 2 + 7) >> 3;
    uint32_t index = (uint32_t)y * bytes_per_row + (uint32_t)(x >> 2);
    uint8_t shift = (uint8_t)(6 - ((x & 0x03) << 1));
    return (rawdraw_color_t)((fb[index] >> shift) & 0x03);
}
