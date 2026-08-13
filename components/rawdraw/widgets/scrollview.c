/**
 * @file scrollview.c
 * @brief Scrollable container widget implementation.
 */
#include "../include/rawdraw_util.h"
#include "scrollview.h"
#include "../include/rawdraw_ext.h"
#include "../include/theme.h"
#include "../include/style.h"
#include <string.h>
/* ============================================================
 * Local helpers
 * ============================================================ */

/* ============================================================
 * Lifecycle
 * ============================================================ */

void widget_scrollview_init(widget_scrollview_t *sv, int x, int y, int w, int h, int content_height)
{
    if (!sv)
        return;
    memset(sv, 0, sizeof(*sv));
    sv->bounds.x = x;
    sv->bounds.y = y;
    sv->bounds.w = w;
    sv->bounds.h = h;
    sv->content_height = content_height;
    sv->scroll_offset = 0;
    sv->scrollbar_width = STYLE_SCROLLBAR_WIDTH;
    sv->scrollbar_enabled = true;
}

/* ============================================================
 * Configuration
 * ============================================================ */

void widget_scrollview_set_bounds(widget_scrollview_t *sv, int x, int y, int w, int h)
{
    if (!sv)
        return;
    sv->bounds.x = x;
    sv->bounds.y = y;
    sv->bounds.w = w;
    sv->bounds.h = h;
    /* Re-clamp scroll offset to valid range. */
    int max = widget_scrollview_get_max_scroll_offset(sv);
    if (sv->scroll_offset > max)
        sv->scroll_offset = max;
}

void widget_scrollview_set_content_height(widget_scrollview_t *sv, int height)
{
    if (!sv)
        return;
    sv->content_height = height;
    /* Clamp scroll offset to valid range. */
    int max = widget_scrollview_get_max_scroll_offset(sv);
    if (sv->scroll_offset > max)
        sv->scroll_offset = max;
}

void widget_scrollview_set_scrollbar_width(widget_scrollview_t *sv, int width)
{
    if (!sv)
        return;
    sv->scrollbar_width = width;
}

void widget_scrollview_set_scrollbar_enabled(widget_scrollview_t *sv, bool enabled)
{
    if (!sv)
        return;
    sv->scrollbar_enabled = enabled;
}

/* ============================================================
 * Scroll control
 * ============================================================ */

void widget_scrollview_set_scroll_offset(widget_scrollview_t *sv, int offset)
{
    if (!sv)
        return;
    sv->scroll_offset = RD_CLAMP(offset, 0, widget_scrollview_get_max_scroll_offset(sv));
}

int widget_scrollview_get_scroll_offset(const widget_scrollview_t *sv)
{
    return sv ? sv->scroll_offset : 0;
}

void widget_scrollview_scroll_to_end(widget_scrollview_t *sv)
{
    if (!sv)
        return;
    sv->scroll_offset = widget_scrollview_get_max_scroll_offset(sv);
}

void widget_scrollview_scroll_by(widget_scrollview_t *sv, int delta)
{
    if (!sv)
        return;
    widget_scrollview_set_scroll_offset(sv, sv->scroll_offset + delta);
}

int widget_scrollview_get_max_scroll_offset(const widget_scrollview_t *sv)
{
    if (!sv)
        return 0;
    int max = sv->content_height - sv->bounds.h;
    return max > 0 ? max : 0;
}

bool widget_scrollview_can_scroll_up(const widget_scrollview_t *sv)
{
    return sv ? (sv->scroll_offset > 0) : false;
}

bool widget_scrollview_can_scroll_down(const widget_scrollview_t *sv)
{
    if (!sv)
        return false;
    return sv->scroll_offset < widget_scrollview_get_max_scroll_offset(sv);
}

/* ============================================================
 * Geometry
 * ============================================================ */

rawdraw_rect_t widget_scrollview_get_visible_content_rect(const widget_scrollview_t *sv)
{
    rawdraw_rect_t r = {0, 0, 0, 0};
    if (!sv)
        return r;
    r.x = sv->bounds.x;
    r.y = sv->scroll_offset;
    r.w = sv->bounds.w - sv->scrollbar_width;
    r.h = sv->bounds.h;
    return r;
}

rawdraw_rect_t widget_scrollview_get_bounds(const widget_scrollview_t *sv)
{
    rawdraw_rect_t r = {0, 0, 0, 0};
    if (!sv)
        return r;
    return sv->bounds;
}

/* ============================================================
 * Rendering
 * ============================================================ */

static void draw_scrollbar(const widget_scrollview_t *sv, uint8_t *fb, int fb_width, int fb_height)
{
    if (!sv->scrollbar_enabled || sv->content_height <= sv->bounds.h)
        return;

    int sb_x = sv->bounds.x + sv->bounds.w - sv->scrollbar_width;

    /* Scrollbar height proportional to visible content ratio. */
    float visible_ratio = (float)sv->bounds.h / (float)sv->content_height;
    int sb_height = (int)(sv->bounds.h * visible_ratio);
    if (sb_height < STYLE_SCROLLBAR_MIN_H)
        sb_height = STYLE_SCROLLBAR_MIN_H;

    /* Scrollbar position based on scroll offset. */
    int max_offset = widget_scrollview_get_max_scroll_offset(sv);
    float scroll_ratio = max_offset > 0 ? (float)sv->scroll_offset / (float)max_offset : 0.0f;
    int sb_y = sv->bounds.y + (int)((sv->bounds.h - sb_height) * scroll_ratio);

    rawdraw_paint_style_t track = rawdraw_theme_style(THEME_TOKEN_BACKGROUND_SECONDARY);
    rawdraw_color_t thumb = rawdraw_theme_color_for(THEME_TOKEN_SELECTED);

    /* Draw scrollbar background (track) */
    rawdraw_rect_t track_rect = {sb_x, sv->bounds.y, sv->scrollbar_width, sv->bounds.h};
    rawdraw_draw_styled_rect(fb, fb_width, fb_height, track_rect, &track);

    /* Draw scrollbar indicator (thumb) */
    rawdraw_rect_t thumb_rect = {sb_x, sb_y, sv->scrollbar_width, sb_height};
    rawdraw_fill_rect(fb, fb_width, fb_height, thumb_rect, thumb);
}

void widget_scrollview_render(widget_scrollview_t *sv, uint8_t *fb, int fb_width, int fb_height,
                              widget_scrollview_content_draw_cb_t draw_cb, void *user_data)
{
    if (!sv || !fb || rawdraw_rect_area(sv->bounds) <= 0)
        return;

    /* Draw content at scroll offset via callback. */
    if (draw_cb) {
        rawdraw_rect_t visible = widget_scrollview_get_visible_content_rect(sv);
        rawdraw_rect_t clip = sv->bounds;
        draw_cb(fb, fb_width, fb_height, visible, clip, user_data);
    }

    /* Draw scrollbar indicator. */
    draw_scrollbar(sv, fb, fb_width, fb_height);
}
