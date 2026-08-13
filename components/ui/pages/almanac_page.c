/**
 * @file almanac_renderer.c
 * @brief Almanac page renderer — C port of C++ rawdraw::AlmanacRenderer.
 *
 * Shows today's Gregorian date, lunar date, solar term, weekday, and
 * simplified traditional almanac info (宜忌). Lunar conversion reuses the
 * widget_calendar_* helpers (shared with the calendar widget).
 */
#include "almanac_page.h"
#include "page_registry.h"

#include "rawdraw_ext.h"
#include "theme.h"
#include "style.h"
#include "layout.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/* Weekday characters (matches calendar.c) */
static const char *const kWeekdayFull[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};

/* Simplified yiji (宜忌) based on lunar day patterns.
 * This is a traditional approximation, not a full almanac calculation. */
static const char *const kYiTable[][4] = {
    {"祭祀", "祈福", "出行", "动土"}, {"嫁娶", "纳采", "订盟", "出行"}, {"开市", "交易", "立券", "纳财"},
    {"破土", "启钻", "安葬", "修坟"}, {"修造", "动土", "起基", "定磉"}, {"安床", "开市", "交易", "立券"},
    {"祭祀", "沐浴", "扫舍", "修造"}, {"祈福", "求嗣", "出行", "解除"}, {"嫁娶", "祭祀", "祈福", "出行"},
    {"开市", "立券", "交易", "纳财"},
};
static const char *const kJiTable[][3] = {
    {"破土", "安葬", "启钻"}, {"开仓", "出货财", "纳粟"}, {"词讼", "争执", "诽谤"}, {"嫁娶", "出行", "祈福"},
    {"安床", "移徙", "入宅"}, {"祭祀", "修造", "动土"},   {"开市", "纳财", "交易"}, {"出行", "解除", "拆卸"},
    {"破土", "启钻", "安葬"}, {"纳采", "订盟", "嫁娶"},
};

/* ------------------------------------------------------------------ */
/* Data interface                                                      */
/* ------------------------------------------------------------------ */

void almanac_page_refresh_data(page_renderer_t *self)
{
    almanac_page_t *r = (almanac_page_t *)self;
    time_t now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);

    r->year = tm_buf.tm_year + 1900;
    r->month = tm_buf.tm_mon + 1;
    r->day = tm_buf.tm_mday;
    r->weekday = tm_buf.tm_wday; /* 0=Sun */

    /* Lunar date via the calendar widget's algorithm. */
    r->lunar = widget_calendar_to_lunar_date(r->year, r->month, r->day);
    widget_calendar_get_lunar_year_name(r->year, r->lunar_year_name, (int)sizeof(r->lunar_year_name));

    /* Solar term (if today is one). */
    r->solar_term = widget_calendar_get_solar_term(r->month, r->day);

    /* Yiji (宜忌) — simplified, based on lunar day. */
    const int yi_idx = (r->lunar.lunar_day - 1) % 10;
    const int ji_idx = r->lunar.lunar_day % 10;
    for (int i = 0; i < 4; ++i)
        r->yi[i] = kYiTable[yi_idx][i];
    for (int i = 0; i < 3; ++i)
        r->ji[i] = kJiTable[ji_idx][i];
}

/* ------------------------------------------------------------------ */
/* PageRenderer vtable                                                 */
/* ------------------------------------------------------------------ */

void almanac_page_init(page_renderer_t *self, int width, int height)
{
    almanac_page_t *r = (almanac_page_t *)self;
    r->base.width = width;
    r->base.height = height;
    r->base.needs_full_refresh_flag = true;
    r->font = &SourceHanSansSC_Regular_slim;
    r->title_font = &SourceHanSansSC_Medium_slim;
    r->icon_font = &weather_icons_48;
    almanac_page_refresh_data(self);
}

static void draw_title_bar(almanac_page_t *r, uint8_t *fb, int width, int height)
{
    const rawdraw_paint_style_t bar_style = rawdraw_theme_style(THEME_TOKEN_BACKGROUND_SECONDARY);
    const rawdraw_color_t text = rawdraw_theme_color_for(THEME_TOKEN_TEXT_PRIMARY);
    const rawdraw_color_t border = rawdraw_theme_color_for(THEME_TOKEN_BORDER);
    const int title_y_start = STYLE_STATUS_BAR_HEIGHT;
    const int title_bar_h = ALMANAC_PAGE_TITLE_BAR_H;

    /* Background */
    rawdraw_draw_styled_rect(fb, width, height, (rawdraw_rect_t){0, title_y_start, width, title_bar_h}, &bar_style);

    /* Top divider (2px) */
    rawdraw_draw_hline(fb, width, height, title_y_start, 0, width, border);
    rawdraw_draw_hline(fb, width, height, title_y_start + 1, 0, width, border);

    /* Bottom divider (2px) */
    const int line_y = title_y_start + title_bar_h - 2;
    rawdraw_draw_hline(fb, width, height, line_y, 0, width, border);
    rawdraw_draw_hline(fb, width, height, line_y + 1, 0, width, border);

    /* Ink-centered title (avoids line_height centering pushing CJK up). */
    const int title_text_y =
        rawdraw_layout_ink_centered_text_top_y_in_box(r->font, "老黄历", title_y_start, title_bar_h, 1);
    rawdraw_draw_text(fb, width, height, STYLE_SPACING_LG, title_text_y, "老黄历", r->font, text);
}

void almanac_page_render(page_renderer_t *self, uint8_t *fb, int width, int height)
{
    almanac_page_t *r = (almanac_page_t *)self;
    if (!fb)
        return;

    const int content_top = STYLE_STATUS_BAR_HEIGHT + ALMANAC_PAGE_TITLE_BAR_H + STYLE_SPACING_XS;
    int y = content_top + STYLE_SPACING_MD;
    const rawdraw_color_t text = rawdraw_theme_color_for(THEME_TOKEN_TEXT_PRIMARY);
    const rawdraw_color_t secondary = rawdraw_theme_color_for(THEME_TOKEN_TEXT_SECONDARY);
    const rawdraw_color_t accent = rawdraw_theme_color_for(THEME_TOKEN_ACCENT);
    const rawdraw_color_t danger = rawdraw_theme_color_for(THEME_TOKEN_DANGER);
    const rawdraw_color_t border = rawdraw_theme_color_for(THEME_TOKEN_BORDER);

    /* === Title bar === */
    draw_title_bar(r, fb, width, height);

    /* === Large lunar year name + date, e.g. "丙午年 三月初八" === */
    char lunar_full[32];
    if (r->lunar.lunar_month > 0 && r->lunar.lunar_day > 0) {
        snprintf(lunar_full, sizeof(lunar_full), "%s年 %s%s", r->lunar_year_name,
                 widget_calendar_get_lunar_month_name(r->lunar.lunar_month),
                 widget_calendar_get_lunar_day_name(r->lunar.lunar_day));
    } else {
        snprintf(lunar_full, sizeof(lunar_full), "%s年", r->lunar_year_name);
    }

    const int lunar_w = rawdraw_measure_text_width(lunar_full, r->title_font);
    const int lunar_x = (width - lunar_w) / 2;
    rawdraw_draw_text(fb, width, height, lunar_x, y, lunar_full, r->title_font, accent);
    y += r->title_font->line_height + STYLE_SPACING_MD;

    /* === Gregorian date === */
    char greg_buf[64];
    snprintf(greg_buf, sizeof(greg_buf), "公历 %d年%d月%d日 %s", r->year, r->month, r->day, kWeekdayFull[r->weekday]);
    const int greg_w = rawdraw_measure_text_width(greg_buf, r->font);
    const int greg_x = (width - greg_w) / 2;
    rawdraw_draw_text(fb, width, height, greg_x, y, greg_buf, r->font, secondary);
    y += r->font->line_height + STYLE_SPACING_MD;

    /* === Solar term (if today) === */
    if (r->solar_term) {
        char st_buf[32];
        snprintf(st_buf, sizeof(st_buf), "【%s】", r->solar_term);
        const int st_w = rawdraw_measure_text_width(st_buf, r->title_font);
        const int st_x = (width - st_w) / 2;
        rawdraw_draw_text(fb, width, height, st_x, y, st_buf, r->title_font, accent);
        y += r->title_font->line_height + STYLE_SPACING_MD;
    }

    /* === Divider === */
    rawdraw_draw_hline(fb, width, height, y, STYLE_SPACING_LG, width - STYLE_SPACING_LG, border);
    y += STYLE_SPACING_SM;

    /* === 宜 (auspicious) section === */
    rawdraw_draw_text(fb, width, height, STYLE_SPACING_LG, y, "宜", r->title_font, accent);
    const int yi_label_w = rawdraw_measure_text_width("宜", r->title_font);
    const int yi_start = STYLE_SPACING_LG + yi_label_w + STYLE_SPACING_SM;
    for (int i = 0; i < 4; i++) {
        rawdraw_draw_text(fb, width, height, yi_start + i * 60, y, r->yi[i], r->font, text);
    }
    y += r->font->line_height + STYLE_SPACING_MD;

    /* === 忌 (inauspicious) section === */
    rawdraw_draw_text(fb, width, height, STYLE_SPACING_LG, y, "忌", r->title_font, danger);
    const int ji_label_w = rawdraw_measure_text_width("忌", r->title_font);
    const int ji_start = STYLE_SPACING_LG + ji_label_w + STYLE_SPACING_SM;
    for (int i = 0; i < 3; i++) {
        rawdraw_draw_text(fb, width, height, ji_start + i * 60, y, r->ji[i], r->font, text);
    }

    r->base.needs_full_refresh_flag = false;
}

bool almanac_page_handle_input(page_renderer_t *self, const ui_button_event_t *event)
{
    almanac_page_t *r = (almanac_page_t *)self;
    switch (event->type) {
    case BTN_UP_CLICK:
    case BTN_DOWN_CLICK:
        /* Navigate months (UP=prev, DOWN=next) */
        if (event->type == BTN_UP_CLICK) {
            r->month--;
            if (r->month < 1) {
                r->month = 12;
                r->year--;
            }
        } else {
            r->month++;
            if (r->month > 12) {
                r->month = 1;
                r->year++;
            }
        }
        r->lunar = widget_calendar_to_lunar_date(r->year, r->month, r->day);
        r->solar_term = widget_calendar_get_solar_term(r->month, r->day);
        r->base.needs_full_refresh_flag = true;
        return true;

    case BTN_BOOT_LONG_PRESS:
        /* Jump to today */
        almanac_page_refresh_data(self);
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

EXT_RAM_BSS_ATTR almanac_page_t s_almanac_instance;

const page_renderer_ops_t almanac_page_ops = {
    .init = almanac_page_init,
    .render = almanac_page_render,
    .handle_input = almanac_page_handle_input,
    .get_dirty_rect = NULL,
    .needs_full_refresh = NULL,
    .mark_full_refresh = NULL,
    .clear_full_refresh_flag = NULL,
    .append_text = NULL,
    .begin_stream = NULL,
    .end_stream = NULL,
};

PAGE_REGISTER(UI_PAGE_ALMANAC, "老黄历", NULL, false, 80, &almanac_page_ops, &s_almanac_instance.base);
