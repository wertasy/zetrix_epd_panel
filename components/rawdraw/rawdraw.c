#ifdef ESP_PLATFORM
#    include "esp_log.h"
#endif
#include "rawdraw.h"
#include <string.h>
#include "rawdraw_util.h"

/* Integer square root — avoids floating-point on Xtensa LX7.
 * Classic bit-shifting algorithm, O(log n) with no library dependency. */
static int isqrt(int n)
{
    if (n <= 0)
        return 0;
    int x = n;
    int c = 0;
    for (int d = 1 << 15; d > 0; d >>= 1) {
        if ((c | d) <= x)
            c |= d;
    }
    /* c is floor(sqrt(n)) — verify: c*c <= n */
    while (c * c > n)
        c--;
    return c;
}

void rawdraw_set_pixel(uint8_t *fb, int width, int height, int x, int y, int color)
{
    if (!fb || x < 0 || y < 0 || x >= width || y >= height)
        return;

    uint16_t bytes_per_row = (width * 2 + 7) >> 3;
    uint32_t index = (uint32_t)y * bytes_per_row + (uint32_t)(x >> 2);
    uint8_t shift = (uint8_t)(6 - ((x & 0x03) << 1));
    uint8_t mask = (uint8_t)(0x03U << shift);
    fb[index] = (uint8_t)((fb[index] & (uint8_t)~mask) | ((uint8_t)(color & 0x03) << shift));
}

/* P0: Fill a horizontal pixel run using memset for byte-aligned interior.
 * Eliminates 75% of PSRAM read-modify-write cycles for solid fills. */
void rawdraw_fill_scanline_segment(uint8_t *fb, int fb_width, int fb_height, int y, int x_start, int x_end, int color)
{
    if (!fb || y < 0 || y >= fb_height)
        return;

    int xs = RD_MAX(0, x_start);
    int xe = RD_MIN(fb_width, x_end);
    if (xs >= xe)
        return;

    uint16_t bpr = (uint16_t)((fb_width * 2 + 7) >> 3);
    uint8_t fill = rd_color_to_fill_byte(color);
    uint8_t *row = fb + (uint32_t)y * bpr;

    /* Fast path: pixel range perfectly aligned to byte boundaries */
    if (likely((xs & 0x03) == 0 && (xe & 0x03) == 0)) {
        memset(row + xs / 4, fill, (size_t)(xe - xs) / 4);
        return;
    }

    /* General path: handle head/tail sub-byte edges, memset the middle.
     * head: [xs, mid_start) — leading sub-byte pixels
     * mid:  [mid_start, mid_end) — byte-aligned memset (only if non-empty)
     * tail: [tail_start, xe) — trailing sub-byte pixels
     * tail_start = max(mid_end, mid_start) so tail never starts before head ends. */
    int mid_start = (xs + 3) & ~3; /* round up to next byte boundary */
    int mid_end = xe & ~3; /* round down to byte boundary */
    int tail_start = (mid_end > mid_start) ? mid_end : mid_start;

    /* Head */
    for (int x = xs; x < mid_start && x < xe; x++)
        rawdraw_set_pixel_unchecked(fb, fb_width, fb_height, x, y, color);

    /* Middle: byte-aligned bulk memset */
    if (mid_end > mid_start)
        memset(row + mid_start / 4, fill, (size_t)(mid_end - mid_start) / 4);

    /* Tail */
    for (int x = tail_start; x < xe; x++)
        rawdraw_set_pixel_unchecked(fb, fb_width, fb_height, x, y, color);
}

void rawdraw_draw_rect(uint8_t *fb, int w, int h, int rx, int ry, int rw, int rh, int color)
{
    if (!fb || rw <= 0 || rh <= 0)
        return;

    int y_start = RD_MAX(0, ry);
    int y_end = RD_MIN(h, ry + rh);
    int x_start = RD_MAX(0, rx);
    int x_end = RD_MIN(w, rx + rw);

    for (int y = y_start; y < y_end; y++)
        rawdraw_fill_scanline_segment(fb, w, h, y, x_start, x_end, color);
}

void rawdraw_draw_dither_rect(uint8_t *fb, int w, int h, int rx, int ry, int rw, int rh)
{
    if (!fb || rw <= 0 || rh <= 0)
        return;

    int y_start = RD_MAX(0, ry);
    int y_end = RD_MIN(h, ry + rh);
    int x_start = RD_MAX(0, rx);
    int x_end = RD_MIN(w, rx + rw);

    for (int y = y_start; y < y_end; y++) {
        for (int x = x_start; x < x_end; x++) {
            int color = ((x + y) & 1) ? RAWDRAW_COLOR_WHITE : RAWDRAW_COLOR_BLACK;
            rawdraw_set_pixel_unchecked(fb, w, h, x, y, color);
        }
    }
}

/* For a given scanline y inside the rounded rect's vertical extent, compute
 * the horizontal pixel span [x_lo, x_hi] (inclusive) that falls inside the
 * rounded rect. Returns false if the entire row is outside (no visible pixels). */
static bool round_rect_row_span(int y, int rx, int ry, int rw, int rh, int radius, int *x_lo, int *x_hi)
{
    int x_start = rx;
    int x_end = rx + rw - 1; /* inclusive */

    if (radius > 0) {
        int top = ry + radius;
        int bottom = ry + rh - 1 - radius;

        if (y < top || y > bottom) {
            /* Corner region: compute x-extent from circle equation.
             * The nearest corner center cy is at top or bottom band edge. */
            int cy = (y < top) ? top : bottom;
            int dy = y - cy;
            int d2 = radius * radius - dy * dy;
            if (d2 < 0)
                return false; /* entire row outside */
            int dx = isqrt(d2);
            x_start = rx + radius - dx;
            x_end = rx + rw - 1 - radius + dx;
        }
    }

    /* Clamp to bounding box */
    if (x_start < rx)
        x_start = rx;
    if (x_end > rx + rw - 1)
        x_end = rx + rw - 1;
    if (x_start > x_end)
        return false;

    *x_lo = x_start;
    *x_hi = x_end;
    return true;
}

void rawdraw_draw_round_rect(uint8_t *fb, int w, int h, int rx, int ry, int rw, int rh, int radius, int fill_color,
                             int border_color, int thickness)
{
    if (!fb || rw <= 0 || rh <= 0)
        return;

    int max_radius = RD_MIN(rw, rh) / 2;
    radius = RD_MIN(radius, max_radius);
    if (radius < 0)
        radius = 0;

    int y_start = RD_MAX(0, ry);
    int y_end = RD_MIN(h, ry + rh);

    if (thickness > 0) {
        /* Border: for each row, compute outer span and inner span.
         * Fill border_color on [outer_lo, inner_lo) ∪ (inner_hi, outer_hi],
         * fill fill_color on [inner_lo, inner_hi]. */
        int inner_rx = rx + thickness;
        int inner_ry = ry + thickness;
        int inner_rw = rw - thickness * 2;
        int inner_rh = rh - thickness * 2;
        int inner_radius = radius - thickness;
        if (inner_radius < 0)
            inner_radius = 0;
        bool has_inner = (inner_rw > 0 && inner_rh > 0);

        for (int y = y_start; y < y_end; y++) {
            int o_lo, o_hi;
            if (!round_rect_row_span(y, rx, ry, rw, rh, radius, &o_lo, &o_hi))
                continue;

            if (has_inner) {
                int i_lo, i_hi;
                if (round_rect_row_span(y, inner_rx, inner_ry, inner_rw, inner_rh, inner_radius, &i_lo, &i_hi)) {
                    /* Left border */
                    if (o_lo < i_lo)
                        rawdraw_fill_scanline_segment(fb, w, h, y, o_lo, i_lo, border_color);
                    /* Interior fill */
                    rawdraw_fill_scanline_segment(fb, w, h, y, i_lo, i_hi + 1, fill_color);
                    /* Right border */
                    if (o_hi > i_hi)
                        rawdraw_fill_scanline_segment(fb, w, h, y, i_hi + 1, o_hi + 1, border_color);
                } else {
                    /* No inner span — entire outer span is border */
                    rawdraw_fill_scanline_segment(fb, w, h, y, o_lo, o_hi + 1, border_color);
                }
            } else {
                /* No valid inner rect — entire span is border */
                rawdraw_fill_scanline_segment(fb, w, h, y, o_lo, o_hi + 1, border_color);
            }
        }
    } else {
        /* Solid fill: one span per row */
        for (int y = y_start; y < y_end; y++) {
            int x_lo, x_hi;
            if (round_rect_row_span(y, rx, ry, rw, rh, radius, &x_lo, &x_hi))
                rawdraw_fill_scanline_segment(fb, w, h, y, x_lo, x_hi + 1, fill_color);
        }
    }
}

void rawdraw_draw_text(uint8_t *fb, int w, int h, int x, int y, const char *text, const lv_font_t *font, int color)
{
    if (!fb || !text || !font)
        return;

    int cursor_x = x;
    int cursor_y = y;
    const char *p = text;

    /* Letter spacing for proportional ASCII rendering with CJK fonts.
     * CJK fonts assign uniform cell-width adv_w to ALL characters,
     * which causes overlaps (W, M) and huge gaps (I, l) for Latin text.
     * For ASCII chars we compute: box_w + ofs_x + spacing. */
    const int letter_spacing = (font->line_height + 8) / 16; /* ~2px at 24px, ~1px at 16px */
    const int space_width = font->line_height / 4;

    while (*p) {
        uint32_t ch = utf8_next(&p);
        if (ch == 0)
            break;

        if (ch == '\n') {
            cursor_x = x;
            cursor_y += font->line_height;
            continue;
        }

        /* Handle space explicitly (box_w=0 in many fonts) */
        if (ch == ' ') {
            cursor_x += space_width;
            continue;
        }

        lv_font_glyph_dsc_t g = {0};
        g.resolved_font = font;
        if (!lv_font_get_glyph_dsc(font, &g, ch, 0)) {
            cursor_x += font->line_height / 2;
            continue;
        }

        /* Request the RAW (A1) bitmap directly via the font's get_glyph_bitmap
         * function pointer with req_raw_bitmap=1. This cannot go through
         * lv_font_get_glyph_bitmap() — the LVGL v9 wrapper unconditionally
         * clears req_raw_bitmap before dispatch and would dereference the NULL
         * draw_buf — and lv_font_get_glyph_static_bitmap() requires
         * font->static_bitmap, which the lv_font_conv-generated fonts don't
         * set (v7/v8 field set). Direct dispatch with req_raw_bitmap=1 makes
         * lv_font_get_bitmap_fmt_txt return the embedded glyph pointer. */
        g.req_raw_bitmap = 1;
        const uint8_t *bitmap = (const uint8_t *)font->get_glyph_bitmap(&g, NULL);
        g.req_raw_bitmap = 0;

        if (!bitmap) {
            static bool s_warned = false;
            if (!s_warned) {
                s_warned = true;
#ifdef ESP_PLATFORM
                /* 0x42000000+ region means the pointer is in flash (valid) */
                ESP_LOGE("rawdraw", "glyph bitmap NULL for char U+%04X (font static_bitmap=%d)", (unsigned)ch,
                         font->static_bitmap ? 1 : 0);
#endif
            }
            cursor_x += g.adv_w;
            continue;
        }

        int gx = cursor_x + g.ofs_x;
        int gy = cursor_y + font->line_height - font->base_line - g.ofs_y - g.box_h;
        int row_bits = (g.stride > 0) ? (int)(g.stride * 8) : (int)g.box_w;

        /* P0: Glyph-box precheck — if the entire box is inside the framebuffer,
         * use the unchecked fast path (no per-pixel bounds check). */
        bool glyph_fully_visible = (gx >= 0 && gy >= 0 && gx + (int)g.box_w <= w && gy + (int)g.box_h <= h);

        for (int row = 0; row < (int)g.box_h; row++) {
            for (int col = 0; col < (int)g.box_w; col++) {
                int bit_idx = row * row_bits + col;
                bool pixel = (bitmap[bit_idx >> 3] >> (7 - (bit_idx & 7))) & 1;

                if (pixel) {
                    if (likely(glyph_fully_visible)) {
                        rawdraw_set_pixel_unchecked(fb, w, h, gx + col, gy + row, color);
                    } else {
                        int px = gx + col, py = gy + row;
                        if (px >= 0 && px < w && py >= 0 && py < h)
                            rawdraw_set_pixel_unchecked(fb, w, h, px, py, color);
                    }
                }
            }
        }

        /* Advance: use proportional width for ASCII, original adv_w for CJK.
         * Proportional: visual extent (box_w + ofs_x) + letter_spacing.
         * This prevents wide glyphs (W, M) from overlapping the next char,
         * and tightens narrow glyphs (I, l, i) that had CJK cell-width gaps. */
        if (ch >= 0x20 && ch <= 0x7E) {
            int prop_adv = (int)g.box_w + (int)g.ofs_x + letter_spacing;
            if (prop_adv < 2)
                prop_adv = 2;
            cursor_x += prop_adv;
        } else {
            cursor_x += g.adv_w;
        }
    }
}
