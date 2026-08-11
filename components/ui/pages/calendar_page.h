/**
 * @file calendar_page.h
 * @brief Calendar page renderer — C port of C++ rawdraw::CalendarRenderer.
 *
 * Displays a 7x6 monthly calendar grid with lunar dates and a selection
 * cursor for picking a specific date.
 *
 * Navigation: UP/DOWN=翻月, BOOT=进入选择模式/确认日期
 * In selection mode: UP/DOWN=移动光标, BOOT=确认选择
 */
#ifndef COMPONENTS_UI_PAGES_CALENDAR_PAGE_H_
#define COMPONENTS_UI_PAGES_CALENDAR_PAGE_H_

#include "page_renderer.h"
#include "../../rawdraw/widgets/calendar.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int year;
    int month;
    int day;
} calendar_page_selected_date_t;

typedef struct {
    page_renderer_t base;

    /* Calendar widget (owns grid + selection cursor state). */
    widget_calendar_t cal;

    const lv_font_t *title_font;
    const lv_font_t *body_font;
    const lv_font_t *small_font;

    /* Displayed month/year (mirrored from cal). */
    int year;
    int month;

    /* Today's date (captured at init). */
    int today_day;
    int today_month;
    int today_year;

    /* Last confirmed selection; {0,0,0} if none. */
    calendar_page_selected_date_t selected_date;

    /* Almanac sub-view state. */
    bool                              show_almanac;
    int                               alm_year;
    int                               alm_month;
    int                               alm_day;
    int                               alm_weekday;
    widget_calendar_lunar_date_t      alm_lunar;
    char                              alm_lunar_year_name[16];
    const char                       *alm_solar_term;
    const char                       *alm_yi[4];
    const char                       *alm_ji[3];
} calendar_page_t;

/* PageRenderer vtable entry points. */
void calendar_page_init(page_renderer_t *self, int width, int height);
void calendar_page_render(page_renderer_t *self, uint8_t *fb, int width, int height);
bool calendar_page_handle_input(page_renderer_t *self, const ui_button_event_t *event);

/* Data interface. */
calendar_page_selected_date_t calendar_page_get_selected_date(const page_renderer_t *self);
int                           calendar_page_get_year(const page_renderer_t *self);
int                           calendar_page_get_month(const page_renderer_t *self);

/**
 * @brief Write the formatted voice query context string into @p out.
 * Returns "2026年4月15日 丙午年二月初一" style text when a date has been
 * selected, or an empty string otherwise.
 */
void calendar_page_get_voice_query_context(const page_renderer_t *self, char *out, int out_size);

#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_UI_PAGES_CALENDAR_PAGE_H_ */
