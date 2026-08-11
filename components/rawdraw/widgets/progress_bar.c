/**
 * @file progress_bar.c
 * @brief Progress bar and circular gauge widget implementation.
 */
#include "../include/rawdraw_util.h"
#include "progress_bar.h"
#include "../include/rawdraw_ext.h"
#include "../include/theme.h"
#include "../include/style.h"
#include <string.h>
#include <math.h>

/* ============================================================
 * Local helpers
 * ============================================================ */

static void copy_str(char *dst, size_t cap, const char *src)
{
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

/* ============================================================
 * Circular progress — ring-arc helper
 * ============================================================ */

/* Integer square root for small radii (R <= ~64 => at most ~64 iterations). */
static int ring_isqrt(int n)
{
    if (n <= 0)
        return 0;
    int r = 1;
    while ((r + 1) * (r + 1) <= n)
        ++r;
    return r;
}

/**
 * @brief Draw a ring arc (annular sector) at the given radius and thickness.
 *
 * Only pixels within the annular region (inner_r <= dist <= outer_r) and
 * within the angular span [start_deg, end_deg] (clock degrees: 0 = 12-o'clock,
 * clockwise increases) are painted.
 *
 * The per-pixel work is fully integer: the outer circle is walked scanline by
 * scanline (dx_max = isqrt(R^2 - dy^2)), and the angular wedge is tested with
 * cross products against the two boundary rays. atan2f is never called inside
 * the pixel loop; sinf/cosf run exactly four times at setup to derive the
 * fixed-point boundary vectors.
 */
static void draw_ring_arc(uint8_t *fb, int width, int height, int cx, int cy, int outer_r, int thickness,
                          float start_deg, float end_deg, rawdraw_color_t color)
{
    int inner_r = outer_r - thickness + 1;
    if (inner_r < 1)
        inner_r = 1;

    const float PI_val     = 3.14159265f;
    const float TWO_PI     = 2.0f * PI_val;
    const float DEG_TO_RAD = PI_val / 180.0f;

    /* Normalize boundaries to math radians. Screen y is down, so atan2(dy,dx)
     * increases clockwise; the (deg - 90) shift maps clock 0 deg -> straight up. */
    float start_math = (start_deg - 90.0f) * DEG_TO_RAD;
    float end_math   = (end_deg - 90.0f) * DEG_TO_RAD;
    while (start_math < 0)
        start_math += TWO_PI;
    while (start_math >= TWO_PI)
        start_math -= TWO_PI;
    while (end_math < 0)
        end_math += TWO_PI;
    while (end_math >= TWO_PI)
        end_math -= TWO_PI;
    if (end_math <= start_math)
        end_math += TWO_PI;
    float arc_span = end_math - start_math; /* (0, 2*PI] */

    /* Fixed-point unit vectors for each boundary ray (only the sign matters).
     * cross(p, u_a) = dx*sin(a) - dy*cos(a) = -|p|*sin(theta - a):
     *   <= 0  <=> pixel is clockwise-or-equal to a
     *   >= 0  <=> pixel is counter-clockwise-or-equal to a          */
    const int Q  = (1 << 20);
    int       Ss = (int)lroundf(sinf(start_math) * (float)Q);
    int       Cs = (int)lroundf(cosf(start_math) * (float)Q);
    int       Se = (int)lroundf(sinf(end_math) * (float)Q);
    int       Ce = (int)lroundf(cosf(end_math) * (float)Q);
    /* Arc <= 180 deg is the intersection of the two boundary half-planes; an arc
     * > 180 deg is their union (each cross-product test is exact within +-180 deg). */
    int use_or = (arc_span > PI_val);

    int outer_r_sq = outer_r * outer_r;
    int inner_r_sq = inner_r * inner_r;

    for (int dy = -outer_r; dy <= outer_r; dy++) {
        int py = cy + dy;
        if ((unsigned)py >= (unsigned)height)
            continue;

        /* |dx| <= dx_max guarantees dx^2 + dy^2 <= outer_r^2 (outer test implicit). */
        int dx_max = ring_isqrt(outer_r_sq - dy * dy);
        int dy_sq  = dy * dy;

        for (int dx = -dx_max; dx <= dx_max; dx++) {
            if (dx * dx + dy_sq < inner_r_sq)
                continue; /* inside the hole */

            int cs     = dx * Ss - dy * Cs; /* <= 0 => clockwise-or-equal of start */
            int ce     = dx * Se - dy * Ce; /* >= 0 => ccw-or-equal of end         */
            int in_arc = use_or ? (cs <= 0 || ce >= 0) : (cs <= 0 && ce >= 0);
            if (in_arc) {
                int px = cx + dx;
                if ((unsigned)px < (unsigned)width) {
                    rawdraw_set_pixel_unchecked(fb, width, height, px, py, (int)color);
                }
            }
        }
    }
}

/* ============================================================
 * Standalone circular progress primitives
 * ============================================================ */

void rawdraw_draw_circular_progress(uint8_t *fb, int width, int height, rawdraw_point_t center, int radius,
                                    int thickness, int value_pct, rawdraw_color_t bg_color, rawdraw_color_t fg_color)
{
    if (!fb || radius <= 0 || thickness <= 0)
        return;

    value_pct = RD_CLAMP(value_pct, 0, 100);

    /* Background ring (full 360 degrees) */
    draw_ring_arc(fb, width, height, center.x, center.y, radius, thickness, 0.0f, 360.0f, bg_color);

    /* Foreground arc (value percentage, clockwise from 12-o'clock) */
    if (value_pct > 0) {
        float end_deg = (value_pct * 360.0f) / 100.0f;
        draw_ring_arc(fb, width, height, center.x, center.y, radius, thickness, 0.0f, end_deg, fg_color);
    }
}

void rawdraw_draw_circular_progress_with_label(uint8_t *fb, int width, int height, rawdraw_point_t center, int radius,
                                               int thickness, int value_pct, const char *label, const lv_font_t *font)
{
    rawdraw_draw_circular_progress(fb, width, height, center, radius, thickness, value_pct, RAWDRAW_COLOR_WHITE,
                                   RAWDRAW_COLOR_BLACK);

    if (label && font) {
        int text_w  = rawdraw_measure_text_width(label, font);
        int text_h  = (int)font->line_height;
        int label_x = center.x - text_w / 2;
        int label_y = center.y - text_h / 2;
        rawdraw_draw_text(fb, width, height, label_x, label_y, label, font,
                          rawdraw_theme_color_for(THEME_TOKEN_TEXT_PRIMARY));
    }
}

/* ============================================================
 * Horizontal ProgressBar widget
 * ============================================================ */

void widget_progress_bar_init(widget_progress_bar_t *bar, int x, int y, int w, int h)
{
    if (!bar)
        return;
    memset(bar, 0, sizeof(*bar));
    bar->bounds.x      = x;
    bar->bounds.y      = y;
    bar->bounds.w      = w;
    bar->bounds.h      = h;
    bar->value         = 0;
    bar->radius        = STYLE_PROGRESS_RADIUS;
    bar->bg_color      = RAWDRAW_COLOR_WHITE;
    bar->fg_color      = RAWDRAW_COLOR_BLACK;
    bar->custom_colors = false;
}

void widget_progress_bar_set_bounds(widget_progress_bar_t *bar, int x, int y, int w, int h)
{
    if (!bar)
        return;
    bar->bounds.x = x;
    bar->bounds.y = y;
    bar->bounds.w = w;
    bar->bounds.h = h;
}

void widget_progress_bar_set_value(widget_progress_bar_t *bar, int value)
{
    if (!bar)
        return;
    bar->value = RD_CLAMP(value, 0, 100);
}

int widget_progress_bar_get_value(const widget_progress_bar_t *bar)
{
    return bar ? bar->value : 0;
}

void widget_progress_bar_set_label(widget_progress_bar_t *bar, const char *label)
{
    if (!bar)
        return;
    copy_str(bar->label, WIDGET_PROGRESS_BAR_LABEL_LEN, label);
}

void widget_progress_bar_set_label_font(widget_progress_bar_t *bar, const lv_font_t *font)
{
    if (!bar)
        return;
    bar->label_font = font;
}

void widget_progress_bar_set_radius(widget_progress_bar_t *bar, int radius)
{
    if (!bar)
        return;
    bar->radius = radius;
}

void widget_progress_bar_set_bg_color(widget_progress_bar_t *bar, rawdraw_color_t color)
{
    if (!bar)
        return;
    bar->bg_color      = color;
    bar->custom_colors = true;
}

void widget_progress_bar_set_fg_color(widget_progress_bar_t *bar, rawdraw_color_t color)
{
    if (!bar)
        return;
    bar->fg_color      = color;
    bar->custom_colors = true;
}

rawdraw_rect_t widget_progress_bar_get_bounds(const widget_progress_bar_t *bar)
{
    rawdraw_rect_t r = {0, 0, 0, 0};
    if (!bar)
        return r;
    return bar->bounds;
}

void widget_progress_bar_render(const widget_progress_bar_t *bar, uint8_t *fb, int fb_width, int fb_height)
{
    if (!bar || !fb)
        return;

    rawdraw_rect_t bounds = rawdraw_clamp_rect(bar->bounds, fb_width, fb_height);
    if (rawdraw_rect_area(bounds) <= 0)
        return;

    /* Clamp radius to half the smaller dimension. */
    int max_radius = (bounds.w < bounds.h ? bounds.w : bounds.h) / 2;
    int r          = bar->radius;
    if (r > max_radius)
        r = max_radius;
    if (r < 0)
        r = 0;

    rawdraw_paint_style_t style = rawdraw_theme_component(ROLE_PROGRESS);
    if (bar->custom_colors) {
        style.bg = bar->bg_color;
        style.fg = bar->fg_color;
    }
    rawdraw_draw_styled_progress(fb, fb_width, fb_height, bounds, bar->value, &style, r);

    /* Draw label if set */
    if (bar->label[0] != '\0' && bar->label_font) {
        int text_w  = rawdraw_measure_text_width(bar->label, bar->label_font);
        int text_h  = (int)bar->label_font->line_height;
        int label_x = bounds.x + (bounds.w - text_w) / 2;
        int label_y = bounds.y + (bounds.h - text_h) / 2;

        /* Choose color based on position relative to fill */
        int             fill_x     = bounds.x + (bounds.w * bar->value) / 100;
        rawdraw_color_t text_color = (label_x < fill_x) ? style.bg : style.fg;

        rawdraw_draw_text(fb, fb_width, fb_height, label_x, label_y, bar->label, bar->label_font, text_color);
    }
}

/* ============================================================
 * CircularGauge widget
 * ============================================================ */

void widget_circular_gauge_init(widget_circular_gauge_t *gauge, int cx, int cy, int radius, int thickness)
{
    if (!gauge)
        return;
    memset(gauge, 0, sizeof(*gauge));
    gauge->cx            = cx;
    gauge->cy            = cy;
    gauge->radius        = radius;
    gauge->thickness     = thickness;
    gauge->value         = 0;
    gauge->bg_color      = RAWDRAW_COLOR_WHITE;
    gauge->fg_color      = RAWDRAW_COLOR_BLACK;
    gauge->custom_colors = false;
}

void widget_circular_gauge_set_center(widget_circular_gauge_t *gauge, int cx, int cy)
{
    if (!gauge)
        return;
    gauge->cx = cx;
    gauge->cy = cy;
}

void widget_circular_gauge_set_radius(widget_circular_gauge_t *gauge, int radius)
{
    if (!gauge)
        return;
    gauge->radius = radius;
}

void widget_circular_gauge_set_thickness(widget_circular_gauge_t *gauge, int thickness)
{
    if (!gauge)
        return;
    gauge->thickness = thickness;
}

void widget_circular_gauge_set_value(widget_circular_gauge_t *gauge, int value)
{
    if (!gauge)
        return;
    gauge->value = RD_CLAMP(value, 0, 100);
}

int widget_circular_gauge_get_value(const widget_circular_gauge_t *gauge)
{
    return gauge ? gauge->value : 0;
}

void widget_circular_gauge_set_label(widget_circular_gauge_t *gauge, const char *label)
{
    if (!gauge)
        return;
    copy_str(gauge->label, WIDGET_PROGRESS_BAR_LABEL_LEN, label);
}

void widget_circular_gauge_set_label_font(widget_circular_gauge_t *gauge, const lv_font_t *font)
{
    if (!gauge)
        return;
    gauge->label_font = font;
}

void widget_circular_gauge_set_bg_color(widget_circular_gauge_t *gauge, rawdraw_color_t color)
{
    if (!gauge)
        return;
    gauge->bg_color      = color;
    gauge->custom_colors = true;
}

void widget_circular_gauge_set_fg_color(widget_circular_gauge_t *gauge, rawdraw_color_t color)
{
    if (!gauge)
        return;
    gauge->fg_color      = color;
    gauge->custom_colors = true;
}

rawdraw_rect_t widget_circular_gauge_get_bounds(const widget_circular_gauge_t *gauge)
{
    rawdraw_rect_t r = {0, 0, 0, 0};
    if (!gauge)
        return r;
    r.x = gauge->cx - gauge->radius;
    r.y = gauge->cy - gauge->radius;
    r.w = gauge->radius * 2;
    r.h = gauge->radius * 2;
    return r;
}

void widget_circular_gauge_render(const widget_circular_gauge_t *gauge, uint8_t *fb, int fb_width, int fb_height)
{
    if (!gauge || !fb || gauge->radius <= 0)
        return;

    rawdraw_color_t bg = gauge->bg_color;
    rawdraw_color_t fg = gauge->fg_color;
    if (!gauge->custom_colors) {
        rawdraw_paint_style_t style = rawdraw_theme_component(ROLE_PROGRESS);
        bg                          = style.bg;
        fg                          = style.fg;
    }

    rawdraw_point_t center = {gauge->cx, gauge->cy};
    rawdraw_draw_circular_progress(fb, fb_width, fb_height, center, gauge->radius, gauge->thickness, gauge->value, bg,
                                   fg);

    /* Draw center label */
    if (gauge->label[0] != '\0' && gauge->label_font) {
        int text_w  = rawdraw_measure_text_width(gauge->label, gauge->label_font);
        int text_h  = (int)gauge->label_font->line_height;
        int label_x = gauge->cx - text_w / 2;
        int label_y = gauge->cy - text_h / 2;
        rawdraw_draw_text(fb, fb_width, fb_height, label_x, label_y, gauge->label, gauge->label_font,
                          rawdraw_theme_color_for(THEME_TOKEN_TEXT_PRIMARY));
    }
}
