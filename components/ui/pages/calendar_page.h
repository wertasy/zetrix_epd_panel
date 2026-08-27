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

#include <time.h>

/* Injectable clock source for host tests (rtc-time-validity plan): the
 * page reads "today" through this hook so tests can simulate an epoch /
 * invalid clock. Defaults to the real clock. */
extern time_t (*calendar_page_time_source)(void);

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
    bool show_almanac;
    int alm_year;
    int alm_month;
    int alm_day;
    int alm_weekday;
    widget_calendar_lunar_date_t alm_lunar;
    char alm_lunar_year_name[16];
    const char *alm_solar_term;
    const char *alm_yi[4];
    const char *alm_ji[3];

    /* Data staleness indicator: unix timestamp of the last successful data
     * refresh (holiday/SNTP landing). 0 = never (label hidden). Persisted
     * via nvs_state "cal_fresh" so it survives deep sleep. */
    int32_t data_refresh_epoch;

    /* [time-validity] True after a calendar service was skipped because
     * the device had no valid time (latched via NVS "cal_time_invalid").
     * The next successful render shows 「时间未同步」 in the footer and
     * clears it — the e-paper keeps its old image during the outage, so
     * this is the only user-visible explanation of the gap. */
    bool time_invalid_latched;
} calendar_page_t;

/* PageRenderer vtable entry points. */
void calendar_page_init(page_renderer_t *self, int width, int height);
void calendar_page_render(page_renderer_t *self, uint8_t *fb, int width, int height);
bool calendar_page_handle_input(page_renderer_t *self, const ui_button_event_t *event);

/* Data interface. */
void calendar_page_set_data_refresh_time(page_renderer_t *self, int32_t epoch);
calendar_page_selected_date_t calendar_page_get_selected_date(const page_renderer_t *self);
void calendar_page_note_time_invalid(void);
int calendar_page_get_year(const page_renderer_t *self);
int calendar_page_get_month(const page_renderer_t *self);

/**
 * Returns "2026年4月15日 丙午年二月初一" style text when a date has been
 * selected, or an empty string otherwise.
 */
void calendar_page_get_voice_query_context(const page_renderer_t *self, char *out, int out_size);

#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_UI_PAGES_CALENDAR_PAGE_H_ */
