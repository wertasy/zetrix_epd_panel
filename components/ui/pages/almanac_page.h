/**
 * @file almanac_page.h
 * @brief Almanac page renderer — C port of C++ rawdraw::AlmanacRenderer.
 *
 * Displays the Gregorian date, lunar date, solar term, and simplified
 * traditional almanac info (宜忌) for the current or navigated day.
 * Uses the widget_calendar_* lunar conversion helpers (2000-2050).
 */
#ifndef COMPONENTS_UI_PAGES_ALMANAC_PAGE_H_
#define COMPONENTS_UI_PAGES_ALMANAC_PAGE_H_

#include "page_renderer.h"
#include "../../rawdraw/widgets/calendar.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ALMANAC_PAGE_TITLE_BAR_H 28

typedef struct {
    page_renderer_t base;

    const lv_font_t *font;
    const lv_font_t *title_font;
    const lv_font_t *icon_font;

    /* Cached Gregorian date (month is 1-based). */
    int year;
    int month;
    int day;
    int weekday; /* 0 = Sunday ... 6 = Saturday */

    /* Lunar data computed via widget_calendar_* helpers. */
    widget_calendar_lunar_date_t lunar;
    char                         lunar_year_name[16];
    const char                  *solar_term; /* NULL when the day is not a solar term day */
    const char                  *yi[4];
    const char                  *ji[3];
} almanac_page_t;

/* PageRenderer vtable entry points. */
void almanac_page_init(page_renderer_t *self, int width, int height);
void almanac_page_render(page_renderer_t *self, uint8_t *fb, int width, int height);
bool almanac_page_handle_input(page_renderer_t *self, const ui_button_event_t *event);

/* Data interface: re-read the current time and recompute lunar data. */
void almanac_page_refresh_data(page_renderer_t *self);

#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_UI_PAGES_ALMANAC_PAGE_H_ */
