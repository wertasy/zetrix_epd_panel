/**
 * @file status_bar.h
 * @brief Bottom status bar widget — C port of rawdraw::StatusBarState.
 *
 * Displays short status text at the bottom of the screen with auto-hide timer.
 *
 * Design constraints:
 *  - Position: bottom of screen (Y = SCREEN_HEIGHT - height).
 *  - Auto-hide: caller-driven via widget_status_bar_should_auto_hide().
 *  - Reserves the clock zone (epd_clock_reserved_zone) to avoid overlap.
 *
 * All functions take explicit dimensions — no global state. Time is supplied
 * by the caller (now_us) so the widget is host-testable without esp_timer.
 *
 * Porting notes (C++ -> C):
 *  - StatusBarState -> widget_status_bar_t struct.
 *  - esp_timer_get_time() removed; caller provides now_us to show/should_hide.
 *  - RegionRefresh counter omitted; draw() returns dirty flag instead.
 *  - Clock reserved zone computed from CLOCK_* constants (header-only, no link).
 */
#ifndef WIDGETS_STATUS_BAR_H_
#define WIDGETS_STATUS_BAR_H_

#include <stdint.h>
#include <stdbool.h>
#include "../include/rawdraw.h"
#include "../include/font_engine.h"
#include "../include/style.h"
#include "../include/clock.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Bottom status bar geometry. */
#define WIDGET_STATUS_BAR_HEIGHT 18
#define WIDGET_STATUS_BAR_Y (STYLE_SCREEN_HEIGHT - WIDGET_STATUS_BAR_HEIGHT)
#define WIDGET_STATUS_BAR_AUTO_HIDE_MS 3000
#define WIDGET_STATUS_BAR_TEXT_LEN 64

typedef struct {
    bool visible;
    char text[WIDGET_STATUS_BAR_TEXT_LEN];
    int64_t show_time_us; /* Timestamp when shown (caller-supplied, microseconds) */
    int64_t auto_hide_ms; /* Auto-hide delay in ms (0 = never) */
    const lv_font_t *font;
} widget_status_bar_t;

/* ---- lifecycle ---- */
void widget_status_bar_init(widget_status_bar_t *sb, const lv_font_t *font);

/* ---- control ---- */
void widget_status_bar_show(widget_status_bar_t *sb, const char *text, int64_t auto_hide_ms, int64_t now_us);
void widget_status_bar_hide(widget_status_bar_t *sb);
bool widget_status_bar_is_visible(const widget_status_bar_t *sb);
bool widget_status_bar_should_auto_hide(const widget_status_bar_t *sb, int64_t now_us);

/* ---- geometry ---- */
rawdraw_rect_t widget_status_bar_get_bounds(const widget_status_bar_t *sb, int fb_width, int fb_height);

/* ---- rendering ---- */
bool widget_status_bar_render(widget_status_bar_t *sb, uint8_t *fb, int fb_width, int fb_height);

#ifdef __cplusplus
}
#endif

#endif /* WIDGETS_STATUS_BAR_H_ */
