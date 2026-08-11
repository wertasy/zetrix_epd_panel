/**
 * @file slider.c
 * @brief Horizontal slider widget implementation.
 */
#include "../include/rawdraw_util.h"
#include "slider.h"
#include "../include/rawdraw_ext.h"
#include "../include/theme.h"
#include "../include/style.h"

#include <stdio.h>
#include <stdlib.h>
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

static void refresh_auto_labels(widget_slider_t *s)
{
    snprintf(s->min_label, sizeof(s->min_label), "%d", s->min_val);
    snprintf(s->max_label, sizeof(s->max_label), "%d", s->max_val);
}

/* ============================================================
 * Lifecycle
 * ============================================================ */

void widget_slider_init(widget_slider_t *s, int x, int y, int w, int h, int min_val, int max_val)
{
    if (!s)
        return;
    memset(s, 0, sizeof(*s));
    s->x                  = x;
    s->y                  = y;
    s->w                  = w;
    s->h                  = h;
    s->min_val            = min_val;
    s->max_val            = max_val;
    s->value              = min_val;
    s->font               = NULL;
    s->callback           = NULL;
    s->callback_user_data = NULL;
    s->track_bg_color     = RAWDRAW_COLOR_WHITE;
    s->track_fill_color   = RAWDRAW_COLOR_BLACK;
    s->thumb_color        = RAWDRAW_COLOR_BLACK;
    s->text_color         = RAWDRAW_COLOR_BLACK;
    s->border_color       = RAWDRAW_COLOR_BLACK;
    s->custom_colors      = false;
    refresh_auto_labels(s);
}

/* ============================================================
 * Configuration
 * ============================================================ */

void widget_slider_set_position(widget_slider_t *s, int x, int y)
{
    if (!s)
        return;
    s->x = x;
    s->y = y;
}

void widget_slider_set_size(widget_slider_t *s, int w, int h)
{
    if (!s)
        return;
    s->w = w;
    s->h = h;
}

void widget_slider_set_range(widget_slider_t *s, int min_val, int max_val)
{
    if (!s)
        return;
    s->min_val = min_val;
    s->max_val = max_val;
    if (s->value < min_val)
        s->value = min_val;
    if (s->value > max_val)
        s->value = max_val;
    refresh_auto_labels(s);
}

void widget_slider_set_value(widget_slider_t *s, int value)
{
    if (!s)
        return;
    s->value = RD_CLAMP(value, s->min_val, s->max_val);
}

int widget_slider_get_value(const widget_slider_t *s)
{
    return s ? s->value : 0;
}

int widget_slider_get_value_percent(const widget_slider_t *s)
{
    if (!s || s->max_val == s->min_val)
        return 0;
    return ((s->value - s->min_val) * 100) / (s->max_val - s->min_val);
}

void widget_slider_set_labels(widget_slider_t *s, const char *min_label, const char *max_label, const char *value_label)
{
    if (!s)
        return;
    if (min_label)
        copy_str(s->min_label, WIDGET_SLIDER_LABEL_LEN, min_label);
    else
        refresh_auto_labels(s);
    if (max_label) {
        /* If only max_label was provided (min_label NULL), refresh already
         * filled min_label above. Only overwrite max_label now. */
        snprintf(s->max_label, sizeof(s->max_label), "%s", max_label);
    }
    if (value_label)
        copy_str(s->value_label, WIDGET_SLIDER_LABEL_LEN, value_label);
    else
        s->value_label[0] = '\0';
}

void widget_slider_set_font(widget_slider_t *s, const lv_font_t *font)
{
    if (!s)
        return;
    s->font = font;
}

void widget_slider_set_callback(widget_slider_t *s, widget_slider_callback_t cb, void *user_data)
{
    if (!s)
        return;
    s->callback           = cb;
    s->callback_user_data = user_data;
}

void widget_slider_set_colors(widget_slider_t *s, rawdraw_color_t track_bg, rawdraw_color_t track_fill,
                              rawdraw_color_t thumb, rawdraw_color_t text)
{
    if (!s)
        return;
    s->track_bg_color   = track_bg;
    s->track_fill_color = track_fill;
    s->thumb_color      = thumb;
    s->text_color       = text;
    s->custom_colors    = true;
}

/* ============================================================
 * State / input
 * ============================================================ */

bool widget_slider_contains(const widget_slider_t *s, int px, int py)
{
    if (!s)
        return false;
    return px >= s->x && px < s->x + s->w && py >= s->y && py < s->y + s->h;
}

bool widget_slider_handle_drag(widget_slider_t *s, int px)
{
    if (!s)
        return false;
    int old_value = s->value;
    int new_value = widget_slider_x_to_value(s, px);
    new_value     = RD_CLAMP(new_value, s->min_val, s->max_val);
    s->value      = new_value;
    if (new_value != old_value) {
        if (s->callback)
            s->callback(new_value, s->callback_user_data);
        return true;
    }
    return false;
}

bool widget_slider_handle_input(widget_slider_t *s, const ui_button_event_t *event)
{
    if (!s || !event)
        return false;

    /* Compute a step that gives ~10 increments across the range. */
    int range = s->max_val - s->min_val;
    if (range <= 0)
        return false;
    int step = RD_MAX(1, range / 10);

    int old_value = s->value;
    switch (event->type) {
    case BTN_DOWN_CLICK:
    case BTN_DOWN_LONG_PRESS:
        s->value = RD_CLAMP(s->value + step, s->min_val, s->max_val);
        break;
    case BTN_UP_CLICK:
    case BTN_UP_LONG_PRESS:
        s->value = RD_CLAMP(s->value - step, s->min_val, s->max_val);
        break;
    default:
        return false;
    }

    if (s->value != old_value) {
        if (s->callback)
            s->callback(s->value, s->callback_user_data);
        return true;
    }
    return false;
}

/* ============================================================
 * Geometry
 * ============================================================ */

rawdraw_rect_t widget_slider_get_bounds(const widget_slider_t *s)
{
    rawdraw_rect_t r = {0, 0, 0, 0};
    if (!s)
        return r;
    r.x = s->x;
    r.y = s->y;
    r.w = s->w;
    r.h = s->h;
    return r;
}

rawdraw_rect_t widget_slider_get_track_bounds(const widget_slider_t *s)
{
    rawdraw_rect_t r = {0, 0, 0, 0};
    if (!s)
        return r;
    /* Track is the horizontal bar portion (middle vertical section). */
    int track_h = RD_MAX(4, s->h / 3);
    int track_y = s->y + (s->h - track_h) / 2;
    r.x         = s->x;
    r.y         = track_y;
    r.w         = s->w;
    r.h         = track_h;
    return r;
}

rawdraw_point_t widget_slider_get_thumb_center(const widget_slider_t *s)
{
    rawdraw_point_t p = {0, 0};
    if (!s)
        return p;
    int pct = widget_slider_get_value_percent(s);
    p.x     = s->x + (s->w * pct) / 100;
    p.y     = s->y + s->h / 2;
    return p;
}

int widget_slider_x_to_value(const widget_slider_t *s, int px)
{
    if (!s || s->w <= 0)
        return s ? s->min_val : 0;
    int clamped_x = RD_CLAMP(px, s->x, s->x + s->w);
    int pct       = ((clamped_x - s->x) * 100) / s->w;
    return s->min_val + (pct * (s->max_val - s->min_val)) / 100;
}

/* ============================================================
 * Rendering
 * ============================================================ */

void widget_slider_render(const widget_slider_t *s, uint8_t *fb, int fb_width, int fb_height)
{
    if (!s || !fb || s->w <= 0 || s->h <= 0)
        return;

    rawdraw_rect_t bounds = rawdraw_clamp_rect(widget_slider_get_bounds(s), fb_width, fb_height);
    if (rawdraw_rect_area(bounds) <= 0)
        return;

    rawdraw_rect_t track = rawdraw_clamp_rect(widget_slider_get_track_bounds(s), fb_width, fb_height);

    int track_radius = track.h / 2; /* Pill shape */
    if (track_radius < 1)
        track_radius = 1;
    int max_r = RD_MIN(track.w, track.h) / 2;
    if (track_radius > max_r)
        track_radius = max_r;

    /* Determine colors: use theme defaults unless custom colors are set. */
    const bool default_colors = !s->custom_colors;

    rawdraw_color_t        track_bg     = s->track_bg_color;
    rawdraw_color_t        track_fill   = s->track_fill_color;
    rawdraw_color_t        thumb_fill   = s->thumb_color;
    rawdraw_color_t        text_color   = s->text_color;
    rawdraw_color_t        border_col   = s->border_color;
    rawdraw_dither_token_t track_dither = DITHER_NONE;

    if (default_colors) {
        rawdraw_paint_style_t progress_style = rawdraw_theme_component(ROLE_PROGRESS);
        track_bg                             = progress_style.bg;
        track_fill                           = progress_style.fg;
        thumb_fill                           = progress_style.border;
        text_color                           = rawdraw_theme_color_for(THEME_TOKEN_TEXT_SECONDARY);
        border_col                           = progress_style.border;
        track_dither                         = progress_style.dither;
    }

    /* Draw track background (styled round rect). */
    rawdraw_paint_style_t track_style =
        rawdraw_make_paint(text_color, track_bg, border_col, track_dither, 1, REFRESH_SMALL_ACCENT);
    rawdraw_draw_styled_round_rect(fb, fb_width, fb_height, track, track_radius, &track_style);

    /* Draw filled portion of track. */
    int pct = widget_slider_get_value_percent(s);
    if (pct > 0) {
        int fill_w = (track.w * pct) / 100;
        if (fill_w > 0) {
            rawdraw_draw_round_rect(fb, fb_width, fb_height, track.x, track.y, fill_w, track.h, track_radius,
                                    (int)track_fill, (int)track_fill, 0);
        }
    }

    /* Draw track border outline. */
    rawdraw_draw_round_rect_border(fb, fb_width, fb_height, track, track_radius, 1, border_col);

    /* Draw thumb (diamond shape — good visibility on 1bpp ePaper). */
    rawdraw_point_t thumb      = widget_slider_get_thumb_center(s);
    int             thumb_size = RD_MAX(4, track.h / 2 + 2);

    for (int dy = -thumb_size; dy <= thumb_size; dy++) {
        int half_w = thumb_size - abs(dy);
        if (half_w <= 0)
            continue;
        int ly = thumb.y + dy;
        if (ly < 0 || ly >= fb_height)
            continue;
        rawdraw_draw_hline(fb, fb_width, fb_height, ly, thumb.x - half_w, thumb.x + half_w, thumb_fill);
    }

    /* Draw labels. */
    if (s->font) {
        int line_h = (int)s->font->line_height;

        /* Min label. */
        if (s->min_label[0] != '\0') {
            int text_x = s->x;
            int text_y = s->y - line_h - STYLE_SPACING_XS;
            if (text_y < 0)
                text_y = s->y + s->h + STYLE_SPACING_XS;
            rawdraw_draw_text(fb, fb_width, fb_height, text_x, text_y, s->min_label, s->font, (int)text_color);
        }

        /* Max label. */
        if (s->max_label[0] != '\0') {
            int text_w = rawdraw_measure_text_width(s->max_label, s->font);
            int text_x = s->x + s->w - text_w;
            int text_y = s->y - line_h - STYLE_SPACING_XS;
            if (text_y < 0)
                text_y = s->y + s->h + STYLE_SPACING_XS;
            rawdraw_draw_text(fb, fb_width, fb_height, text_x, text_y, s->max_label, s->font, (int)text_color);
        }

        /* Value label above thumb. */
        if (s->value_label[0] != '\0') {
            int text_w = rawdraw_measure_text_width(s->value_label, s->font);
            int text_x = thumb.x - text_w / 2;
            int text_y = thumb.y - thumb_size - line_h - STYLE_SPACING_XS;
            if (text_y < 0)
                text_y = thumb.y + thumb_size + STYLE_SPACING_XS;
            rawdraw_draw_text(fb, fb_width, fb_height, text_x, text_y, s->value_label, s->font, (int)text_color);
        }
    }
}
