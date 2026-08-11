/**
 * @file lifebar_page.h
 * @brief Life progress page renderer — C port of C++ rawdraw::LifeBarRenderer.
 *
 * Shows a large circular gauge with age, days elapsed/remaining,
 * remaining weekends count, and a motivational quote.
 * Assumes birthdate 1990-01-01, 80-year lifespan.
 */
#ifndef COMPONENTS_UI_PAGES_LIFEBAR_PAGE_H_
#define COMPONENTS_UI_PAGES_LIFEBAR_PAGE_H_

#include "page_renderer.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    page_renderer_t base;

    /* Cached stats (recomputed on init/render) */
    int age_years;
    int age_months;
    int days_elapsed;
    int days_remaining;
    int weekends_remaining;
    int life_pct;

    /* Visibility (controlled via settings) */
    bool visible;

    /* Fonts */
    const lv_font_t *title_font;
    const lv_font_t *body_font;
    const lv_font_t *small_font;
} lifebar_page_t;

/* PageRenderer vtable entry points. */
void lifebar_page_init(page_renderer_t *self, int width, int height);
void lifebar_page_render(page_renderer_t *self, uint8_t *fb, int width, int height);
bool lifebar_page_handle_input(page_renderer_t *self, const ui_button_event_t *event);

/* Data interface. */
void lifebar_page_set_visible(page_renderer_t *self, bool visible);
bool lifebar_page_is_visible(const page_renderer_t *self);

#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_UI_PAGES_LIFEBAR_PAGE_H_ */
