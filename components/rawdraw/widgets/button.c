/**
 * @file button.c
 * @brief Button widget implementation.
 */
#include "../include/rawdraw_util.h"
#include "button.h"
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

void widget_button_init(widget_button_t *btn, int x, int y, int w, int h)
{
    if (!btn)
        return;
    memset(btn, 0, sizeof(*btn));
    btn->x = x;
    btn->y = y;
    btn->w = w;
    btn->h = h;
    btn->radius = STYLE_BUTTON_RADIUS;
    btn->pressed = false;
    btn->callback = NULL;
    btn->callback_user_data = NULL;
    btn->bg_color = RAWDRAW_COLOR_WHITE;
    btn->fg_color = RAWDRAW_COLOR_BLACK;
    btn->border_color = RAWDRAW_COLOR_BLACK;
}

/* ============================================================
 * Configuration
 * ============================================================ */

void widget_button_set_position(widget_button_t *btn, int x, int y)
{
    if (!btn)
        return;
    btn->x = x;
    btn->y = y;
}

void widget_button_set_size(widget_button_t *btn, int w, int h)
{
    if (!btn)
        return;
    btn->w = w;
    btn->h = h;
}

void widget_button_set_icon(widget_button_t *btn, const char *icon_code)
{
    if (!btn)
        return;
    copy_str(btn->icon_code, WIDGET_BUTTON_ICON_LEN, icon_code);
}

void widget_button_set_icon_font(widget_button_t *btn, const lv_font_t *font)
{
    if (!btn)
        return;
    btn->icon_font = font;
}

void widget_button_set_text(widget_button_t *btn, const char *text)
{
    if (!btn)
        return;
    copy_str(btn->text, WIDGET_BUTTON_TEXT_LEN, text);
}

void widget_button_set_text_font(widget_button_t *btn, const lv_font_t *font)
{
    if (!btn)
        return;
    btn->text_font = font;
}

void widget_button_set_radius(widget_button_t *btn, int radius)
{
    if (!btn)
        return;
    btn->radius = radius;
}

void widget_button_set_callback(widget_button_t *btn, widget_button_callback_t cb, void *user_data)
{
    if (!btn)
        return;
    btn->callback = cb;
    btn->callback_user_data = user_data;
}

void widget_button_set_colors(widget_button_t *btn, rawdraw_color_t bg, rawdraw_color_t fg, rawdraw_color_t border)
{
    if (!btn)
        return;
    btn->bg_color = bg;
    btn->fg_color = fg;
    btn->border_color = border;
}

/* ============================================================
 * State / input
 * ============================================================ */

bool widget_button_contains(const widget_button_t *btn, int px, int py)
{
    if (!btn)
        return false;
    return px >= btn->x && px < btn->x + btn->w && py >= btn->y && py < btn->y + btn->h;
}

void widget_button_set_pressed(widget_button_t *btn, bool pressed)
{
    if (!btn)
        return;
    btn->pressed = pressed;
}

bool widget_button_is_pressed(const widget_button_t *btn)
{
    return btn ? btn->pressed : false;
}

void widget_button_handle_press(widget_button_t *btn)
{
    if (!btn)
        return;
    widget_button_set_pressed(btn, true);
    if (btn->callback) {
        btn->callback(btn->callback_user_data);
    }
}

bool widget_button_handle_input(widget_button_t *btn, const ui_button_event_t *event)
{
    if (!btn || !event)
        return false;
    /* The boot button is the confirm/select key; it activates the button. */
    if (event->type == BTN_BOOT_CLICK) {
        widget_button_handle_press(btn);
        return true;
    }
    return false;
}

/* ============================================================
 * Geometry / rendering
 * ============================================================ */

rawdraw_rect_t widget_button_get_bounds(const widget_button_t *btn)
{
    rawdraw_rect_t r = {0, 0, 0, 0};
    if (!btn)
        return r;
    r.x = btn->x;
    r.y = btn->y;
    r.w = btn->w;
    r.h = btn->h;
    return r;
}

void widget_button_render(const widget_button_t *btn, uint8_t *fb, int fb_width, int fb_height)
{
    if (!btn || !fb)
        return;

    rawdraw_rect_t bounds = widget_button_get_bounds(btn);
    bounds = rawdraw_clamp_rect(bounds, fb_width, fb_height);
    if (rawdraw_rect_area(bounds) <= 0)
        return;

    rawdraw_paint_style_t style = rawdraw_theme_component(btn->pressed ? ROLE_BUTTON_SELECTED : ROLE_BUTTON_NORMAL);

    /* Custom colors override the theme; pressed state inverts bg/fg/border. */
    if (btn->bg_color != RAWDRAW_COLOR_WHITE || btn->fg_color != RAWDRAW_COLOR_BLACK ||
        btn->border_color != RAWDRAW_COLOR_BLACK) {
        style.bg = btn->pressed ? btn->fg_color : btn->bg_color;
        style.fg = btn->pressed ? btn->bg_color : btn->fg_color;
        style.border = btn->pressed ? btn->fg_color : btn->border_color;
    }

    /* Background */
    rawdraw_draw_styled_round_rect(fb, fb_width, fb_height, bounds, btn->radius, &style);

    /* Icon, centered (shifted up when a label is present) */
    if (btn->icon_code[0] != '\0' && btn->icon_font) {
        int icon_size = (int)btn->icon_font->line_height;
        int icon_x = btn->x + (btn->w - icon_size) / 2;
        int text_half = 0;
        if (btn->text[0] != '\0') {
            text_half = btn->text_font ? (int)(btn->text_font->line_height / 2) : STYLE_SPACING_MD;
        }
        int icon_y = btn->y + (btn->h - icon_size) / 2 - text_half;
        rawdraw_draw_styled_icon(fb, fb_width, fb_height, icon_x, icon_y, btn->icon_code, btn->icon_font, &style);
    }

    /* Label, below the icon */
    if (btn->text[0] != '\0' && btn->text_font) {
        int text_w = rawdraw_measure_text_width(btn->text, btn->text_font);
        int text_x = btn->x + (btn->w - text_w) / 2;
        int text_y = btn->y + btn->h - (int)btn->text_font->line_height - STYLE_SPACING_XS;
        rawdraw_draw_styled_text(fb, fb_width, fb_height, text_x, text_y, btn->text, btn->text_font, &style);
    }
}
