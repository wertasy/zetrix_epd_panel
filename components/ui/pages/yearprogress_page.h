/**
 * @file yearprogress_page.h
 * @brief Year progress page renderer — C port of C++ rawdraw::YearProgressRenderer.
 *
 * Displays the current date, year progress percentage, horizontal progress
 * bar, day counter, and a 12-month overview grid with UP/DOWN navigation.
 */
#ifndef COMPONENTS_UI_PAGES_YEARPROGRESS_PAGE_H_
#define COMPONENTS_UI_PAGES_YEARPROGRESS_PAGE_H_

#include "page_renderer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    page_renderer_t base;

    const lv_font_t *title_font;
    const lv_font_t *body_font;
    const lv_font_t *small_font;
    const lv_font_t *icon_font;

    /* Cached time data */
    int year;
    int month; /* 0-based */
    int day;
    int wday; /* 0=Sun ... 6=Sat */
    int day_of_year; /* 1-based */
    int total_days;
    int progress_pct;

    /* State: -1 = overview, 0-11 = selected month */
    int selected_month;
} yearprogress_page_t;

/* PageRenderer vtable entry points. */
void yearprogress_page_init(page_renderer_t *self, int width, int height);
void yearprogress_page_render(page_renderer_t *self, uint8_t *fb, int width, int height);
bool yearprogress_page_handle_input(page_renderer_t *self, const ui_button_event_t *event);

/* Data interface: refresh cached time data from the RTC. */
void yearprogress_page_update_time(page_renderer_t *self);

#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_UI_PAGES_YEARPROGRESS_PAGE_H_ */
