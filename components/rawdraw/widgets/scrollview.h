/**
 * @file scrollview.h
 * @brief Scrollable container widget — C port of rawdraw::ScrollView.
 *
 * Manages a scrollable content area with an optional scrollbar indicator.
 * Content is drawn via a callback that receives the visible content rect
 * and a screen-space clip rect. The scrollbar is drawn automatically when
 * content exceeds the visible height.
 *
 * All draw functions take (fb, fb_width, fb_height) — no global state.
 *
 * Porting notes (C++ -> C):
 *  - std::function callback -> function pointer + user_data.
 *  - std::max / std::min -> inline clamping.
 */
#ifndef WIDGETS_SCROLLVIEW_H_
#define WIDGETS_SCROLLVIEW_H_

#include <stdint.h>
#include <stdbool.h>
#include "../include/rawdraw.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Content drawing callback type.
 *
 * @param fb            Framebuffer.
 * @param width         Framebuffer width.
 * @param height        Framebuffer height.
 * @param visible_rect  Currently visible content region (relative to content).
 * @param clip_rect     Clipping region in screen coordinates.
 * @param user_data     Opaque user pointer.
 */
typedef void (*widget_scrollview_content_draw_cb_t)(uint8_t *fb, int width, int height, rawdraw_rect_t visible_rect,
                                                    rawdraw_rect_t clip_rect, void *user_data);

typedef struct {
    rawdraw_rect_t bounds; /* visible area on screen */
    int content_height; /* total scrollable content height */
    int scroll_offset; /* current scroll position (Y offset) */
    int scrollbar_width; /* scrollbar indicator width */
    bool scrollbar_enabled; /* show scrollbar */
} widget_scrollview_t;

/* ---- lifecycle ---- */
void widget_scrollview_init(widget_scrollview_t *sv, int x, int y, int w, int h, int content_height);

/* ---- configuration ---- */
void widget_scrollview_set_bounds(widget_scrollview_t *sv, int x, int y, int w, int h);
void widget_scrollview_set_content_height(widget_scrollview_t *sv, int height);
void widget_scrollview_set_scrollbar_width(widget_scrollview_t *sv, int width);
void widget_scrollview_set_scrollbar_enabled(widget_scrollview_t *sv, bool enabled);

/* ---- scroll control ---- */
void widget_scrollview_set_scroll_offset(widget_scrollview_t *sv, int offset);
int widget_scrollview_get_scroll_offset(const widget_scrollview_t *sv);
void widget_scrollview_scroll_to_end(widget_scrollview_t *sv);
void widget_scrollview_scroll_by(widget_scrollview_t *sv, int delta);
int widget_scrollview_get_max_scroll_offset(const widget_scrollview_t *sv);
bool widget_scrollview_can_scroll_up(const widget_scrollview_t *sv);
bool widget_scrollview_can_scroll_down(const widget_scrollview_t *sv);

/* ---- rendering ---- */
/**
 * @brief Draw the scrollview: invoke the content callback, then draw scrollbar.
 *
 * @param sv         Scrollview.
 * @param fb         Framebuffer.
 * @param fb_width   Framebuffer width.
 * @param fb_height  Framebuffer height.
 * @param draw_cb    Content drawing callback (may be NULL to draw scrollbar only).
 * @param user_data  Opaque pointer passed to draw_cb.
 */
void widget_scrollview_render(widget_scrollview_t *sv, uint8_t *fb, int fb_width, int fb_height,
                              widget_scrollview_content_draw_cb_t draw_cb, void *user_data);

/* ---- geometry ---- */
rawdraw_rect_t widget_scrollview_get_visible_content_rect(const widget_scrollview_t *sv);
rawdraw_rect_t widget_scrollview_get_bounds(const widget_scrollview_t *sv);

#ifdef __cplusplus
}
#endif

#endif /* WIDGETS_SCROLLVIEW_H_ */
