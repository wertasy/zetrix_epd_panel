/**
 * @file panel.c
 * @brief Panel container widget implementation.
 */
#include "../include/rawdraw_util.h"
#include "panel.h"
#include "../include/rawdraw_ext.h"
#include "../include/theme.h"
#include "../include/style.h"
#include <string.h>

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
 * Lifecycle
 * ============================================================ */

void widget_panel_init(widget_panel_t *panel, int x, int y, int w, int h, int radius)
{
    if (!panel)
        return;
    memset(panel, 0, sizeof(*panel));
    panel->bounds.x         = x;
    panel->bounds.y         = y;
    panel->bounds.w         = w;
    panel->bounds.h         = h;
    panel->radius           = radius;
    panel->title_height     = 0;
    panel->padding          = STYLE_PANEL_PADDING;
    panel->border_width     = STYLE_PANEL_BORDER_WIDTH;
    panel->title_enabled    = true;
    panel->bg_color         = RAWDRAW_COLOR_WHITE;
    panel->border_color     = RAWDRAW_COLOR_BLACK;
    panel->title_bg_color   = RAWDRAW_COLOR_WHITE;
    panel->title_text_color = RAWDRAW_COLOR_BLACK;
}

/* ============================================================
 * Configuration
 * ============================================================ */

void widget_panel_set_bounds(widget_panel_t *panel, int x, int y, int w, int h)
{
    if (!panel)
        return;
    panel->bounds.x = x;
    panel->bounds.y = y;
    panel->bounds.w = w;
    panel->bounds.h = h;
}

void widget_panel_set_radius(widget_panel_t *panel, int radius)
{
    if (!panel)
        return;
    panel->radius = radius;
}

void widget_panel_set_title(widget_panel_t *panel, const char *title)
{
    if (!panel)
        return;
    copy_str(panel->title, WIDGET_PANEL_TITLE_LEN, title);
}

void widget_panel_set_title_font(widget_panel_t *panel, const lv_font_t *font)
{
    if (!panel)
        return;
    panel->title_font = font;
}

void widget_panel_set_title_height(widget_panel_t *panel, int height)
{
    if (!panel)
        return;
    panel->title_height = height;
}

void widget_panel_set_padding(widget_panel_t *panel, int padding)
{
    if (!panel)
        return;
    panel->padding = padding;
}

void widget_panel_set_border_width(widget_panel_t *panel, int width)
{
    if (!panel)
        return;
    panel->border_width = width;
}

void widget_panel_set_title_enabled(widget_panel_t *panel, bool enabled)
{
    if (!panel)
        return;
    panel->title_enabled = enabled;
}

void widget_panel_set_colors(widget_panel_t *panel, rawdraw_color_t bg, rawdraw_color_t border)
{
    if (!panel)
        return;
    panel->bg_color     = bg;
    panel->border_color = border;
}

void widget_panel_set_title_colors(widget_panel_t *panel, rawdraw_color_t bg, rawdraw_color_t text)
{
    if (!panel)
        return;
    panel->title_bg_color   = bg;
    panel->title_text_color = text;
}

/* ============================================================
 * Layout
 * ============================================================ */

int widget_panel_calculate_title_height(const widget_panel_t *panel)
{
    if (!panel)
        return 0;
    if (!panel->title_enabled || panel->title[0] == '\0')
        return 0;
    if (panel->title_height > 0)
        return panel->title_height;
    if (panel->title_font)
        return (int)panel->title_font->line_height + panel->padding * 2;
    return STYLE_PANEL_TITLE_HEIGHT;
}

rawdraw_rect_t widget_panel_get_bounds(const widget_panel_t *panel)
{
    rawdraw_rect_t r = {0, 0, 0, 0};
    if (!panel)
        return r;
    return panel->bounds;
}

rawdraw_rect_t widget_panel_get_title_bounds(const widget_panel_t *panel)
{
    rawdraw_rect_t r = {0, 0, 0, 0};
    if (!panel)
        return r;
    int th = widget_panel_calculate_title_height(panel);
    r.x    = panel->bounds.x;
    r.y    = panel->bounds.y;
    r.w    = panel->bounds.w;
    r.h    = th;
    return r;
}

rawdraw_rect_t widget_panel_get_content_bounds(const widget_panel_t *panel)
{
    rawdraw_rect_t r = {0, 0, 0, 0};
    if (!panel)
        return r;
    int th = widget_panel_calculate_title_height(panel);
    int p  = panel->padding;
    r.x    = panel->bounds.x + p;
    r.y    = panel->bounds.y + th + p;
    r.w    = panel->bounds.w - 2 * p;
    r.h    = panel->bounds.h - th - 2 * p;
    return r;
}

/* ============================================================
 * Rendering
 * ============================================================ */

void widget_panel_render(const widget_panel_t *panel, uint8_t *fb, int fb_width, int fb_height)
{
    if (!panel || !fb || rawdraw_rect_area(panel->bounds) <= 0)
        return;

    rawdraw_rect_t bounds = rawdraw_clamp_rect(panel->bounds, fb_width, fb_height);
    if (rawdraw_rect_area(bounds) <= 0)
        return;

    rawdraw_paint_style_t panel_style = rawdraw_theme_component(ROLE_PANEL);
    if (panel->bg_color != RAWDRAW_COLOR_WHITE || panel->border_color != RAWDRAW_COLOR_BLACK) {
        panel_style.bg     = panel->bg_color;
        panel_style.border = panel->border_color;
    }
    panel_style.border_width = (uint8_t)panel->border_width;

    /* Panel background + border */
    rawdraw_draw_styled_round_rect(fb, fb_width, fb_height, bounds, panel->radius, &panel_style);

    /* Title bar */
    if (panel->title_enabled && panel->title[0] != '\0') {
        int th = widget_panel_calculate_title_height(panel);
        if (th > 0) {
            rawdraw_rect_t title_bg = {panel->bounds.x, panel->bounds.y, panel->bounds.w, th};
            title_bg                = rawdraw_clamp_rect(title_bg, fb_width, fb_height);

            rawdraw_paint_style_t title_style = rawdraw_theme_style(THEME_TOKEN_BACKGROUND_SECONDARY);
            title_style.bg                    = panel->title_bg_color;
            title_style.fg                    = panel->title_text_color;
            rawdraw_draw_styled_rect(fb, fb_width, fb_height, title_bg, &title_style);

            if (panel->title_font) {
                int text_x = panel->bounds.x + panel->padding;
                int text_y = panel->bounds.y + panel->padding;
                rawdraw_draw_styled_text(fb, fb_width, fb_height, text_x, text_y, panel->title, panel->title_font,
                                         &title_style);
            }

            /* Separator line below the title bar */
            if (th < panel->bounds.h) {
                int sep_y = panel->bounds.y + th;
                rawdraw_draw_hline(fb, fb_width, fb_height, sep_y, panel->bounds.x,
                                   panel->bounds.x + panel->bounds.w - 1, panel_style.border);
            }
        }
    }
}
