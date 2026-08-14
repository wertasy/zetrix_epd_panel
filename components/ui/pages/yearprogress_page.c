/**
 * @file yearprogress_renderer.c
 * @brief Year progress page renderer — C port of C++ rawdraw::YearProgressRenderer.
 *
 * Displays current date, year progress %, horizontal progress bar, and a
 * 12-month grid with UP/DOWN navigation. Days-in-year/month are computed
 * with the standard mktime() normalization algorithm.
 */
#include "yearprogress_page.h"
#include "page_registry.h"

#include "rawdraw_ext.h"
#include "theme.h"
#include "style.h"
#include "layout.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *const month_names[] = {"1月", "2月", "3月", "4月",  "5月",  "6月",
                                          "7月", "8月", "9月", "10月", "11月", "12月"};

static const char *const weekday_names[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};

/* ------------------------------------------------------------------ */
/* Date calculation helpers                                            */
/* ------------------------------------------------------------------ */

/* Days in the given Gregorian year (365/366) via mktime normalization:
 * the day before Jan 1 of year+1 is Dec 31 of year; its 0-based yday + 1
 * equals the number of days in the year. */
static int yearprogress_get_days_in_year(int year)
{
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = year + 1 - 1900;
    t.tm_mon = 0;
    t.tm_mday = 0; /* normalizes to Dec 31 of `year` */
    t.tm_hour = 12;
    t.tm_isdst = -1;
    mktime(&t);
    return t.tm_yday + 1;
}

/* Days in the given month (0-based month index) via mktime normalization:
 * the day before the 1st of the next month is the last day of the month. */
static int yearprogress_get_days_in_month(int year, int month)
{
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = year - 1900;
    t.tm_mon = month + 1;
    t.tm_mday = 0; /* normalizes to last day of `month` */
    t.tm_hour = 12;
    t.tm_isdst = -1;
    mktime(&t);
    return t.tm_mday;
}

static void yearprogress_format_date(const yearprogress_page_t *r, char *buf, int len)
{
    snprintf(buf, len, "%04d年%02d月%02d日 %s", r->year, r->month + 1, r->day, weekday_names[r->wday]);
}

static const char *yearprogress_get_month_name(int month)
{
    if (month < 0 || month > 11)
        return "";
    return month_names[month];
}

/* ------------------------------------------------------------------ */
/* Data interface                                                      */
/* ------------------------------------------------------------------ */

void yearprogress_page_update_time(page_renderer_t *self)
{
    yearprogress_page_t *r = (yearprogress_page_t *)self;
    time_t now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);

    r->year = tm_buf.tm_year + 1900;
    r->month = tm_buf.tm_mon;
    r->day = tm_buf.tm_mday;
    r->wday = tm_buf.tm_wday;
    r->day_of_year = tm_buf.tm_yday + 1; /* 1-based */

    r->total_days = yearprogress_get_days_in_year(r->year);
    r->progress_pct = (r->day_of_year * 100) / r->total_days;
}

/* ------------------------------------------------------------------ */
/* PageRenderer vtable                                                 */
/* ------------------------------------------------------------------ */

void yearprogress_page_init(page_renderer_t *self, int width, int height)
{
    yearprogress_page_t *r = (yearprogress_page_t *)self;
    r->base.width = width;
    r->base.height = height;
    r->selected_month = -1;
    r->base.needs_full_refresh_flag = true;
    r->title_font = &SourceHanSansSC_Medium_slim;
    r->body_font = &SourceHanSansSC_Regular_slim;
    r->small_font = &SourceHanSansSC_Regular_slim;
    r->icon_font = &font_zectrix_16_1;
    yearprogress_page_update_time(self);
}

/* Page gained focus: request a redraw but keep the selected month. */
static void yearprogress_page_enter(page_renderer_t *self)
{
    yearprogress_page_t *r = (yearprogress_page_t *)self;
    if (!r)
        return;
    r->base.needs_full_refresh_flag = true;
}

static void yearprogress_render_month_row(yearprogress_page_t *r, uint8_t *fb, int width, int height, int y, int month,
                                          bool is_past, bool is_current, bool is_selected)
{
    const rawdraw_paint_style_t selected_style = rawdraw_theme_component(ROLE_SETTINGS_SELECTED);
    rawdraw_paint_style_t progress_style = rawdraw_theme_component(ROLE_PROGRESS);
    const rawdraw_color_t text = rawdraw_theme_color_for(THEME_TOKEN_TEXT_PRIMARY);
    const rawdraw_color_t secondary = rawdraw_theme_color_for(THEME_TOKEN_TEXT_SECONDARY);
    const rawdraw_color_t success = rawdraw_theme_color_for(THEME_TOKEN_SUCCESS_LIKE);
    const rawdraw_color_t accent = rawdraw_theme_color_for(THEME_TOKEN_ACCENT);
    const int row_h = 22;

    const char *month_name = yearprogress_get_month_name(month);
    const int text_y = rawdraw_layout_ink_centered_text_top_y_in_box(r->small_font, month_name, y, row_h, 0);

    rawdraw_color_t fg = is_past ? success : text;

    /* Selected: inverted background */
    if (is_selected) {
        const int card_x = STYLE_SPACING_SM;
        const int card_w = width - 2 * STYLE_SPACING_SM;
        rawdraw_draw_styled_round_rect(fb, width, height, (rawdraw_rect_t){card_x, y, card_w, row_h},
                                       STYLE_BORDER_RADIUS_SM, &selected_style);
        fg = selected_style.fg;
        progress_style.bg = selected_style.bg;
        progress_style.border = selected_style.border;
    }

    /* Month name (left column) */
    int name_x = STYLE_SPACING_MD;
    name_x = (name_x + 7) & ~7;
    rawdraw_draw_text(fb, width, height, name_x, text_y, month_name, r->small_font, fg);

    /* Status indicator after month name */
    if (is_past) {
        /* Simple checkmark text */
        const int ok_x = name_x + rawdraw_measure_text_width(month_name, r->small_font) + STYLE_SPACING_XS;
        rawdraw_draw_text(fb, width, height, ok_x, text_y, "OK", r->small_font, fg);
    } else if (is_current && !is_selected) {
        /* Current month indicator: filled dot */
        const int dot_x = name_x + rawdraw_measure_text_width(month_name, r->small_font) + STYLE_SPACING_XS + 4;
        const int dot_y = y + row_h / 2;
        rawdraw_draw_circle(fb, width, height, (rawdraw_point_t){dot_x, dot_y}, 3, accent);
    }

    /* Mini progress bar (right side, ~40% width) */
    int bar_w = 80;
    bar_w = (bar_w + 7) & ~7;
    const int bar_h = 6;
    /* Reserve space for percentage text: measure "100%" as worst case */
    char worst_case_pct[8];
    snprintf(worst_case_pct, sizeof(worst_case_pct), "100%%");
    const int pct_reserved_w = rawdraw_measure_text_width(worst_case_pct, r->small_font) + STYLE_SPACING_SM;
    int bar_x = width - STYLE_SPACING_MD - bar_w - pct_reserved_w;
    bar_x = (bar_x + 7) & ~7;
    const int bar_y_offset = y + (row_h - bar_h) / 2;

    /* Calculate month progress */
    int month_pct = 0;
    if (is_past) {
        month_pct = 100;
    } else if (is_current) {
        month_pct = (r->day * 100) / yearprogress_get_days_in_month(r->year, r->month);
    }

    progress_style.fg = is_past ? success : (is_current ? accent : progress_style.fg);
    if (!is_current && !is_past) {
        progress_style.fg = secondary;
    }
    rawdraw_draw_styled_progress(fb, width, height, (rawdraw_rect_t){bar_x, bar_y_offset, bar_w, bar_h}, month_pct,
                                 &progress_style, STYLE_BORDER_RADIUS_PILL);

    /* Percentage text (right of progress bar) */
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", month_pct);
    const int pct_w = rawdraw_measure_text_width(buf, r->small_font);
    int pct_x = width - pct_w - STYLE_SPACING_MD;
    pct_x = (pct_x + 7) & ~7;
    rawdraw_draw_text(fb, width, height, pct_x, text_y, buf, r->small_font, fg);
}

static void yearprogress_render_month_grid(yearprogress_page_t *r, uint8_t *fb, int width, int height, int y_start)
{
    const rawdraw_color_t text = rawdraw_theme_color_for(THEME_TOKEN_TEXT_PRIMARY);
    const rawdraw_color_t border = rawdraw_theme_color_for(THEME_TOKEN_BORDER);
    const int row_h = 22; /* Slightly taller rows to prevent overlap */
    const int content_bottom = height - STYLE_SPACING_SM;
    const int rows_visible = (content_bottom - y_start - r->small_font->line_height - STYLE_SPACING_XS) / row_h;

    /* Section title */
    const char *section = "月份概览";
    int sec_x = STYLE_SPACING_MD;
    sec_x = (sec_x + 7) & ~7;
    rawdraw_draw_text(fb, width, height, sec_x, y_start, section, r->small_font, text);

    /* Divider line */
    const int line_y = y_start + r->small_font->line_height + STYLE_SPACING_XS;
    rawdraw_draw_hline(fb, width, height, line_y, STYLE_SPACING_MD, width - STYLE_SPACING_MD, border);

    int row_y = line_y + STYLE_SPACING_XS + 1;

    int scroll = 0;
    const int max_scroll = 12 > rows_visible ? (12 - rows_visible) : 0;
    if (r->selected_month >= 0) {
        if (r->selected_month < scroll)
            scroll = r->selected_month;
        else if (r->selected_month >= scroll + rows_visible)
            scroll = r->selected_month - rows_visible + 1;
    }
    if (scroll > max_scroll)
        scroll = max_scroll;

    for (int i = scroll; i < 12 && i < scroll + rows_visible; i++) {
        if (row_y + row_h > content_bottom)
            break;

        const bool is_past = i < r->month;
        const bool is_current = (i == r->month);
        const bool is_selected = (i == r->selected_month);

        yearprogress_render_month_row(r, fb, width, height, row_y, i, is_past, is_current, is_selected);
        row_y += row_h;
    }
}

void yearprogress_page_render(page_renderer_t *self, uint8_t *fb, int width, int height)
{
    yearprogress_page_t *r = (yearprogress_page_t *)self;
    if (!fb)
        return;

    const rawdraw_paint_style_t progress_style = rawdraw_theme_component(ROLE_PROGRESS);
    const rawdraw_color_t text = rawdraw_theme_color_for(THEME_TOKEN_TEXT_PRIMARY);
    const rawdraw_color_t secondary = rawdraw_theme_color_for(THEME_TOKEN_TEXT_SECONDARY);
    const rawdraw_color_t accent = rawdraw_theme_color_for(THEME_TOKEN_ACCENT);

    /* Refresh time periodically */
    yearprogress_page_update_time(self);

    const int content_top = STYLE_STATUS_BAR_HEIGHT + 8; /* 8px gap after status bar */
    const int content_bottom = height - STYLE_SPACING_SM;
    int y = content_top;

    /* === Section 1: Title "年度进度" === */
    const char *title = "年度进度";
    const int title_w = rawdraw_measure_text_width(title, r->title_font);
    int title_x = (width - title_w) / 2;
    title_x = (title_x + 7) & ~7;
    rawdraw_draw_text(fb, width, height, title_x, y, title, r->title_font, text);
    y += r->title_font->line_height + 20; /* 20px gap after title */

    /* === Section 2: Date line === */
    char date_buf[48];
    yearprogress_format_date(r, date_buf, sizeof(date_buf));
    const int date_w = rawdraw_measure_text_width(date_buf, r->body_font);
    int date_x = (width - date_w) / 2;
    date_x = (date_x + 7) & ~7;
    rawdraw_draw_text(fb, width, height, date_x, y, date_buf, r->body_font, secondary);
    y += r->body_font->line_height + 20; /* 20px gap after date */

    /* === Section 3: Large progress percentage === */
    char pct_str[16];
    snprintf(pct_str, sizeof(pct_str), "%d%%", r->progress_pct);
    const int pct_w = rawdraw_measure_text_width(pct_str, r->title_font);
    int pct_x = (width - pct_w) / 2;
    pct_x = (pct_x + 7) & ~7;
    rawdraw_draw_text(fb, width, height, pct_x, y, pct_str, r->title_font, accent);
    y += r->title_font->line_height + 20; /* 20px gap after percentage */

    /* === Section 4: Progress bar === */
    int bar_w = (width * 80) / 100;
    bar_w = (bar_w + 7) & ~7;
    const int bar_h = STYLE_PROGRESS_HEIGHT + 4;
    int bar_x = (width - bar_w) / 2;
    bar_x = (bar_x + 7) & ~7;

    rawdraw_draw_styled_progress(fb, width, height, (rawdraw_rect_t){bar_x, y, bar_w, bar_h}, r->progress_pct,
                                 &progress_style, STYLE_BORDER_RADIUS_PILL);
    y += bar_h + 20; /* 20px gap after progress bar */

    /* === Section 5: "第X天/共Y天" === */
    char day_str[64];
    snprintf(day_str, sizeof(day_str), "第%d天 / 共%d天", r->day_of_year, r->total_days);
    const int day_str_w = rawdraw_measure_text_width(day_str, r->small_font);
    int day_str_x = (width - day_str_w) / 2;
    day_str_x = (day_str_x + 7) & ~7;
    rawdraw_draw_text(fb, width, height, day_str_x, y, day_str, r->small_font, secondary);
    y += r->small_font->line_height + 20; /* 20px gap after day count */

    /* === Section 6: Month overview (only if enough space) === */
    if (y < content_bottom - r->small_font->line_height * 2) {
        yearprogress_render_month_grid(r, fb, width, height, y);
    }

    r->base.needs_full_refresh_flag = false;
}

bool yearprogress_page_handle_input(page_renderer_t *self, const ui_button_event_t *event)
{
    yearprogress_page_t *r = (yearprogress_page_t *)self;
    switch (event->type) {
    case BTN_UP_CLICK:
        /* Previous month (or select December from overview) */
        if (r->selected_month < 0) {
            r->selected_month = 11; /* Start at December */
        } else if (r->selected_month > 0) {
            r->selected_month--;
        } else {
            r->selected_month = 11; /* Wrap to December */
        }
        r->base.needs_full_refresh_flag = true;
        return true;

    case BTN_DOWN_CLICK:
        /* Next month (or select January from overview) */
        if (r->selected_month < 0) {
            r->selected_month = 0; /* Start at January */
        } else if (r->selected_month < 11) {
            r->selected_month++;
        } else {
            r->selected_month = 0; /* Wrap to January */
        }
        r->base.needs_full_refresh_flag = true;
        return true;

    case BTN_BOOT_CLICK:
        /* Return to overview */
        if (r->selected_month >= 0) {
            r->selected_month = -1;
            r->base.needs_full_refresh_flag = true;
            return true;
        }
        break;

    case BTN_UP_LONG_PRESS:
        /* Jump to current month */
        r->selected_month = r->month;
        r->base.needs_full_refresh_flag = true;
        return true;

    default:
        break;
    }

    return false;
}

/* ------------------------------------------------------------------ */
/* vtable instance                                                     */
/* ------------------------------------------------------------------ */

EXT_RAM_BSS_ATTR yearprogress_page_t s_year_progress_instance;

const page_renderer_ops_t yearprogress_page_ops = {
    .init = yearprogress_page_init,
    .enter = yearprogress_page_enter,
    .render = yearprogress_page_render,
    .handle_input = yearprogress_page_handle_input,
    .get_dirty_rect = NULL,
    .needs_full_refresh = NULL,
    .mark_full_refresh = NULL,
    .clear_full_refresh_flag = NULL,
    .append_text = NULL,
    .begin_stream = NULL,
    .end_stream = NULL,
};

PAGE_REGISTER(UI_PAGE_YEAR_PROGRESS, "年度进度", NULL, true, 110, &yearprogress_page_ops,
              &s_year_progress_instance.base);
