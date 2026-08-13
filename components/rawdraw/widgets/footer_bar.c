/**
 * @file footer_bar.c
 * @brief Bottom footer / hint bar widget implementation.
 */
#include "../include/rawdraw_util.h"
#include "footer_bar.h"
#include "../include/rawdraw_ext.h"
#include "../include/theme.h"
#include "../include/layout.h"

#include <string.h>

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
 * Lifecycle
 * ============================================================ */

void widget_footer_bar_init(widget_footer_bar_t *fb, int screen_width, int screen_height)
{
    if (!fb)
        return;
    memset(fb, 0, sizeof(*fb));
    fb->bounds.x = 0;
    fb->bounds.y = screen_height - STYLE_FOOTER_BAR_HEIGHT;
    fb->bounds.w = screen_width;
    fb->bounds.h = STYLE_FOOTER_BAR_HEIGHT;
    fb->font = NULL;
    fb->left_text[0] = '\0';
    fb->center_text[0] = '\0';
    fb->right_text[0] = '\0';
    fb->inverted = false;
}

/* ============================================================
 * Configuration
 * ============================================================ */

void widget_footer_bar_set_bounds(widget_footer_bar_t *fb, int screen_width, int screen_height)
{
    if (!fb)
        return;
    fb->bounds.x = 0;
    fb->bounds.y = screen_height - STYLE_FOOTER_BAR_HEIGHT;
    fb->bounds.w = screen_width;
    fb->bounds.h = STYLE_FOOTER_BAR_HEIGHT;
}

void widget_footer_bar_set_text(widget_footer_bar_t *fb, const char *left, const char *center, const char *right)
{
    if (!fb)
        return;
    copy_str(fb->left_text, WIDGET_FOOTER_BAR_TEXT_LEN, left);
    copy_str(fb->center_text, WIDGET_FOOTER_BAR_TEXT_LEN, center);
    copy_str(fb->right_text, WIDGET_FOOTER_BAR_TEXT_LEN, right);
}

void widget_footer_bar_set_font(widget_footer_bar_t *fb, const lv_font_t *font)
{
    if (!fb || !font)
        return;
    fb->font = font;
}

void widget_footer_bar_set_inverted(widget_footer_bar_t *fb, bool inverted)
{
    if (!fb)
        return;
    fb->inverted = inverted;
}

/* ============================================================
 * Geometry
 * ============================================================ */

rawdraw_rect_t widget_footer_bar_get_bounds(const widget_footer_bar_t *fb)
{
    rawdraw_rect_t r = {0, 0, 0, 0};
    if (!fb)
        return r;
    return fb->bounds;
}

/* ============================================================
 * Rendering
 * ============================================================ */

void widget_footer_bar_render(const widget_footer_bar_t *fb, uint8_t *framebuffer, int fb_width, int fb_height)
{
    if (!fb || !framebuffer || !fb->font)
        return;

    rawdraw_rect_t bounds = rawdraw_clamp_rect(fb->bounds, fb_width, fb_height);
    if (rawdraw_rect_area(bounds) <= 0)
        return;

    rawdraw_paint_style_t normal = rawdraw_theme_component(ROLE_STATUS_BAR);
    rawdraw_paint_style_t selected = rawdraw_theme_style(THEME_TOKEN_SELECTED);

    rawdraw_color_t bg = fb->inverted ? selected.bg : normal.bg;
    rawdraw_color_t fg = fb->inverted ? selected.fg : normal.fg;
    rawdraw_color_t border = fb->inverted ? selected.border : rawdraw_theme_color_for(THEME_TOKEN_BORDER);
    rawdraw_dither_token_t dither = fb->inverted ? selected.dither : normal.dither;

    rawdraw_paint_style_t paint = rawdraw_make_paint(fg, bg, border, dither, 0, REFRESH_STATIC_SAFE);
    rawdraw_draw_styled_rect(framebuffer, fb_width, fb_height, bounds, &paint);

    /* Top divider line. */
    rawdraw_draw_rect(framebuffer, fb_width, fb_height, bounds.x, bounds.y, bounds.w, STYLE_SHELL_DIVIDER_THICKNESS,
                      (int)border);

    int center_y = bounds.y + bounds.h / 2;
    int pad = STYLE_FOOTER_BAR_PADDING;

    /* Determine text_y using ink-centered layout on whichever text exists. */
    const char *primary = fb->left_text[0] != '\0'     ? fb->left_text
                          : fb->center_text[0] != '\0' ? fb->center_text
                          : fb->right_text[0] != '\0'  ? fb->right_text
                                                       : "";
    int text_y = rawdraw_layout_ink_centered_text_top_y(fb->font, primary, center_y, 0);

    /* Left text. */
    if (fb->left_text[0] != '\0') {
        rawdraw_draw_text(framebuffer, fb_width, fb_height, bounds.x + pad, text_y, fb->left_text, fb->font, (int)fg);
    }

    /* Center text. */
    if (fb->center_text[0] != '\0') {
        int center_w = rawdraw_measure_text_width(fb->center_text, fb->font);
        int center_x = bounds.x + RD_MAX(0, (bounds.w - center_w) / 2);
        rawdraw_draw_text(framebuffer, fb_width, fb_height, center_x, text_y, fb->center_text, fb->font, (int)fg);
    }

    /* Right text. */
    if (fb->right_text[0] != '\0') {
        int right_w = rawdraw_measure_text_width(fb->right_text, fb->font);
        int right_x = bounds.x + bounds.w - pad - right_w;
        rawdraw_draw_text(framebuffer, fb_width, fb_height, RD_MAX(bounds.x + pad, right_x), text_y, fb->right_text,
                          fb->font, (int)fg);
    }
}
