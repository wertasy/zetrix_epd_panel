/**
 * @file toggle.c
 * @brief Toggle switch widget implementation.
 */
#include "../include/rawdraw_util.h"
#include "toggle.h"
#include "../include/rawdraw_ext.h"
#include "../include/theme.h"
#include "../include/style.h"

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

void widget_toggle_init(widget_toggle_t *t, int x, int y, int w, int h)
{
    if (!t)
        return;
    memset(t, 0, sizeof(*t));
    t->x                  = x;
    t->y                  = y;
    t->w                  = w;
    t->h                  = h;
    t->state              = false;
    t->font               = NULL;
    t->callback           = NULL;
    t->callback_user_data = NULL;
    t->track_on_color     = RAWDRAW_COLOR_BLACK;
    t->track_off_color    = RAWDRAW_COLOR_WHITE;
    t->thumb_color        = RAWDRAW_COLOR_WHITE;
    t->border_color       = RAWDRAW_COLOR_BLACK;
}

/* ============================================================
 * Configuration
 * ============================================================ */

void widget_toggle_set_position(widget_toggle_t *t, int x, int y)
{
    if (!t)
        return;
    t->x = x;
    t->y = y;
}

void widget_toggle_set_size(widget_toggle_t *t, int w, int h)
{
    if (!t)
        return;
    t->w = w;
    t->h = h;
}

void widget_toggle_set_state(widget_toggle_t *t, bool on)
{
    if (!t)
        return;
    t->state = on;
}

bool widget_toggle_get_state(const widget_toggle_t *t)
{
    return t ? t->state : false;
}

void widget_toggle_set_label(widget_toggle_t *t, const char *label)
{
    if (!t)
        return;
    copy_str(t->label, WIDGET_TOGGLE_LABEL_LEN, label);
}

void widget_toggle_set_font(widget_toggle_t *t, const lv_font_t *font)
{
    if (!t)
        return;
    t->font = font;
}

void widget_toggle_set_callback(widget_toggle_t *t, widget_toggle_callback_t cb, void *user_data)
{
    if (!t)
        return;
    t->callback           = cb;
    t->callback_user_data = user_data;
}

void widget_toggle_set_colors(widget_toggle_t *t, rawdraw_color_t track_on, rawdraw_color_t track_off,
                              rawdraw_color_t thumb, rawdraw_color_t border)
{
    if (!t)
        return;
    t->track_on_color  = track_on;
    t->track_off_color = track_off;
    t->thumb_color     = thumb;
    t->border_color    = border;
}

/* ============================================================
 * State / input
 * ============================================================ */

bool widget_toggle_contains(const widget_toggle_t *t, int px, int py)
{
    if (!t)
        return false;
    /* Use track bounds for hit-testing (not label). */
    return px >= t->x && px < t->x + t->w && py >= t->y && py < t->y + t->h;
}

void widget_toggle_handle_tap(widget_toggle_t *t)
{
    if (!t)
        return;
    t->state = !t->state;
    if (t->callback)
        t->callback(t->state, t->callback_user_data);
}

bool widget_toggle_handle_input(widget_toggle_t *t, const ui_button_event_t *event)
{
    if (!t || !event)
        return false;
    if (event->type == BTN_BOOT_CLICK) {
        widget_toggle_handle_tap(t);
        return true;
    }
    return false;
}

/* ============================================================
 * Geometry
 * ============================================================ */

rawdraw_rect_t widget_toggle_get_track_bounds(const widget_toggle_t *t)
{
    rawdraw_rect_t r = {0, 0, 0, 0};
    if (!t)
        return r;
    r.x = t->x;
    r.y = t->y;
    r.w = t->w;
    r.h = t->h;
    return r;
}

rawdraw_rect_t widget_toggle_get_bounds(const widget_toggle_t *t, int screen_width)
{
    rawdraw_rect_t r = {0, 0, 0, 0};
    if (!t)
        return r;
    int total_w = t->w;
    if (t->label[0] != '\0' && t->font) {
        total_w = t->w + STYLE_SPACING_SM + rawdraw_measure_text_width(t->label, t->font);
    }
    (void)screen_width;
    r.x = t->x;
    r.y = t->y;
    r.w = total_w;
    r.h = t->h;
    return r;
}

rawdraw_point_t widget_toggle_get_thumb_center(const widget_toggle_t *t)
{
    rawdraw_point_t p = {0, 0};
    if (!t)
        return p;
    int radius  = t->h / 2;
    int padding = STYLE_TOGGLE_PADDING;
    p.x         = t->state ? (t->x + t->w - radius - padding) : (t->x + radius + padding);
    p.y         = t->y + t->h / 2;
    return p;
}

/* ============================================================
 * Rendering
 * ============================================================ */

void widget_toggle_render(const widget_toggle_t *t, uint8_t *fb, int fb_width, int fb_height)
{
    if (!t || !fb || t->w <= 0 || t->h <= 0)
        return;

    rawdraw_rect_t track = rawdraw_clamp_rect(widget_toggle_get_track_bounds(t), fb_width, fb_height);
    if (rawdraw_rect_area(track) <= 0)
        return;

    int radius = t->h / 2; /* Pill shape */
    if (radius < 1)
        radius = 1;

    /* Determine colors: use theme defaults unless custom colors are set. */
    const bool default_colors = t->track_on_color == RAWDRAW_COLOR_BLACK && t->track_off_color == RAWDRAW_COLOR_WHITE &&
                                t->thumb_color == RAWDRAW_COLOR_WHITE && t->border_color == RAWDRAW_COLOR_BLACK;

    rawdraw_color_t        track_fill   = t->state ? t->track_on_color : t->track_off_color;
    rawdraw_color_t        track_border = t->border_color;
    rawdraw_color_t        thumb_fill   = t->state ? t->thumb_color : RAWDRAW_COLOR_BLACK;
    rawdraw_color_t        thumb_inner  = RAWDRAW_COLOR_WHITE;
    rawdraw_color_t        label_color  = RAWDRAW_COLOR_BLACK;
    rawdraw_dither_token_t track_dither = DITHER_NONE;

    if (default_colors) {
        rawdraw_paint_style_t active_style =
            rawdraw_theme_component(t->state ? ROLE_SETTINGS_SELECTED : ROLE_SETTINGS_ROW);
        rawdraw_paint_style_t disabled_style = rawdraw_theme_style(THEME_TOKEN_DISABLED);

        track_fill   = active_style.bg;
        track_border = active_style.border;
        track_dither = t->state ? DITHER_NONE : disabled_style.dither;
        thumb_fill   = t->state ? active_style.fg : active_style.border;
        thumb_inner  = t->state ? rawdraw_theme_color_for(THEME_TOKEN_BACKGROUND_PRIMARY) : active_style.bg;
        label_color  = rawdraw_theme_color_for(THEME_TOKEN_TEXT_PRIMARY);
    }

    /* Draw track background (pill shape, styled). */
    rawdraw_paint_style_t track_style =
        rawdraw_make_paint(track_fill, track_fill, track_border, track_dither, 1, REFRESH_SMALL_ACCENT);
    rawdraw_draw_styled_round_rect(fb, fb_width, fb_height, track, radius, &track_style);

    /* Draw thumb circle. */
    rawdraw_point_t thumb   = widget_toggle_get_thumb_center(t);
    int             thumb_r = radius - 2;
    if (thumb_r < 1)
        thumb_r = 1;

    rawdraw_draw_circle(fb, fb_width, fb_height, thumb, thumb_r, thumb_fill);
    if (!t->state && thumb_r > 2) {
        /* OFF: outlined circle — fill inner area with background color. */
        rawdraw_draw_circle(fb, fb_width, fb_height, thumb, thumb_r - 2, thumb_inner);
    }

    /* Draw label to the right of toggle. */
    if (t->label[0] != '\0' && t->font) {
        int text_x = t->x + t->w + STYLE_SPACING_SM;
        int line_h = (int)t->font->line_height;
        int text_y = t->y + (t->h - line_h) / 2;
        rawdraw_draw_text(fb, fb_width, fb_height, text_x, text_y, t->label, t->font, (int)label_color);
    }
}
