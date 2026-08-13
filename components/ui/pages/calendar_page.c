/**
 * @file calendar_page.c
 * @brief Calendar page renderer with embedded almanac sub-view.
 *
 * Navigation:
 *   Calendar mode: UP/DOWN=翻月, BOOT=进入老黄历
 *   Almanac mode:  UP/DOWN=翻日, BOOT=返回日历
 */
#include "calendar_page.h"
#include "page_registry.h"
#include "fa_settings.h"

#include "rawdraw_ext.h"
#include "theme.h"
#include "style.h"
#include "layout.h"
#include "footer_bar.h"
#include "nvs_state.h"
#include "holiday_fetcher.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static const lv_font_t *const kCalendarTitleFont = &SourceHanSansSC_Medium_slim;
static const lv_font_t *const kCalendarBodyFont  = &SourceHanSansSC_Regular_slim;

/* Weekday full names */
static const char *const kWeekdayFull[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};

/* Holiday data source injected into the calendar widget (decouples rawdraw from network). */
static const holiday_provider_t s_holiday_provider = {
    .is_holiday         = holiday_fetcher_is_holiday,
    .is_makeup_workday  = holiday_fetcher_is_makeup_workday,
    .get_holiday_name   = holiday_fetcher_get_holiday_name,
    .get_makeup_label   = holiday_fetcher_get_makeup_label,
};

/* Simplified yiji tables (copied from almanac_page.c) */
static const char *const kYiTable[][4] = {
    {"祭祀", "祈福", "出行", "动土"}, {"嫁娶", "纳采", "订盟", "出行"}, {"开市", "交易", "立券", "纳财"},
    {"破土", "启钻", "安葬", "修坟"}, {"修造", "动土", "起基", "定磉"}, {"安床", "开市", "交易", "立券"},
    {"祭祀", "沐浴", "扫舍", "修造"}, {"祈福", "求嗣", "出行", "解除"}, {"嫁娶", "祭祀", "祈福", "出行"},
    {"开市", "立券", "交易", "纳财"},
};

static int weekday_of_date_local(int year, int month, int day)
{
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    int              y   = year;
    if (month < 3)
        y--;
    return (y + y / 4 - y / 100 + y / 400 + t[month - 1] + day) % 7;
}
static const char *const kJiTable[][3] = {
    {"破土", "安葬", "启钻"}, {"开仓", "出货财", "纳粟"}, {"词讼", "争执", "诽谤"}, {"嫁娶", "出行", "祈福"},
    {"安床", "移徙", "入宅"}, {"祭祀", "修造", "动土"},   {"开市", "纳财", "交易"}, {"出行", "解除", "拆卸"},
    {"破土", "启钻", "安葬"}, {"纳采", "订盟", "嫁娶"},
};

/* ------------------------------------------------------------------ */
/* Almanac sub-view helpers                                            */
/* ------------------------------------------------------------------ */

static void refresh_almanac_data(calendar_page_t *r)
{
    r->alm_year    = r->today_year;
    r->alm_month   = r->today_month;
    r->alm_day     = r->today_day;
    r->alm_weekday = weekday_of_date_local(r->alm_year, r->alm_month, r->alm_day);

    r->alm_lunar = widget_calendar_to_lunar_date(r->alm_year, r->alm_month, r->alm_day);
    widget_calendar_get_lunar_year_name(r->alm_year, r->alm_lunar_year_name, (int)sizeof(r->alm_lunar_year_name));
    r->alm_solar_term = widget_calendar_get_solar_term(r->alm_month, r->alm_day);

    const int yi_idx = (r->alm_lunar.lunar_day - 1) % 10;
    const int ji_idx = r->alm_lunar.lunar_day % 10;
    for (int i = 0; i < 4; ++i)
        r->alm_yi[i] = kYiTable[yi_idx][i];
    for (int i = 0; i < 3; ++i)
        r->alm_ji[i] = kJiTable[ji_idx][i];
}

static int days_in_month_g(int year, int month)
{
    static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12)
        return 0;
    if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)))
        return 29;
    return d[month - 1];
}

static void navigate_almanac_day(calendar_page_t *r, int delta)
{
    r->alm_day += delta;
    if (r->alm_day < 1) {
        r->alm_month--;
        if (r->alm_month < 1) {
            r->alm_month = 12;
            r->alm_year--;
        }
        r->alm_day = days_in_month_g(r->alm_year, r->alm_month);
    } else if (r->alm_day > days_in_month_g(r->alm_year, r->alm_month)) {
        r->alm_day = 1;
        r->alm_month++;
        if (r->alm_month > 12) {
            r->alm_month = 1;
            r->alm_year++;
        }
    }
    r->alm_weekday = weekday_of_date_local(r->alm_year, r->alm_month, r->alm_day);
    r->alm_lunar   = widget_calendar_to_lunar_date(r->alm_year, r->alm_month, r->alm_day);
    widget_calendar_get_lunar_year_name(r->alm_year, r->alm_lunar_year_name, (int)sizeof(r->alm_lunar_year_name));
    r->alm_solar_term = widget_calendar_get_solar_term(r->alm_month, r->alm_day);

    const int yi_idx = (r->alm_lunar.lunar_day - 1) % 10;
    const int ji_idx = r->alm_lunar.lunar_day % 10;
    for (int i = 0; i < 4; ++i)
        r->alm_yi[i] = kYiTable[yi_idx][i];
    for (int i = 0; i < 3; ++i)
        r->alm_ji[i] = kJiTable[ji_idx][i];
}

static void render_almanac_view(calendar_page_t *r, uint8_t *fb, int width, int height)
{
    const rawdraw_paint_style_t bg_style = rawdraw_theme_style(THEME_TOKEN_BACKGROUND_PRIMARY);
    rawdraw_draw_styled_rect(fb, width, height,
                             (rawdraw_rect_t){0, STYLE_STATUS_BAR_HEIGHT, width, height - STYLE_STATUS_BAR_HEIGHT},
                             &bg_style);

    const rawdraw_color_t text      = rawdraw_theme_color_for(THEME_TOKEN_TEXT_PRIMARY);
    const rawdraw_color_t secondary = rawdraw_theme_color_for(THEME_TOKEN_TEXT_SECONDARY);
    const rawdraw_color_t accent    = rawdraw_theme_style(THEME_TOKEN_ACCENT).bg;
    const rawdraw_color_t danger    = rawdraw_theme_style(THEME_TOKEN_DANGER).bg;
    const rawdraw_color_t border    = rawdraw_theme_color_for(THEME_TOKEN_BORDER);

    int y = STYLE_STATUS_BAR_HEIGHT + 8;

    /* Large lunar year name + date */
    char lunar_full[32];
    if (r->alm_lunar.lunar_month > 0 && r->alm_lunar.lunar_day > 0) {
        snprintf(lunar_full, sizeof(lunar_full), "%s年 %s%s", r->alm_lunar_year_name,
                 widget_calendar_get_lunar_month_name(r->alm_lunar.lunar_month),
                 widget_calendar_get_lunar_day_name(r->alm_lunar.lunar_day));
    } else {
        snprintf(lunar_full, sizeof(lunar_full), "%s年", r->alm_lunar_year_name);
    }
    int lunar_w = rawdraw_measure_text_width(lunar_full, r->title_font);
    rawdraw_draw_text(fb, width, height, (width - lunar_w) / 2, y, lunar_full, r->title_font, accent);
    y += r->title_font->line_height + 6;

    /* Gregorian date */
    char greg_buf[64];
    snprintf(greg_buf, sizeof(greg_buf), "公历 %d年%d月%d日 %s", r->alm_year, r->alm_month, r->alm_day,
             kWeekdayFull[r->alm_weekday]);
    int greg_w = rawdraw_measure_text_width(greg_buf, r->body_font);
    rawdraw_draw_text(fb, width, height, (width - greg_w) / 2, y, greg_buf, r->body_font, secondary);
    y += r->body_font->line_height + 8;

    /* Solar term */
    if (r->alm_solar_term) {
        char st_buf[32];
        snprintf(st_buf, sizeof(st_buf), "【%s】", r->alm_solar_term);
        int st_w = rawdraw_measure_text_width(st_buf, r->title_font);
        rawdraw_draw_text(fb, width, height, (width - st_w) / 2, y, st_buf, r->title_font, accent);
        y += r->title_font->line_height + 8;
    }

    /* Divider */
    rawdraw_draw_hline(fb, width, height, y, 16, width - 16, border);
    y += 10;

    /* 宜 */
    rawdraw_draw_text(fb, width, height, 16, y, "宜", r->title_font, accent);
    int yi_label_w = rawdraw_measure_text_width("宜", r->title_font);
    int yi_start   = 16 + yi_label_w + 8;
    for (int i = 0; i < 4; i++)
        rawdraw_draw_text(fb, width, height, yi_start + i * 60, y, r->alm_yi[i], r->body_font, text);
    y += r->body_font->line_height + 8;

    /* 忌 */
    rawdraw_draw_text(fb, width, height, 16, y, "忌", r->title_font, danger);
    int ji_label_w = rawdraw_measure_text_width("忌", r->title_font);
    int ji_start   = 16 + ji_label_w + 8;
    for (int i = 0; i < 3; i++)
        rawdraw_draw_text(fb, width, height, ji_start + i * 60, y, r->alm_ji[i], r->body_font, text);

    /* Footer hints */
    widget_footer_bar_t footer;
    widget_footer_bar_init(&footer, width, height);
    widget_footer_bar_set_text(&footer, "UP上一天", "BOOT返回日历", "DN下一天");
    widget_footer_bar_render(&footer, fb, width, height);
}

/* ------------------------------------------------------------------ */
/* PageRenderer vtable                                                 */
/* ------------------------------------------------------------------ */

void calendar_page_init(page_renderer_t *self, int width, int height)
{
    calendar_page_t *r              = (calendar_page_t *)self;
    r->base.width                   = width;
    r->base.height                  = height;
    r->base.needs_full_refresh_flag = true;

    /* Get today's date. */
    time_t    now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    r->today_year  = tm_buf.tm_year + 1900;
    r->today_month = tm_buf.tm_mon + 1;
    r->today_day   = tm_buf.tm_mday;

    const int content_top = STYLE_STATUS_BAR_HEIGHT;
    widget_calendar_init(&r->cal, 0, content_top, width, height - content_top);
    widget_calendar_set_holiday_provider(&r->cal, &s_holiday_provider);

    r->title_font = kCalendarTitleFont;
    r->body_font  = kCalendarBodyFont;
    r->small_font = kCalendarBodyFont;
    widget_calendar_set_fonts(&r->cal, r->title_font, r->body_font, r->small_font);
    widget_calendar_set_show_lunar(&r->cal, true);
    widget_calendar_set_show_overflow_days(&r->cal, false);
    widget_calendar_set_show_header(&r->cal, false);

    /* Sync state. */
    /* P1: Restore navigated month from NVS on wake (deep sleep wipes RAM). */
    int32_t saved_y = 0, saved_m = 0;
    if (nvs_state_get_i32("cal_year", &saved_y) && nvs_state_get_i32("cal_month", &saved_m)) {
        if (saved_y >= 2020 && saved_y <= 2050 && saved_m >= 1 && saved_m <= 12) {
            r->year  = saved_y;
            r->month = saved_m;
        } else {
            r->year  = r->today_year;
            r->month = r->today_month;
        }
    } else {
        r->year  = r->today_year;
        r->month = r->today_month;
    }
    r->selected_date.year  = 0;
    r->selected_date.month = 0;
    r->selected_date.day   = 0;
    r->show_almanac        = false;
}

/* Page gained focus: request a redraw but keep the navigated month. */
static void calendar_page_enter(page_renderer_t *self)
{
    calendar_page_t *r = (calendar_page_t *)self;
    if (!r)
        return;
    r->base.needs_full_refresh_flag = true;
}

void calendar_page_render(page_renderer_t *self, uint8_t *fb, int width, int height)
{
    calendar_page_t *r = (calendar_page_t *)self;
    if (!fb)
        return;

    if (r->show_almanac) {
        render_almanac_view(r, fb, width, height);
    } else {
        widget_calendar_set_date(&r->cal, r->year, r->month);
        widget_calendar_render(&r->cal, fb, width, height);
    }

    r->base.needs_full_refresh_flag = false;
}

bool calendar_page_handle_input(page_renderer_t *self, const ui_button_event_t *event)
{
    calendar_page_t *r = (calendar_page_t *)self;

    /* Almanac sub-view mode. */
    if (r->show_almanac) {
        switch (event->type) {
        case BTN_UP_CLICK:
            navigate_almanac_day(r, -1);
            r->base.needs_full_refresh_flag = true;
            return true;
        case BTN_DOWN_CLICK:
            navigate_almanac_day(r, 1);
            r->base.needs_full_refresh_flag = true;
            return true;
        case BTN_BOOT_CLICK:
            r->show_almanac               = false;
            r->base.needs_full_refresh_flag = true;
            return true;
        case BTN_BOOT_LONG_PRESS:
            refresh_almanac_data(r);
            r->base.needs_full_refresh_flag = true;
            return true;
        default:
            return false;
        }
    }

    /* Normal calendar mode (not in selection). */
    switch (event->type) {
    case BTN_UP_CLICK:
        widget_calendar_prev_month(&r->cal);
        r->year                         = r->cal.year;
        r->month                        = r->cal.month;
        nvs_state_set_i32("cal_year", r->year);
        nvs_state_set_i32("cal_month", r->month);
        r->base.needs_full_refresh_flag = true;
        return true;

        widget_calendar_next_month(&r->cal);
        r->year                         = r->cal.year;
        r->month                        = r->cal.month;
        nvs_state_set_i32("cal_year", r->year);
        nvs_state_set_i32("cal_month", r->month);
        r->base.needs_full_refresh_flag = true;
        return true;

    case BTN_BOOT_CLICK:
        /* Enter almanac sub-view for today. */
        refresh_almanac_data(r);
        r->show_almanac               = true;
        r->base.needs_full_refresh_flag = true;
        return true;

    default:
        break;
    }

    return false;
}

/* ------------------------------------------------------------------ */
/* Data interface                                                      */
/* ------------------------------------------------------------------ */

calendar_page_selected_date_t calendar_page_get_selected_date(const page_renderer_t *self)
{
    const calendar_page_t *r = (const calendar_page_t *)self;
    return r->selected_date;
}

int calendar_page_get_year(const page_renderer_t *self)
{
    const calendar_page_t *r = (const calendar_page_t *)self;
    return r->year;
}

int calendar_page_get_month(const page_renderer_t *self)
{
    const calendar_page_t *r = (const calendar_page_t *)self;
    return r->month;
}

void calendar_page_get_voice_query_context(const page_renderer_t *self, char *out, int out_size)
{
    const calendar_page_t *r = (const calendar_page_t *)self;
    if (!out || out_size <= 0)
        return;
    out[0] = '\0';

    if (r->selected_date.year == 0 || r->selected_date.month == 0 || r->selected_date.day == 0) {
        return;
    }

    const widget_calendar_lunar_date_t ld =
        widget_calendar_to_lunar_date(r->selected_date.year, r->selected_date.month, r->selected_date.day);

    char buf[128];
    if (ld.lunar_year > 0) {
        char year_name[16];
        widget_calendar_get_lunar_year_name(r->selected_date.year, year_name, sizeof(year_name));
        const char *leap_prefix = ld.is_leap_month ? "闰" : "";
        snprintf(buf, sizeof(buf), "%d年%d月%d日 %s年%s%s%s", r->selected_date.year, r->selected_date.month,
                 r->selected_date.day, year_name, leap_prefix, widget_calendar_get_lunar_month_name(ld.lunar_month),
                 widget_calendar_get_lunar_day_name(ld.lunar_day));
    } else {
        snprintf(buf, sizeof(buf), "%d年%d月%d日", r->selected_date.year, r->selected_date.month, r->selected_date.day);
    }

    strncpy(out, buf, out_size - 1);
    out[out_size - 1] = '\0';
}

/* ------------------------------------------------------------------ */
/* vtable instance                                                     */
/* ------------------------------------------------------------------ */

EXT_RAM_BSS_ATTR calendar_page_t s_calendar_instance;

const page_renderer_ops_t calendar_page_ops = {
    .init                    = calendar_page_init,
    .enter                   = calendar_page_enter,
    .render                  = calendar_page_render,
    .handle_input            = calendar_page_handle_input,
    .get_dirty_rect          = NULL,
    .needs_full_refresh      = NULL,
    .mark_full_refresh       = NULL,
    .clear_full_refresh_flag = NULL,
    .append_text             = NULL,
    .begin_stream            = NULL,
    .end_stream              = NULL,
};

PAGE_REGISTER(UI_PAGE_CALENDAR, "日历", FA_SETTINGS_CALENDAR, true, 30, &calendar_page_ops, &s_calendar_instance.base);
