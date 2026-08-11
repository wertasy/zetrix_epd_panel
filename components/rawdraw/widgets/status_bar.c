/**
 * @file status_bar.c
 * @brief Bottom status bar widget implementation.
 *
 * Renders a thin status bar at the bottom of the screen with auto-hide support.
 * Reserves the clock zone (top-right) to ensure no overlap.
 */
#include "../include/rawdraw_util.h"
#include "status_bar.h"
#include "../include/rawdraw_ext.h"
#include "../include/theme.h"

#include <string.h>

/* ============================================================
 * Local helpers
 * ============================================================ */

/**
 * Compute the clock reserved zone from header constants (avoids linking
 * clock.c on the host test build while remaining correct on target).
 */
static rawdraw_rect_t clock_reserved_zone(void)
{
    rawdraw_rect_t r = {CLOCK_DEFAULT_X, CLOCK_DEFAULT_Y, CLOCK_W, CLOCK_H};
    return r;
}

/* ============================================================
 * Lifecycle
 * ============================================================ */

void widget_status_bar_init(widget_status_bar_t *sb, const lv_font_t *font)
{
    if (!sb)
        return;
    memset(sb, 0, sizeof(*sb));
    sb->visible      = false;
    sb->text[0]      = '\0';
    sb->show_time_us = 0;
    sb->auto_hide_ms = 0;
    sb->font         = font;
}

/* ============================================================
 * Control
 * ============================================================ */

void widget_status_bar_show(widget_status_bar_t *sb, const char *text, int64_t auto_hide_ms, int64_t now_us)
{
    if (!sb || !text)
        return;

    strncpy(sb->text, text, sizeof(sb->text) - 1);
    sb->text[sizeof(sb->text) - 1] = '\0';

    sb->visible      = true;
    sb->show_time_us = now_us;
    sb->auto_hide_ms = auto_hide_ms;
}

void widget_status_bar_hide(widget_status_bar_t *sb)
{
    if (!sb)
        return;
    sb->visible = false;
    sb->text[0] = '\0';
}

bool widget_status_bar_is_visible(const widget_status_bar_t *sb)
{
    return sb && sb->visible;
}

bool widget_status_bar_should_auto_hide(const widget_status_bar_t *sb, int64_t now_us)
{
    if (!sb || !sb->visible || sb->auto_hide_ms == 0)
        return false;
    int64_t elapsed_ms = (now_us - sb->show_time_us) / 1000;
    return elapsed_ms >= sb->auto_hide_ms;
}

/* ============================================================
 * Geometry
 * ============================================================ */

rawdraw_rect_t widget_status_bar_get_bounds(const widget_status_bar_t *sb, int fb_width, int fb_height)
{
    (void)sb;
    int height = WIDGET_STATUS_BAR_HEIGHT;
    int y      = fb_height - height;
    int w      = fb_width;

    /* Reserve the clock zone: if the status bar would overlap the clock's
     * vertical band, narrow the width so we never draw under it. In practice
     * the status bar is at the bottom and the clock is at the top, so there
     * is no overlap — but this guard is here for correctness. */
    rawdraw_rect_t clk = clock_reserved_zone();
    if (y < clk.y + clk.h && y + height > clk.y) {
        w = RD_MIN(w, clk.x);
    }

    rawdraw_rect_t r = {0, y, w, height};
    return r;
}

/* ============================================================
 * Rendering
 * ============================================================ */

bool widget_status_bar_render(widget_status_bar_t *sb, uint8_t *fb, int fb_width, int fb_height)
{
    if (!sb || !fb || !sb->visible)
        return false;

    rawdraw_rect_t bounds = widget_status_bar_get_bounds(sb, fb_width, fb_height);
    bounds                = rawdraw_clamp_rect(bounds, fb_width, fb_height);
    if (rawdraw_rect_area(bounds) <= 0)
        return false;

    rawdraw_paint_style_t bar_style  = rawdraw_theme_component(ROLE_STATUS_BAR);
    rawdraw_color_t       border     = rawdraw_theme_color_for(THEME_TOKEN_BORDER);
    rawdraw_color_t       text_color = bar_style.fg;

    /* Clear background. */
    rawdraw_draw_styled_rect(fb, fb_width, fb_height, bounds, &bar_style);

    /* Draw thin top border line. */
    rawdraw_draw_hline(fb, fb_width, fb_height, bounds.y, bounds.x, bounds.x + bounds.w - 1, border);

    /* Draw text centered vertically within the bar. */
    if (sb->font && sb->text[0] != '\0') {
        int text_h = (int)sb->font->line_height;
        int text_y = bounds.y + (bounds.h - text_h) / 2;
        if (text_y < bounds.y)
            text_y = bounds.y;
        int text_x = bounds.x + STYLE_SPACING_MD;
        rawdraw_draw_text(fb, fb_width, fb_height, text_x, text_y, sb->text, sb->font, (int)text_color);
    }

    return true;
}
