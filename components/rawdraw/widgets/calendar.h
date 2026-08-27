#ifndef COMPONENTS_RAWDRAW_WIDGETS_CALENDAR_H_
#define COMPONENTS_RAWDRAW_WIDGETS_CALENDAR_H_

#include <stdint.h>
#include <stdbool.h>
#include "../include/rawdraw.h"
#include "../include/font_engine.h"
#include "../include/framebuffer.h"

#define CALENDAR_COLS 7
#define CALENDAR_ROWS 6

typedef struct {
    int lunar_year; /* e.g. 2026 */
    int lunar_month; /* 1-12 (1=正月) */
    int lunar_day; /* 1-30 */
    bool is_leap_month; /* true if this is a leap month (闰月) */
} widget_calendar_lunar_date_t;

/** Opaque holiday-provider table (full definition below) — injected by the caller. */
typedef struct holiday_provider_s holiday_provider_t;
typedef struct {
    /* Bounds */
    int x, y, w, h;

    /* Displayed month/year */
    int year;
    int month; /* 1-based */

    /* Today's date (for highlight) */
    int today_year;
    int today_month;
    int today_day;

    /* Fonts */
    const lv_font_t *title_font;
    const lv_font_t *body_font;
    const lv_font_t *small_font;

    /* Computed layout */
    int cell_w;
    int cell_h;

    /* Flags */
    bool show_lunar;
    bool show_overflow;
    bool show_header;
    bool needs_full_refresh;

    /* Injected holiday data source (NULL = no holiday/makeup annotations). */
    const holiday_provider_t *hol_provider;

    /* Selection cursor state */
    bool selection_mode;
    int sel_row; /* 0-based grid row (0..5) */
    int sel_col; /* 0-based grid col (0..6, Sun..Sat) */
    int selected_day; /* confirmed selected day, 0 if none */
} widget_calendar_t;

/** Function-pointer table injected by caller — widget never includes holiday headers. */
struct holiday_provider_s {
    bool (*is_holiday)(int year, int month, int day);
    bool (*is_makeup_workday)(int year, int month, int day);
    const char *(*get_holiday_name)(int year, int month, int day);
    const char *(*get_makeup_label)(int year, int month, int day);
};

void widget_calendar_set_holiday_provider(widget_calendar_t *c, const holiday_provider_t *provider);

void widget_calendar_init(widget_calendar_t *c, int x, int y, int w, int h);
void widget_calendar_set_bounds(widget_calendar_t *c, int x, int y, int w, int h);
void widget_calendar_set_date(widget_calendar_t *c, int year, int month);
void widget_calendar_set_today(widget_calendar_t *c, int year, int month, int day);
void widget_calendar_set_show_lunar(widget_calendar_t *c, bool show);
void widget_calendar_set_show_overflow_days(widget_calendar_t *c, bool show);
void widget_calendar_set_show_header(widget_calendar_t *c, bool show);
void widget_calendar_set_fonts(widget_calendar_t *c, const lv_font_t *title_font, const lv_font_t *body_font,
                               const lv_font_t *small_font);

bool widget_calendar_prev_month(widget_calendar_t *c);
bool widget_calendar_next_month(widget_calendar_t *c);
bool widget_calendar_jump_to_today(widget_calendar_t *c);

void widget_calendar_enter_selection_mode(widget_calendar_t *c);
void widget_calendar_exit_selection_mode(widget_calendar_t *c);
bool widget_calendar_in_selection_mode(const widget_calendar_t *c);
void widget_calendar_navigate_selection(widget_calendar_t *c, int direction);
bool widget_calendar_confirm_selection(widget_calendar_t *c);
int widget_calendar_get_selected_day(const widget_calendar_t *c);
int widget_calendar_get_cursor_row(const widget_calendar_t *c);
int widget_calendar_get_cursor_col(const widget_calendar_t *c);

void widget_calendar_render(widget_calendar_t *c, uint8_t *fb, int fb_width, int fb_height);

/* Lunar calendar utility functions */
widget_calendar_lunar_date_t widget_calendar_to_lunar_date(int year, int month, int day);
void widget_calendar_get_lunar_year_name(int year, char *buf, int buf_size);
const char *widget_calendar_get_lunar_month_name(int month);
const char *widget_calendar_get_lunar_day_name(int day);
const char *widget_calendar_get_solar_term(int month, int day);

#endif /* COMPONENTS_RAWDRAW_WIDGETS_CALENDAR_H_ */
