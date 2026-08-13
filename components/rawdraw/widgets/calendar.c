#include "../include/rawdraw_util.h"
#include "calendar.h"
#include "../include/rawdraw_ext.h"
#include "../include/theme.h"
#include "../include/style.h"
#include "../include/layout.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/* ============================================================
 * Solar term lookup table (approximate fixed dates, ±1 day)
 * ============================================================ */

typedef struct {
    int month;
    int day;
    const char *name;
} solar_term_entry_t;

static const solar_term_entry_t kSolarTerms[] = {
    {1, 5, "小寒"},  {1, 20, "大寒"},  {2, 4, "立春"},  {2, 19, "雨水"},  {3, 5, "惊蛰"},  {3, 20, "春分"},
    {4, 4, "清明"},  {4, 20, "谷雨"},  {5, 5, "立夏"},  {5, 21, "小满"},  {6, 5, "芒种"},  {6, 21, "夏至"},
    {7, 7, "小暑"},  {7, 23, "大暑"},  {8, 7, "立秋"},  {8, 23, "处暑"},  {9, 7, "白露"},  {9, 23, "秋分"},
    {10, 8, "寒露"}, {10, 23, "霜降"}, {11, 7, "立冬"}, {11, 22, "小雪"}, {12, 7, "大雪"}, {12, 22, "冬至"},
};

static const int kSolarTermCount = sizeof(kSolarTerms) / sizeof(kSolarTerms[0]);

/* Lunar month names (lunar calendar months 1-12) */
static const char *kLunarMonths[] = {"正月", "二月", "三月", "四月", "五月",   "六月",
                                     "七月", "八月", "九月", "十月", "十一月", "腊月"};

/* Lunar day names */
static const char *kLunarDays[] = {"初一", "初二", "初三", "初四", "初五", "初六", "初七", "初八", "初九", "初十",
                                   "十一", "十二", "十三", "十四", "十五", "十六", "十七", "十八", "十九", "二十",
                                   "廿一", "廿二", "廿三", "廿四", "廿五", "廿六", "廿七", "廿八", "廿九", "三十"};

/* Weekday header characters */
static const char *kWeekdayChars[] = {"日", "一", "二", "三", "四", "五", "六"};
static const char *kWeekdayFull[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};

/* Holidays (fixed-date solar holidays in Gregorian calendar) */
typedef struct {
    int month;
    int day;
    const char *name;
} calendar_holiday_entry_t;

static const calendar_holiday_entry_t kHolidays[] = {
    {1, 1, "元旦"},    {2, 14, "情人节"}, {3, 8, "妇女节"},   {3, 12, "植树节"},  {4, 1, "愚人节"},
    {5, 1, "劳动节"},  {5, 4, "青年节"},  {6, 1, "儿童节"},   {7, 1, "建党节"},   {8, 1, "建军节"},
    {9, 10, "教师节"}, {10, 1, "国庆节"}, {10, 31, "万圣节"}, {12, 25, "圣诞节"},
};

static const int kHolidayCount = sizeof(kHolidays) / sizeof(kHolidays[0]);

/* ============================================================
 * Lunar calendar algorithm (2000-2050)
 * ============================================================ */

static const uint16_t kSpringInfo[] = {
    0x0205, 0x0118, 0x020C, 0x0201, 0x0116, /* 2000-2004 */
    0x0209, 0x011D, 0x0212, 0x0207, 0x011A, /* 2005-2009 */
    0x020E, 0x0203, 0x0117, 0x020A, 0x011F, /* 2010-2014 */
    0x0213, 0x0208, 0x011C, 0x0210, 0x0205, /* 2015-2019 */
    0x0119, 0x020C, 0x0201, 0x0116, 0x020A, /* 2020-2024 */
    0x011D, 0x0211, 0x0206, 0x011A, 0x020D, /* 2025-2029 */
    0x0203, 0x0117, 0x020B, 0x011F, 0x0213, /* 2030-2034 */
    0x0208, 0x011C, 0x020F, 0x0204, 0x0118, /* 2035-2039 */
    0x020C, 0x0201, 0x0116, 0x020A, 0x011E, /* 2040-2044 */
    0x0211, 0x0206, 0x011A, 0x020E, 0x0202, /* 2045-2049 */
    0x0117, /* 2050 */
};

static const int kLunarMinYear = 2000;
static const int kLunarMaxYear = 2050;

/* Standard lunarInfo table (bits 0-3 = leap month, bits 4-15 = month
 * lengths 1=30d/0=29d, bit 16 = leap month 30d).
 * Used for exact per-year month lengths instead of the old alternating
 * 30/29 approximation which caused off-by-one errors. */
static const uint32_t kLunarInfo[] = {
    0x0c960, 0x0d954, 0x0d4a0, 0x0da50, 0x07552, /* 2000-2004 */
    0x056a0, 0x0abb7, 0x025d0, 0x092d0, 0x0cab5, /* 2005-2009 */
    0x0a950, 0x0b4a0, 0x0baa4, 0x0ad50, 0x055d9, /* 2010-2014 */
    0x04ba0, 0x0a5b0, 0x15176, 0x052b0, 0x0a930, /* 2015-2019 */
    0x07954, 0x06aa0, 0x0ad50, 0x05b52, 0x04b60, /* 2020-2024 */
    0x0a6e6, 0x0a4e0, 0x0d260, 0x0ea65, 0x0d530, /* 2025-2029 */
    0x05aa0, 0x076a3, 0x096d0, 0x04afb, 0x04ad0, /* 2030-2034 */
    0x0a4d0, 0x1d0b6, 0x0d250, 0x0d520, 0x0dd45, /* 2035-2039 */
    0x0b5a0, 0x056d0, 0x055b2, 0x049b0, 0x0a577, /* 2040-2044 */
    0x0a4b0, 0x0aa50, 0x1b255, 0x06d20, 0x0ada0, /* 2045-2049 */
    0x05520, /* 2050 */
};

static int lunar_month_days(int year, int month)
{
    /* month 1-12 → bit 15..4 in kLunarInfo */
    return ((kLunarInfo[year - kLunarMinYear] >> (16 - month)) & 1) ? 30 : 29;
}

static int lunar_leap_month(int year)
{
    return kLunarInfo[year - kLunarMinYear] & 0xf;
}

static int lunar_leap_days(int year)
{
    return ((kLunarInfo[year - kLunarMinYear] >> 16) & 1) ? 30 : 29;
}

/* Tian Gan (天干) and Di Zhi (地支) for year names */
static const char *kTianGan[] = {"甲", "乙", "丙", "丁", "戊", "己", "庚", "辛", "壬", "癸"};
static const char *kDiZhi[] = {"子", "丑", "寅", "卯", "辰", "巳", "午", "未", "申", "酉", "戌", "亥"};

static bool is_leap_year(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static int days_in_month(int year, int month)
{
    static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12)
        return 0;
    if (month == 2 && is_leap_year(year))
        return 29;
    return d[month - 1];
}

static int weekday_of_date(int year, int month, int day)
{
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    int y = year;
    if (month < 3)
        y--;
    return (y + y / 4 - y / 100 + y / 400 + t[month - 1] + day) % 7;
}

static int day_of_year(int year, int month, int day)
{
    int doy = 0;
    for (int m = 1; m < month; m++) {
        doy += days_in_month(year, m);
    }
    return doy + day;
}

static int days_in_year(int year)
{
    return is_leap_year(year) ? 366 : 365;
}

/* ============================================================
 * Public Lunar Utilities
 * ============================================================ */

widget_calendar_lunar_date_t widget_calendar_to_lunar_date(int year, int month, int day)
{
    widget_calendar_lunar_date_t fail = {0, 0, 0, false};
    if (year < kLunarMinYear || year > kLunarMaxYear)
        return fail;

    int idx = year - kLunarMinYear;
    uint16_t cny = kSpringInfo[idx];
    int cny_m = (cny >> 8) & 0xff;
    int cny_d = cny & 0xff;

    int doy = day_of_year(year, month, day);
    int cny_doy = day_of_year(year, cny_m, cny_d);

    int lunar_year = year;
    int days_since_cny;

    if (doy < cny_doy) {
        if (year <= kLunarMinYear)
            return fail;
        lunar_year = year - 1;
        uint16_t prev_cny = kSpringInfo[idx - 1];
        int prev_cny_m = (prev_cny >> 8) & 0xff;
        int prev_cny_d = prev_cny & 0xff;
        int prev_cny_doy = day_of_year(year - 1, prev_cny_m, prev_cny_d);
        int prev_year_days = days_in_year(year - 1);
        days_since_cny = (prev_year_days - prev_cny_doy) + doy;
    } else {
        days_since_cny = doy - cny_doy;
    }

    int lunar_month = 1;
    int leap_month = lunar_leap_month(lunar_year);

    while (lunar_month <= 12) {
        int dim = lunar_month_days(lunar_year, lunar_month);
        if (days_since_cny < dim) {
            widget_calendar_lunar_date_t res = {lunar_year, lunar_month, days_since_cny + 1, false};
            return res;
        }
        days_since_cny -= dim;

        if (leap_month == lunar_month) {
            int leap_days = lunar_leap_days(lunar_year);
            if (days_since_cny < leap_days) {
                widget_calendar_lunar_date_t res = {lunar_year, lunar_month, days_since_cny + 1, true};
                return res;
            }
            days_since_cny -= leap_days;
        }
        lunar_month++;
    }

    widget_calendar_lunar_date_t res = {lunar_year, 12, 1, false};
    return res;
}

void widget_calendar_get_lunar_year_name(int year, char *buf, int buf_size)
{
    if (!buf || buf_size <= 0)
        return;
    if (year < 1900 || year > 2100) {
        buf[0] = '\0';
        return;
    }
    int tg = (year - 4) % 10;
    int dz = (year - 4) % 12;
    snprintf(buf, buf_size, "%s%s", kTianGan[tg], kDiZhi[dz]);
}

const char *widget_calendar_get_lunar_month_name(int month)
{
    if (month < 1 || month > 12)
        return "";
    return kLunarMonths[month - 1];
}

const char *widget_calendar_get_lunar_day_name(int day)
{
    if (day < 1 || day > 30)
        return "";
    return kLunarDays[day - 1];
}

const char *widget_calendar_get_solar_term(int month, int day)
{
    for (int i = 0; i < kSolarTermCount; i++) {
        if (kSolarTerms[i].month == month && kSolarTerms[i].day == day) {
            return kSolarTerms[i].name;
        }
    }
    return NULL;
}

/* ============================================================
 * Widget Lifecycle and Configuration
 * ============================================================ */

void widget_calendar_init(widget_calendar_t *c, int x, int y, int w, int h)
{
    if (!c)
        return;
    memset(c, 0, sizeof(*c));
    c->x = x;
    c->y = y;
    c->w = w;
    c->h = h;
    c->year = 2026;
    c->month = 1;
    c->title_font = NULL;
    c->body_font = NULL;
    c->small_font = NULL;
    c->show_lunar = false;
    c->show_overflow = false;
    c->show_header = true;
    c->needs_full_refresh = true;
    c->hol_provider = NULL;
    c->selection_mode = false;
    c->sel_row = -1;
    c->sel_col = -1;
    c->selected_day = 0;

    time_t now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    c->today_year = tm_buf.tm_year + 1900;
    c->today_month = tm_buf.tm_mon + 1;
    c->today_day = tm_buf.tm_mday;
    c->year = c->today_year;
    c->month = c->today_month;
}

void widget_calendar_set_bounds(widget_calendar_t *c, int x, int y, int w, int h)
{
    if (!c)
        return;
    c->x = x;
    c->y = y;
    c->w = w;
    c->h = h;
    c->needs_full_refresh = true;
}

void widget_calendar_set_date(widget_calendar_t *c, int year, int month)
{
    if (!c)
        return;
    if (month < 1) {
        month = 12;
        year--;
    } else if (month > 12) {
        month = 1;
        year++;
    }
    c->year = year;
    c->month = month;
    c->needs_full_refresh = true;
}

void widget_calendar_set_show_lunar(widget_calendar_t *c, bool show)
{
    if (c)
        c->show_lunar = show;
}

void widget_calendar_set_show_overflow_days(widget_calendar_t *c, bool show)
{
    if (c)
        c->show_overflow = show;
}

void widget_calendar_set_show_header(widget_calendar_t *c, bool show)
{
    if (!c)
        return;
    c->show_header = show;
    c->needs_full_refresh = true;
}

void widget_calendar_set_fonts(widget_calendar_t *c, const lv_font_t *title_font, const lv_font_t *body_font,
                               const lv_font_t *small_font)
{
    if (!c)
        return;
    c->title_font = title_font;
    c->body_font = body_font;
    c->small_font = small_font;
    c->needs_full_refresh = true;
}

void widget_calendar_set_holiday_provider(widget_calendar_t *c, const holiday_provider_t *provider)
{
    if (!c)
        return;
    c->hol_provider = provider;
}

/* ============================================================
 * Navigation
 * ============================================================ */

bool widget_calendar_prev_month(widget_calendar_t *c)
{
    if (!c)
        return false;
    bool exit_sel = false;
    if (c->selection_mode) {
        widget_calendar_exit_selection_mode(c);
        exit_sel = true;
    }
    int old_year = c->year, old_month = c->month;
    c->month--;
    if (c->month < 1) {
        c->month = 12;
        c->year--;
    }
    if (c->year != old_year || c->month != old_month || exit_sel) {
        c->needs_full_refresh = true;
    }
    return c->needs_full_refresh;
}

bool widget_calendar_next_month(widget_calendar_t *c)
{
    if (!c)
        return false;
    bool exit_sel = false;
    if (c->selection_mode) {
        widget_calendar_exit_selection_mode(c);
        exit_sel = true;
    }
    int old_year = c->year, old_month = c->month;
    c->month++;
    if (c->month > 12) {
        c->month = 1;
        c->year++;
    }
    if (c->year != old_year || c->month != old_month || exit_sel) {
        c->needs_full_refresh = true;
    }
    return c->needs_full_refresh;
}

bool widget_calendar_jump_to_today(widget_calendar_t *c)
{
    if (!c)
        return false;
    bool exit_sel = false;
    if (c->selection_mode) {
        widget_calendar_exit_selection_mode(c);
        exit_sel = true;
    }
    int old_year = c->year, old_month = c->month;
    c->year = c->today_year;
    c->month = c->today_month;
    if (c->year != old_year || c->month != old_month || exit_sel) {
        c->needs_full_refresh = true;
    }
    return c->needs_full_refresh;
}

/* ============================================================
 * Selection Cursor
 * ============================================================ */

static int get_first_day_of_month(const widget_calendar_t *c)
{
    return weekday_of_date(c->year, c->month, 1);
}

void widget_calendar_enter_selection_mode(widget_calendar_t *c)
{
    if (!c)
        return;
    c->selection_mode = true;
    c->selected_day = 0;
    c->needs_full_refresh = true;

    int first_dow = get_first_day_of_month(c);
    if (c->year == c->today_year && c->month == c->today_month) {
        int today_cell = first_dow + c->today_day - 1;
        c->sel_row = today_cell / CALENDAR_COLS;
        c->sel_col = today_cell % CALENDAR_COLS;
    } else {
        c->sel_row = first_dow / CALENDAR_COLS;
        c->sel_col = first_dow % CALENDAR_COLS;
    }
}

void widget_calendar_exit_selection_mode(widget_calendar_t *c)
{
    if (!c)
        return;
    c->selection_mode = false;
    c->sel_row = -1;
    c->sel_col = -1;
    c->needs_full_refresh = true;
}

bool widget_calendar_in_selection_mode(const widget_calendar_t *c)
{
    return c ? c->selection_mode : false;
}

void widget_calendar_navigate_selection(widget_calendar_t *c, int direction)
{
    if (!c || !c->selection_mode || direction == 0)
        return;

    int first_dow = get_first_day_of_month(c);
    int dim = days_in_month(c->year, c->month);
    int prev_dim = days_in_month(c->month == 1 ? c->year - 1 : c->year, c->month == 1 ? 12 : c->month - 1);

    int new_row = c->sel_row + direction;
    if (new_row < 0)
        new_row = 0;
    if (new_row >= CALENDAR_ROWS)
        new_row = CALENDAR_ROWS - 1;

    int cell = new_row * CALENDAR_COLS + c->sel_col;
    int day = 0;
    bool valid = false;

    if (cell < first_dow) {
        day = prev_dim - first_dow + cell + 1;
        if (c->show_overflow)
            valid = true;
    } else if (cell >= first_dow + dim) {
        day = cell - first_dow - dim + 1;
        if (c->show_overflow)
            valid = true;
    } else {
        day = cell - first_dow + 1;
        valid = true;
    }

    if (valid && day > 0) {
        if (c->sel_row != new_row) {
            c->sel_row = new_row;
            c->needs_full_refresh = true;
        }
    } else {
        for (int try_row = new_row; try_row >= 0 && try_row < CALENDAR_ROWS; try_row += direction) {
            int try_cell = try_row * CALENDAR_COLS + c->sel_col;
            int try_day = 0;
            bool try_valid = false;
            if (try_cell < first_dow) {
                try_day = prev_dim - first_dow + try_cell + 1;
                if (c->show_overflow)
                    try_valid = true;
            } else if (try_cell >= first_dow + dim) {
                try_day = try_cell - first_dow - dim + 1;
                if (c->show_overflow)
                    try_valid = true;
            } else {
                try_day = try_cell - first_dow + 1;
                try_valid = true;
            }
            if (try_valid && try_day > 0) {
                if (c->sel_row != try_row) {
                    c->sel_row = try_row;
                    c->needs_full_refresh = true;
                }
                return;
            }
        }
    }
}

bool widget_calendar_confirm_selection(widget_calendar_t *c)
{
    if (!c || !c->selection_mode)
        return false;

    int first_dow = get_first_day_of_month(c);
    int dim = days_in_month(c->year, c->month);
    int cell = c->sel_row * CALENDAR_COLS + c->sel_col;
    int day = 0;

    if (cell < first_dow) {
        int prev_dim = days_in_month(c->month == 1 ? c->year - 1 : c->year, c->month == 1 ? 12 : c->month - 1);
        day = prev_dim - first_dow + cell + 1;
        c->month--;
        if (c->month < 1) {
            c->month = 12;
            c->year--;
        }
    } else if (cell >= first_dow + dim) {
        day = cell - first_dow - dim + 1;
        c->month++;
        if (c->month > 12) {
            c->month = 1;
            c->year++;
        }
    } else {
        day = cell - first_dow + 1;
    }

    if (day > 0 && day <= 31) {
        c->selected_day = day;
        widget_calendar_exit_selection_mode(c);
        return true;
    }
    return false;
}

int widget_calendar_get_selected_day(const widget_calendar_t *c)
{
    return c ? c->selected_day : 0;
}

int widget_calendar_get_cursor_row(const widget_calendar_t *c)
{
    return c ? c->sel_row : -1;
}

int widget_calendar_get_cursor_col(const widget_calendar_t *c)
{
    return c ? c->sel_col : -1;
}

/* ============================================================
 * Rendering Sub-methods
 * ============================================================ */

static void draw_header(const widget_calendar_t *c, uint8_t *fb, int width, int height)
{
    int title_bar_h = STYLE_PANEL_TITLE_HEIGHT;
    rawdraw_paint_style_t bg = rawdraw_theme_component(ROLE_PANEL);
    rawdraw_color_t text_color = rawdraw_theme_color_for(THEME_TOKEN_TEXT_PRIMARY);
    rawdraw_color_t border_color = rawdraw_theme_color_for(THEME_TOKEN_BORDER);

    rawdraw_rect_t rect = {c->x, c->y, c->w, title_bar_h};
    rawdraw_draw_styled_rect(fb, width, height, rect, &bg);

    int line_y = c->y + title_bar_h - 2;
    rawdraw_draw_hline(fb, width, height, line_y, c->x, c->x + c->w - 1, border_color);
    rawdraw_draw_hline(fb, width, height, line_y + 1, c->x, c->x + c->w - 1, border_color);

    char title[32];
    snprintf(title, sizeof(title), "%d年 %d月", c->year, c->month);
    int title_w = rawdraw_measure_text_width(title, c->title_font);
    int title_x = c->x + (c->w - title_w) / 2;
    int title_text_y = c->y + (title_bar_h - (int)c->title_font->line_height) / 2;
    title_x = (title_x + 7) & ~7;
    rawdraw_draw_text(fb, width, height, title_x, title_text_y, title, c->title_font, text_color);

    const char *left_arrow = "<";
    int left_x = c->x + STYLE_SPACING_LG;
    left_x = (left_x + 7) & ~7;
    rawdraw_draw_text(fb, width, height, left_x, title_text_y, left_arrow, c->title_font, text_color);

    const char *right_arrow = ">";
    int right_w = rawdraw_measure_text_width(right_arrow, c->title_font);
    int right_x = c->x + c->w - STYLE_SPACING_LG - right_w;
    right_x = (right_x + 7) & ~7;
    rawdraw_draw_text(fb, width, height, right_x, title_text_y, right_arrow, c->title_font, text_color);
}

static void draw_weekday_row(const widget_calendar_t *c, uint8_t *fb, int width, int height, int y)
{
    rawdraw_paint_style_t bg = rawdraw_theme_style(THEME_TOKEN_BACKGROUND_SECONDARY);
    rawdraw_color_t text_color = rawdraw_theme_color_for(THEME_TOKEN_TEXT_SECONDARY);
    rawdraw_color_t border_color = rawdraw_theme_color_for(THEME_TOKEN_BORDER);

    int bg_h = 22;
    rawdraw_rect_t rect = {c->x, y, c->w, bg_h};
    rawdraw_draw_styled_rect(fb, width, height, rect, &bg);

    for (int i = 0; i < CALENDAR_COLS; i++) {
        const char *ch = kWeekdayChars[i];
        int ch_w = rawdraw_measure_text_width(ch, c->body_font);
        int cx = c->x + i * c->cell_w;
        int text_x = cx + (c->cell_w - ch_w) / 2;

        rawdraw_draw_text(fb, width, height, text_x,
                          rawdraw_layout_ink_centered_text_top_y_in_box(c->body_font, ch, y, bg_h, 0), ch, c->body_font,
                          text_color);
    }

    int line_y = y + bg_h - 2;
    rawdraw_draw_hline(fb, width, height, line_y, c->x, c->x + c->w - 1, border_color);
    rawdraw_draw_hline(fb, width, height, line_y + 1, c->x, c->x + c->w - 1, border_color);
}

static void draw_grid(const widget_calendar_t *c, uint8_t *fb, int width, int height, int y)
{
    rawdraw_paint_style_t today_style = rawdraw_theme_style(THEME_TOKEN_SELECTED);
    rawdraw_color_t text_color = rawdraw_theme_color_for(THEME_TOKEN_TEXT_PRIMARY);
    rawdraw_color_t dim_text = rawdraw_theme_color_for(THEME_TOKEN_DISABLED);
    rawdraw_color_t accent = rawdraw_theme_style(THEME_TOKEN_ACCENT).bg;

    int first_dow = get_first_day_of_month(c);
    int dim = days_in_month(c->year, c->month);
    int prev_dim = days_in_month(c->month == 1 ? c->year - 1 : c->year, c->month == 1 ? 12 : c->month - 1);

    int total_cells = CALENDAR_ROWS * CALENDAR_COLS;
    int start_offset = first_dow;

    for (int cell = 0; cell < total_cells; cell++) {
        int row = cell / CALENDAR_COLS;
        int col = cell % CALENDAR_COLS;
        int cx = c->x + col * c->cell_w;
        int cy = y + row * c->cell_h;

        int display_day = 0;
        bool is_today = false;
        bool is_current_month = true;

        if (cell < start_offset) {
            display_day = prev_dim - start_offset + cell + 1;
            is_current_month = false;
        } else if (cell >= start_offset + dim) {
            display_day = cell - start_offset - dim + 1;
            is_current_month = false;
        } else {
            display_day = cell - start_offset + 1;
            if (c->year == c->today_year && c->month == c->today_month && display_day == c->today_day) {
                is_today = true;
            }
        }

        if (!is_current_month && !c->show_overflow)
            continue;

        const char *solar_term = NULL;
        const char *holiday = NULL;
        const char *makeup_label = NULL;
        bool is_holiday = false;

        if (is_current_month) {
            solar_term = widget_calendar_get_solar_term(c->month, display_day);
            for (int h = 0; h < kHolidayCount; h++) {
                if (kHolidays[h].month == c->month && kHolidays[h].day == display_day) {
                    holiday = kHolidays[h].name;
                    break;
                }
            }

            int q_year = c->year, q_month = c->month, q_day = display_day;
            if (c->hol_provider) {
                if (c->hol_provider->is_holiday(q_year, q_month, q_day)) {
                    is_holiday = true;
                    holiday = c->hol_provider->get_holiday_name(q_year, q_month, q_day);
                    if (!holiday)
                        holiday = "休";
                } else if (c->hol_provider->is_makeup_workday(q_year, q_month, q_day)) {
                    makeup_label = c->hol_provider->get_makeup_label(q_year, q_month, q_day);
                }
            }
        }

        char buf[16];
        snprintf(buf, sizeof(buf), "%d", display_day);
        int num_w = rawdraw_measure_text_width(buf, c->body_font);
        int kDateBoxH = 18;
        int kDateTopPad = 2;
        int kDateLunarGap = 2;
        int kLunarBoxH = 16;
        int num_x = cx + (c->cell_w - num_w) / 2;
        int num_box_y = cy + kDateTopPad;
        int num_y = rawdraw_layout_ink_centered_text_top_y_in_box(c->body_font, buf, num_box_y, kDateBoxH, 0);

        if (num_x + num_w > c->x + c->w)
            continue;
        if (num_y + kDateBoxH > c->y + c->h)
            continue;

        if (is_today) {
            rawdraw_rect_t hl = {cx + 5, cy + 1, c->cell_w - 10, c->cell_h - 2};
            if (hl.x + hl.w > c->x + c->w)
                hl.w = c->x + c->w - hl.x;
            if (hl.y + hl.h > c->y + c->h)
                hl.h = c->y + c->h - hl.y;
            rawdraw_draw_styled_round_rect(fb, width, height, hl, STYLE_BORDER_RADIUS_SM, &today_style);
            rawdraw_draw_styled_text(fb, width, height, num_x, num_y, buf, c->body_font, &today_style);
        } else if (!is_current_month) {
            rawdraw_draw_text(fb, width, height, num_x, num_y, buf, c->body_font, dim_text);
        } else if (is_holiday) {
            /* Holiday: red circle background + white date number. */
            int circle_cx = cx + c->cell_w / 2;
            int circle_cy = num_box_y + kDateBoxH / 2;
            int circle_r = kDateBoxH / 2 + 1;
            rawdraw_draw_circle(fb, width, height, (rawdraw_point_t){circle_cx, circle_cy}, circle_r,
                                RAWDRAW_COLOR_RED);
            rawdraw_draw_text(fb, width, height, num_x, num_y, buf, c->body_font, RAWDRAW_COLOR_WHITE);
        } else if (makeup_label) {
            /* Makeup workday (补班): yellow circle background + white date number. */
            int circle_cx = cx + c->cell_w / 2;
            int circle_cy = num_box_y + kDateBoxH / 2;
            int circle_r = kDateBoxH / 2 + 1;
            rawdraw_draw_circle(fb, width, height, (rawdraw_point_t){circle_cx, circle_cy}, circle_r,
                                RAWDRAW_COLOR_YELLOW);
            rawdraw_draw_text(fb, width, height, num_x, num_y, buf, c->body_font, RAWDRAW_COLOR_BLACK);
        } else {
            rawdraw_draw_text(fb, width, height, num_x, num_y, buf, c->body_font, text_color);
        }

        if (c->show_lunar || solar_term || holiday || makeup_label) {
            const char *label = NULL;
            bool is_solar_term = false;

            if (solar_term) {
                label = solar_term;
                is_solar_term = true;
            } else if (holiday) {
                label = holiday;
            } else if (makeup_label) {
                label = makeup_label;
            } else if (c->show_lunar && is_current_month) {
                widget_calendar_lunar_date_t ld = widget_calendar_to_lunar_date(c->year, c->month, display_day);
                if (ld.lunar_day == 1) {
                    label = widget_calendar_get_lunar_month_name(ld.lunar_month);
                } else {
                    label = widget_calendar_get_lunar_day_name(ld.lunar_day);
                }
            }

            if (label && *label) {
                int label_w = rawdraw_measure_text_width(label, c->small_font);
                int label_x = cx + (c->cell_w - label_w) / 2;
                int label_box_y = num_box_y + kDateBoxH + kDateLunarGap;
                int label_y =
                    rawdraw_layout_ink_centered_text_top_y_in_box(c->small_font, label, label_box_y, kLunarBoxH, 0);

                if (label_x + label_w <= c->x + c->w && label_box_y + kLunarBoxH <= c->y + c->h) {
                    rawdraw_color_t label_color =
                        is_today ? today_style.fg
                                 : (is_solar_term ? accent : (is_holiday ? RAWDRAW_COLOR_RED : text_color));
                    rawdraw_draw_text(fb, width, height, label_x, label_y, label, c->small_font, label_color);
                }
            }
        }
    }
}

static void draw_bottom_info(const widget_calendar_t *c, uint8_t *fb, int width, int height, int y)
{
    if (y + (int)c->small_font->line_height > c->y + c->h - STYLE_SPACING_SM)
        return;

    char buf[80];
    if (c->year == c->today_year && c->month == c->today_month) {
        int weekday_idx = weekday_of_date(c->today_year, c->today_month, c->today_day);
        snprintf(buf, sizeof(buf), "今天 %d月%d日 %s", c->today_month, c->today_day, kWeekdayFull[weekday_idx]);

        widget_calendar_lunar_date_t ld = widget_calendar_to_lunar_date(c->today_year, c->today_month, c->today_day);
        char year_name[16];
        widget_calendar_get_lunar_year_name(ld.lunar_year, year_name, sizeof(year_name));
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), " %s年%s%s", year_name,
                 widget_calendar_get_lunar_month_name(ld.lunar_month),
                 widget_calendar_get_lunar_day_name(ld.lunar_day));
    } else {
        int dim = days_in_month(c->year, c->month);
        snprintf(buf, sizeof(buf), "%d年%d月 共%d天", c->year, c->month, dim);
    }

    int text_w = rawdraw_measure_text_width(buf, c->small_font);
    int text_x = c->x + (c->w - text_w) / 2;
    text_x = (text_x + 7) & ~7;
    rawdraw_draw_text(fb, width, height, text_x, y, buf, c->small_font,
                      rawdraw_theme_color_for(THEME_TOKEN_TEXT_SECONDARY));
}

static void draw_selection_cursor(const widget_calendar_t *c, uint8_t *fb, int width, int height, int grid_y)
{
    if (!c->selection_mode || c->sel_row < 0 || c->sel_col < 0)
        return;

    int cx = c->x + c->sel_col * c->cell_w;
    int cy = grid_y + c->sel_row * c->cell_h;

    rawdraw_color_t focus = rawdraw_theme_color_for(THEME_TOKEN_FOCUS);
    int border_w = 3;

    rawdraw_rect_t cursor_r = {cx, cy, c->cell_w, c->cell_h};
    rawdraw_draw_rect_border(fb, width, height, cursor_r, border_w, focus);

    const char *cursor_mark = ">";
    if (c->small_font) {
        int mark_x = cx + STYLE_SPACING_XS;
        int mark_y = cy + 1;
        mark_x = (mark_x + 7) & ~7;
        rawdraw_draw_text(fb, width, height, mark_x, mark_y, cursor_mark, c->small_font, focus);
    }
}

/* ============================================================
 * Main Render API
 * ============================================================ */

void widget_calendar_render(widget_calendar_t *c, uint8_t *fb, int fb_width, int fb_height)
{
    if (!c || !fb)
        return;

    const lv_font_t *tfont = c->title_font ? c->title_font : &SourceHanSansSC_Medium_slim;
    const lv_font_t *bfont = c->body_font ? c->body_font : &SourceHanSansSC_Regular_slim;
    const lv_font_t *sfont = c->small_font ? c->small_font : &SourceHanSansSC_Regular_slim;

    /* Temporarily set resolution-fallback fonts if missing */
    c->title_font = tfont;
    c->body_font = bfont;
    c->small_font = sfont;

    int title_bar_h = c->show_header ? STYLE_PANEL_TITLE_HEIGHT : 0;
    int weekday_h = 22;
    bool show_bottom_info = c->show_header;
    int bottom_reserve = show_bottom_info ? 26 : 0;
    int grid_total_h = c->h - title_bar_h - weekday_h - bottom_reserve;

    c->cell_h = grid_total_h / CALENDAR_ROWS;
    if (c->cell_h < 34)
        c->cell_h = 34;
    if (c->cell_h > 40)
        c->cell_h = 40;
    c->cell_w = c->w / CALENDAR_COLS;

    int base_y = c->y;

    if (c->show_header) {
        draw_header(c, fb, fb_width, fb_height);
    }
    base_y += title_bar_h;

    draw_weekday_row(c, fb, fb_width, fb_height, base_y);
    base_y += weekday_h;

    int grid_y = base_y;
    draw_grid(c, fb, fb_width, fb_height, base_y);
    base_y += c->cell_h * CALENDAR_ROWS;

    if (c->selection_mode) {
        draw_selection_cursor(c, fb, fb_width, fb_height, grid_y);
    }

    if (show_bottom_info) {
        draw_bottom_info(c, fb, fb_width, fb_height, base_y);
    }

    c->needs_full_refresh = false;
}
