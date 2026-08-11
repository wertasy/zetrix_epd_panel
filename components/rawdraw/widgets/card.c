/**
 * @file card.c
 * @brief Card container widget implementation.
 */
#include "../include/rawdraw_util.h"
#include "card.h"
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

void widget_card_init(widget_card_t *card, int x, int y, int w, int h, int radius)
{
    if (!card)
        return;
    memset(card, 0, sizeof(*card));
    card->bounds.x         = x;
    card->bounds.y         = y;
    card->bounds.w         = w;
    card->bounds.h         = h;
    card->radius           = radius;
    card->border_width     = STYLE_CARD_BORDER_WIDTH;
    card->padding          = STYLE_CARD_PADDING;
    card->title_height     = 0;
    card->title_enabled    = true;
    card->shadow_enabled   = false;
    card->shadow_offset    = STYLE_CARD_SHADOW_OFFSET;
    card->bg_color         = RAWDRAW_COLOR_WHITE;
    card->border_color     = RAWDRAW_COLOR_BLACK;
    card->title_bg_color   = RAWDRAW_COLOR_WHITE;
    card->title_text_color = RAWDRAW_COLOR_BLACK;
    card->shadow_color     = RAWDRAW_COLOR_BLACK;
    card->custom_colors    = false;
}

/* ============================================================
 * Configuration
 * ============================================================ */

void widget_card_set_bounds(widget_card_t *card, int x, int y, int w, int h)
{
    if (!card)
        return;
    card->bounds.x = x;
    card->bounds.y = y;
    card->bounds.w = w;
    card->bounds.h = h;
}

void widget_card_set_radius(widget_card_t *card, int radius)
{
    if (!card)
        return;
    card->radius = radius;
}

void widget_card_set_border_width(widget_card_t *card, int width)
{
    if (!card)
        return;
    card->border_width = width;
}

void widget_card_set_padding(widget_card_t *card, int padding)
{
    if (!card)
        return;
    card->padding = padding;
}

void widget_card_set_title(widget_card_t *card, const char *title)
{
    if (!card)
        return;
    copy_str(card->title, WIDGET_CARD_TITLE_LEN, title);
}

void widget_card_set_title_font(widget_card_t *card, const lv_font_t *font)
{
    if (!card)
        return;
    card->title_font = font;
}

void widget_card_set_title_height(widget_card_t *card, int height)
{
    if (!card)
        return;
    card->title_height = height;
}

void widget_card_set_title_enabled(widget_card_t *card, bool enabled)
{
    if (!card)
        return;
    card->title_enabled = enabled;
}

void widget_card_set_shadow_enabled(widget_card_t *card, bool enabled)
{
    if (!card)
        return;
    card->shadow_enabled = enabled;
}

void widget_card_set_shadow_offset(widget_card_t *card, int offset)
{
    if (!card)
        return;
    card->shadow_offset = offset;
}

void widget_card_set_colors(widget_card_t *card, rawdraw_color_t bg, rawdraw_color_t border)
{
    if (!card)
        return;
    card->bg_color      = bg;
    card->border_color  = border;
    card->custom_colors = true;
}

void widget_card_set_title_colors(widget_card_t *card, rawdraw_color_t bg, rawdraw_color_t text)
{
    if (!card)
        return;
    card->title_bg_color   = bg;
    card->title_text_color = text;
}

void widget_card_set_shadow_color(widget_card_t *card, rawdraw_color_t color)
{
    if (!card)
        return;
    card->shadow_color = color;
}

/* ============================================================
 * Layout
 * ============================================================ */

int widget_card_calculate_title_height(const widget_card_t *card)
{
    if (!card)
        return 0;
    if (!card->title_enabled || card->title[0] == '\0')
        return 0;
    if (card->title_height > 0)
        return card->title_height;
    if (card->title_font)
        return (int)card->title_font->line_height + card->padding * 2;
    return STYLE_CARD_TITLE_HEIGHT;
}

rawdraw_rect_t widget_card_get_bounds(const widget_card_t *card)
{
    rawdraw_rect_t r = {0, 0, 0, 0};
    if (!card)
        return r;
    return card->bounds;
}

rawdraw_rect_t widget_card_get_title_bounds(const widget_card_t *card)
{
    rawdraw_rect_t r = {0, 0, 0, 0};
    if (!card)
        return r;
    int th = widget_card_calculate_title_height(card);
    r.x    = card->bounds.x;
    r.y    = card->bounds.y;
    r.w    = card->bounds.w;
    r.h    = th;
    return r;
}

rawdraw_rect_t widget_card_get_content_bounds(const widget_card_t *card)
{
    rawdraw_rect_t r = {0, 0, 0, 0};
    if (!card)
        return r;
    int th = widget_card_calculate_title_height(card);
    int p  = card->padding;
    r.x    = card->bounds.x + p;
    r.y    = card->bounds.y + th + p;
    r.w    = RD_MAX(0, card->bounds.w - 2 * p);
    r.h    = RD_MAX(0, card->bounds.h - th - 2 * p);
    return r;
}

/* ============================================================
 * Rendering
 * ============================================================ */

void widget_card_render(const widget_card_t *card, uint8_t *fb, int fb_width, int fb_height)
{
    if (!card || !fb || rawdraw_rect_area(card->bounds) <= 0)
        return;

    rawdraw_rect_t bounds = rawdraw_clamp_rect(card->bounds, fb_width, fb_height);
    if (rawdraw_rect_area(bounds) <= 0)
        return;

    /* Clamp radius to valid range (card-specific, vs. button/panel). */
    int half = RD_MIN(bounds.w, bounds.h) / 2;
    int r    = RD_MIN(card->radius, half);
    if (r < 0)
        r = 0;

    rawdraw_paint_style_t card_style = rawdraw_theme_component(ROLE_CARD_DEFAULT);
    if (card->custom_colors) {
        card_style.bg     = card->bg_color;
        card_style.border = card->border_color;
    }

    rawdraw_paint_style_t shadow_style = rawdraw_theme_style(THEME_TOKEN_SHADOW);
    shadow_style.bg                    = card->shadow_color;

    /* 1. Shadow (offset filled rect behind the card) */
    if (card->shadow_enabled && card->shadow_offset > 0) {
        rawdraw_rect_t shadow_rect = {bounds.x + card->shadow_offset, bounds.y + card->shadow_offset, bounds.w,
                                      bounds.h};
        shadow_rect                = rawdraw_clamp_rect(shadow_rect, fb_width, fb_height);
        if (rawdraw_rect_area(shadow_rect) > 0) {
            rawdraw_draw_styled_rect(fb, fb_width, fb_height, shadow_rect, &shadow_style);
        }
    }

    /* 2. Card background + border */
    card_style.border_width = (uint8_t)card->border_width;
    rawdraw_draw_styled_round_rect(fb, fb_width, fb_height, bounds, r, &card_style);

    /* 3. Title bar */
    if (card->title_enabled && card->title[0] != '\0') {
        int th = widget_card_calculate_title_height(card);
        if (th > 0) {
            rawdraw_rect_t title_bg = {bounds.x, bounds.y, bounds.w, th};
            title_bg                = rawdraw_clamp_rect(title_bg, fb_width, fb_height);

            rawdraw_paint_style_t title_style = rawdraw_theme_style(THEME_TOKEN_BACKGROUND_SECONDARY);
            title_style.bg                    = card->title_bg_color;
            title_style.fg                    = card->title_text_color;
            rawdraw_draw_styled_rect(fb, fb_width, fb_height, title_bg, &title_style);

            if (card->title_font) {
                int text_x = bounds.x + card->padding;
                int text_y = bounds.y + card->padding;
                rawdraw_draw_styled_text(fb, fb_width, fb_height, text_x, text_y, card->title, card->title_font,
                                         &title_style);
            }

            /* Separator line below the title bar */
            if (th < bounds.h) {
                int sep_y = bounds.y + th;
                rawdraw_draw_hline(fb, fb_width, fb_height, sep_y, bounds.x, bounds.x + bounds.w - 1,
                                   card_style.border);
            }
        }
    }
}
