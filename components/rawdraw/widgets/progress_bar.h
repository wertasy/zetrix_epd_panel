/**
 * @file progress_bar.h
 * @brief Progress bar and circular gauge widgets — C port of rawdraw::ProgressBar
 *        and rawdraw::CircularGauge, plus standalone circular-progress primitives.
 *
 * ProgressBar: a horizontal bar showing progress from 0–100 % with rounded
 * corners and an optional centered label.
 *
 * CircularGauge: a ring-style progress indicator with an optional center label.
 *
 * Also exposes rawdraw_draw_circular_progress() and
 * rawdraw_draw_circular_progress_with_label() standalone drawing functions.
 *
 * All draw functions take (fb, fb_width, fb_height) — no global state.
 *
 * Porting notes (C++ -> C):
 *  - class members -> struct fields; label stored in a fixed char buffer.
 *  - const lv_font_t* kept as a non-owning pointer (fonts are static const).
 *  - std::clamp / std::max / std::min -> inline clamping.
 */
#ifndef WIDGETS_PROGRESS_BAR_H_
#define WIDGETS_PROGRESS_BAR_H_

#include <stdint.h>
#include <stdbool.h>
#include "../include/rawdraw.h"
#include "../include/font_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Capacity of the inline label buffer. */
#define WIDGET_PROGRESS_BAR_LABEL_LEN 32

/* ============================================================
 * Standalone circular progress primitives
 * ============================================================ */

/**
 * @brief Draw a ring-style circular progress arc.
 *
 * Renders a full background ring, then overlays a foreground arc from the
 * 12-o'clock position clockwise for value_pct percent of the circle.
 *
 * @param fb          Framebuffer pointer.
 * @param width       Framebuffer width.
 * @param height      Framebuffer height.
 * @param center      Circle center.
 * @param radius      Outer radius.
 * @param thickness   Ring thickness.
 * @param value_pct   Progress value (0–100).
 * @param bg_color    Background ring color.
 * @param fg_color    Foreground (filled) arc color.
 */
void rawdraw_draw_circular_progress(uint8_t *fb, int width, int height, rawdraw_point_t center, int radius,
                                    int thickness, int value_pct, rawdraw_color_t bg_color, rawdraw_color_t fg_color);

/**
 * @brief Draw a circular progress arc with a centered text label.
 */
void rawdraw_draw_circular_progress_with_label(uint8_t *fb, int width, int height, rawdraw_point_t center, int radius,
                                               int thickness, int value_pct, const char *label, const lv_font_t *font);

/* ============================================================
 * Horizontal ProgressBar widget
 * ============================================================ */

typedef struct {
    rawdraw_rect_t bounds;
    int            value; /* 0–100 */
    int            radius; /* corner radius (STYLE_PROGRESS_RADIUS = pill) */

    char             label[WIDGET_PROGRESS_BAR_LABEL_LEN]; /* "" = no label */
    const lv_font_t *label_font;

    rawdraw_color_t bg_color; /* empty track color */
    rawdraw_color_t fg_color; /* filled portion color */
    bool            custom_colors;
} widget_progress_bar_t;

/* ---- lifecycle ---- */
void widget_progress_bar_init(widget_progress_bar_t *bar, int x, int y, int w, int h);

/* ---- configuration ---- */
void widget_progress_bar_set_bounds(widget_progress_bar_t *bar, int x, int y, int w, int h);
void widget_progress_bar_set_value(widget_progress_bar_t *bar, int value);
int  widget_progress_bar_get_value(const widget_progress_bar_t *bar);
void widget_progress_bar_set_label(widget_progress_bar_t *bar, const char *label);
void widget_progress_bar_set_label_font(widget_progress_bar_t *bar, const lv_font_t *font);
void widget_progress_bar_set_radius(widget_progress_bar_t *bar, int radius);
void widget_progress_bar_set_bg_color(widget_progress_bar_t *bar, rawdraw_color_t color);
void widget_progress_bar_set_fg_color(widget_progress_bar_t *bar, rawdraw_color_t color);

/* ---- geometry / rendering ---- */
rawdraw_rect_t widget_progress_bar_get_bounds(const widget_progress_bar_t *bar);
void           widget_progress_bar_render(const widget_progress_bar_t *bar, uint8_t *fb, int fb_width, int fb_height);

/* ============================================================
 * CircularGauge widget
 * ============================================================ */

typedef struct {
    int cx, cy; /* center position */
    int radius; /* outer radius */
    int thickness; /* ring thickness */
    int value; /* 0–100 */

    char             label[WIDGET_PROGRESS_BAR_LABEL_LEN]; /* "" = no label */
    const lv_font_t *label_font;

    rawdraw_color_t bg_color; /* background ring color */
    rawdraw_color_t fg_color; /* foreground arc color */
    bool            custom_colors;
} widget_circular_gauge_t;

/* ---- lifecycle ---- */
void widget_circular_gauge_init(widget_circular_gauge_t *gauge, int cx, int cy, int radius, int thickness);

/* ---- configuration ---- */
void widget_circular_gauge_set_center(widget_circular_gauge_t *gauge, int cx, int cy);
void widget_circular_gauge_set_radius(widget_circular_gauge_t *gauge, int radius);
void widget_circular_gauge_set_thickness(widget_circular_gauge_t *gauge, int thickness);
void widget_circular_gauge_set_value(widget_circular_gauge_t *gauge, int value);
int  widget_circular_gauge_get_value(const widget_circular_gauge_t *gauge);
void widget_circular_gauge_set_label(widget_circular_gauge_t *gauge, const char *label);
void widget_circular_gauge_set_label_font(widget_circular_gauge_t *gauge, const lv_font_t *font);
void widget_circular_gauge_set_bg_color(widget_circular_gauge_t *gauge, rawdraw_color_t color);
void widget_circular_gauge_set_fg_color(widget_circular_gauge_t *gauge, rawdraw_color_t color);

/* ---- geometry / rendering ---- */
rawdraw_rect_t widget_circular_gauge_get_bounds(const widget_circular_gauge_t *gauge);
void widget_circular_gauge_render(const widget_circular_gauge_t *gauge, uint8_t *fb, int fb_width, int fb_height);

#ifdef __cplusplus
}
#endif

#endif /* WIDGETS_PROGRESS_BAR_H_ */
