/**
 * @file footer_bar.h
 * @brief Bottom footer / hint bar widget — C port of rawdraw::FooterBar.
 *
 * A shared bottom bar with left / center / right text slots, used across
 * pages for hint text and navigation cues. Supports an inverted color mode.
 *
 * All draw functions take (fb, fb_width, fb_height) — no global state.
 *
 * Porting notes (C++ -> C):
 *  - class members -> struct fields; text stored in fixed char buffers.
 *  - const lv_font_t* kept as a non-owning pointer (fonts are static const).
 */
#ifndef WIDGETS_FOOTER_BAR_H_
#define WIDGETS_FOOTER_BAR_H_

#include <stdint.h>
#include <stdbool.h>
#include "../include/rawdraw.h"
#include "../include/font_engine.h"
#include "../include/style.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Capacity of each text slot. */
#define WIDGET_FOOTER_BAR_TEXT_LEN 64

typedef struct {
    rawdraw_rect_t   bounds;
    const lv_font_t *font;

    char left_text[WIDGET_FOOTER_BAR_TEXT_LEN];
    char center_text[WIDGET_FOOTER_BAR_TEXT_LEN];
    char right_text[WIDGET_FOOTER_BAR_TEXT_LEN];

    bool inverted;
} widget_footer_bar_t;

/* ---- lifecycle ---- */
void widget_footer_bar_init(widget_footer_bar_t *fb, int screen_width, int screen_height);

/* ---- configuration ---- */
void widget_footer_bar_set_bounds(widget_footer_bar_t *fb, int screen_width, int screen_height);
void widget_footer_bar_set_text(widget_footer_bar_t *fb, const char *left, const char *center, const char *right);
void widget_footer_bar_set_font(widget_footer_bar_t *fb, const lv_font_t *font);
void widget_footer_bar_set_inverted(widget_footer_bar_t *fb, bool inverted);

/* ---- geometry ---- */
rawdraw_rect_t widget_footer_bar_get_bounds(const widget_footer_bar_t *fb);

/* ---- rendering ---- */
void widget_footer_bar_render(const widget_footer_bar_t *fb, uint8_t *framebuffer, int fb_width, int fb_height);

#ifdef __cplusplus
}
#endif

#endif /* WIDGETS_FOOTER_BAR_H_ */
