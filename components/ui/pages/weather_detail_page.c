/**
 * @file weather_detail_renderer.c
 * @brief Weather detail page renderer — C port of C++ rawdraw::WeatherDetailRenderer.
 *
 * Shows current weather summary + metrics panel and a 24 h temperature
 * curve built from the hourly timeline. The selected hour is highlighted;
 * a detail modal shows the full info for it.
 */
#include "weather_detail_page.h"
#include "page_registry.h"

#include "rawdraw_ext.h"
#include "theme.h"
#include "style.h"
#include "layout.h"
#include "ui_text_util.h"
#include "weather_icons.h"
#include "modal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const lv_font_t *const detail_font = &SourceHanSansSC_Regular_slim;
static const lv_font_t *const detail_title_font = &SourceHanSansSC_Medium_slim;
static const lv_font_t *const detail_icon_font = &weather_icons_16;

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static void build_fallback_timeline(weather_detail_page_t *r);
static void draw_hour_detail_modal(weather_detail_page_t *r, uint8_t *fb, int width, int height);

/* ------------------------------------------------------------------ */
/* PageRenderer vtable                                                 */
/* ------------------------------------------------------------------ */

void weather_detail_page_init(page_renderer_t *self, int width, int height)
{
    weather_detail_page_t *r = (weather_detail_page_t *)self;
    r->base.width = width;
    r->base.height = height;
    r->base.needs_full_refresh_flag = true;
    r->font = detail_font;
    r->title_font = detail_title_font;
    r->icon_font = detail_icon_font;
    r->hourly_count = 0;
    r->selected_hour = 0;
    r->detail_open = false;
    r->has_data = false;
    memset(&r->data, 0, sizeof(r->data));
    memset(r->hourly, 0, sizeof(r->hourly));
}

/* Page gained focus: request a redraw but keep previously fetched data. */
static void weather_detail_page_enter(page_renderer_t *self)
{
    weather_detail_page_t *r = (weather_detail_page_t *)self;
    if (!r)
        return;
    r->base.needs_full_refresh_flag = true;
}

void weather_detail_page_render(page_renderer_t *self, uint8_t *fb, int width, int height)
{
    weather_detail_page_t *r = (weather_detail_page_t *)self;
    if (!fb)
        return;

    const rawdraw_paint_style_t bg_style = rawdraw_theme_style(THEME_TOKEN_BACKGROUND_PRIMARY);
    const rawdraw_paint_style_t card_style = rawdraw_theme_component(ROLE_CARD_DEFAULT);
    const rawdraw_paint_style_t selected_style = rawdraw_theme_component(ROLE_SETTINGS_SELECTED);
    const rawdraw_color_t text = rawdraw_theme_color_for(THEME_TOKEN_TEXT_PRIMARY);
    const rawdraw_color_t secondary = rawdraw_theme_color_for(THEME_TOKEN_TEXT_SECONDARY);
    const rawdraw_color_t accent = rawdraw_theme_color_for(THEME_TOKEN_ACCENT);

    const int content_top = STYLE_STATUS_BAR_HEIGHT + 8;
    rawdraw_draw_styled_rect(fb, width, height,
                             (rawdraw_rect_t){0, STYLE_STATUS_BAR_HEIGHT, width, height - STYLE_STATUS_BAR_HEIGHT},
                             &bg_style);

    if (!r->has_data) {
        widget_modal_t modal;
        widget_modal_init(&modal);
        widget_modal_set_title(&modal, "暂无天气详情");
        widget_modal_set_footer(&modal, "等待天气数据");
        widget_modal_center_in_screen(&modal, width, height, 52);
        widget_modal_render(&modal, fb, width, height);
    } else {
        /* Current condition: icon + temperature + description. */
        const char *glyph = ui_text_icon_glyph_for_code(r->data.weather_icon, r->data.weather_text);
        rawdraw_draw_text(fb, width, height, 86,
                          rawdraw_layout_ink_centered_text_top_y(r->icon_font, glyph, content_top + 38, 0), glyph,
                          r->icon_font, accent);
        char temp_buf[20];
        const char *temp_src = r->data.temp[0] != '\0' ? r->data.temp : "--";
        snprintf(temp_buf, sizeof(temp_buf), "%.8s°C", temp_src);
        rawdraw_draw_text(fb, width, height, 170,
                          rawdraw_layout_ink_centered_text_top_y(r->title_font, temp_buf, content_top + 68, 0),
                          temp_buf, r->title_font, accent);
        const char *weather_text = r->data.weather_text[0] != '\0' ? r->data.weather_text : "--";
        rawdraw_draw_text(fb, width, height, 178,
                          rawdraw_layout_ink_centered_text_top_y(r->font, weather_text, content_top + 94, 0),
                          weather_text, r->font, text);

        /* Metrics panel. */
        const rawdraw_rect_t metrics = {238, content_top + 16, 130, 108};
        rawdraw_draw_styled_round_rect(fb, width, height, metrics, STYLE_BORDER_RADIUS_MD, &card_style);
        const char *labels[] = {"体感温度", "湿度", "能见度", "气压"};
        char values[4][16];
        const char *feels_src =
            r->data.feels_like[0] != '\0' ? r->data.feels_like : (r->data.temp[0] != '\0' ? r->data.temp : "--");
        snprintf(values[0], sizeof(values[0]), "%.10s°C", feels_src);
        const char *hum_src = r->data.humidity[0] != '\0' ? r->data.humidity : "--";
        snprintf(values[1], sizeof(values[1]), "%.6s%%", hum_src);
        strcpy(values[2], "20km");
        strcpy(values[3], "1012hPa");
        for (int i = 0; i < 4; ++i) {
            const int center_y = metrics.y + 20 + i * 24;
            rawdraw_draw_text(fb, width, height, metrics.x + 16,
                              rawdraw_layout_ink_centered_text_top_y(r->font, labels[i], center_y, 0), labels[i],
                              r->font, secondary);
            const int val_w = rawdraw_measure_text_width(values[i], r->font);
            rawdraw_draw_text(fb, width, height, metrics.x + metrics.w - val_w - 16,
                              rawdraw_layout_ink_centered_text_top_y(r->font, values[i], center_y, 0), values[i],
                              r->font, text);
        }

        /* Hourly timeline: 24 h temperature curve + hour columns. */
        const rawdraw_rect_t timeline = {50, 178, width - 100, 108};
        rawdraw_draw_styled_round_rect(fb, width, height, timeline, STYLE_BORDER_RADIUS_MD, &card_style);

        const int count = r->hourly_count;
        if (count > 0) {
            /* Temperature curve across all hourly points. */
            int min_temp = 0, max_temp = 0;
            for (int i = 0; i < count; ++i) {
                if (i == 0 || r->hourly[i].temp < min_temp)
                    min_temp = r->hourly[i].temp;
                if (i == 0 || r->hourly[i].temp > max_temp)
                    max_temp = r->hourly[i].temp;
            }
            if (max_temp - min_temp < 2)
                max_temp = min_temp + 2; /* avoid div-by-zero */

            const int curve_top = timeline.y + 10;
            const int curve_bottom = timeline.y + 38;
            rawdraw_point_t curve_pts[WEATHER_DETAIL_MAX_HOURS];
            for (int i = 0; i < count; ++i) {
                const int x = timeline.x + 14 + i * (timeline.w - 28) / (count > 1 ? count - 1 : 1);
                const int y =
                    curve_bottom - (r->hourly[i].temp - min_temp) * (curve_bottom - curve_top) / (max_temp - min_temp);
                curve_pts[i].x = x;
                curve_pts[i].y = y;
            }
            for (int i = 1; i < count; ++i) {
                rawdraw_draw_line(fb, width, height, curve_pts[i - 1], curve_pts[i], secondary);
            }
            for (int i = 0; i < count; ++i) {
                const bool sel = (i == r->selected_hour);
                rawdraw_draw_circle_border(fb, width, height, curve_pts[i], sel ? 3 : 2, 1, sel ? accent : text);
            }

            /* Visible window of columns under the curve. */
            const int visible = RD_MIN(5, count);
            const int start = RD_MAX(0, RD_MIN(r->selected_hour - 2, count - visible));
            const int usable_x = timeline.x + 10;
            const int usable_w = timeline.w - 20;
            const int col_w = usable_w / RD_MAX(1, visible);

            for (int col = 0; col < visible; ++col) {
                const int i = start + col;
                const weather_hour_point_t *point = &r->hourly[i];
                const int cx = usable_x + col * col_w + col_w / 2;
                const bool sel = (i == r->selected_hour);

                const int label_w = rawdraw_measure_text_width(point->label, r->font);
                rawdraw_draw_text(fb, width, height, cx - label_w / 2,
                                  rawdraw_layout_ink_centered_text_top_y(r->font, point->label, timeline.y + 50, 0),
                                  point->label, r->font, sel ? accent : secondary);

                const char *hour_glyph = ui_text_icon_glyph_for_code(point->icon_code, point->weather_text);
                const int glyph_w = rawdraw_measure_text_width(hour_glyph, r->icon_font);
                rawdraw_draw_text(fb, width, height, cx - glyph_w / 2,
                                  rawdraw_layout_ink_centered_text_top_y(r->icon_font, hour_glyph, timeline.y + 68, 0),
                                  hour_glyph, r->icon_font, sel ? accent : text);

                char hour_temp[12];
                snprintf(hour_temp, sizeof(hour_temp), "%d°C", (int)point->temp);
                const int temp_w = rawdraw_measure_text_width(hour_temp, r->font);
                rawdraw_draw_text(fb, width, height, cx - temp_w / 2,
                                  rawdraw_layout_ink_centered_text_top_y(r->font, hour_temp, timeline.y + 86, 0),
                                  hour_temp, r->font, text);
                if (sel) {
                    rawdraw_draw_hline(fb, width, height, timeline.y + 96, cx - 14, cx + 14, selected_style.bg);
                }
            }
        }
    }

    if (r->detail_open && r->hourly_count > 0) {
        draw_hour_detail_modal(r, fb, width, height);
    }

    r->base.needs_full_refresh_flag = false;
}

bool weather_detail_page_handle_input(page_renderer_t *self, const ui_button_event_t *event)
{
    weather_detail_page_t *r = (weather_detail_page_t *)self;
    if (r->detail_open) {
        if (event->type == BTN_BOOT_CLICK || event->type == BTN_BOOT_LONG_PRESS) {
            r->detail_open = false;
            r->base.needs_full_refresh_flag = true;
            return true;
        }
        return true;
    }

    switch (event->type) {
    case BTN_UP_CLICK:
        if (r->selected_hour > 0) {
            r->selected_hour--;
            r->base.needs_full_refresh_flag = true;
            return true;
        }
        break;
    case BTN_DOWN_CLICK:
        if (r->selected_hour < r->hourly_count - 1) {
            r->selected_hour++;
            r->base.needs_full_refresh_flag = true;
            return true;
        }
        break;
    default:
        break;
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* Data interface                                                      */
/* ------------------------------------------------------------------ */

void weather_detail_page_update(page_renderer_t *self, const weather_data_t *data)
{
    weather_detail_page_t *r = (weather_detail_page_t *)self;
    if (!data)
        return;
    r->data = *data;
    r->has_data = true;
    if (r->hourly_count == 0) {
        build_fallback_timeline(r);
    }
    if (r->hourly_count > 0 && r->selected_hour >= r->hourly_count) {
        r->selected_hour = r->hourly_count - 1;
    }
    if (r->selected_hour < 0) {
        r->selected_hour = 0;
    }
    r->base.needs_full_refresh_flag = true;
}

void weather_detail_page_set_hourly_forecast(page_renderer_t *self, const weather_hour_point_t *points, int count)
{
    weather_detail_page_t *r = (weather_detail_page_t *)self;
    if (!points || count <= 0) {
        r->hourly_count = 0;
    } else {
        const int n = RD_MIN(count, WEATHER_DETAIL_MAX_HOURS);
        for (int i = 0; i < n; ++i) {
            strncpy(r->hourly[i].label, points[i].label, sizeof(r->hourly[i].label) - 1);
            r->hourly[i].label[sizeof(r->hourly[i].label) - 1] = '\0';
            strncpy(r->hourly[i].icon_code, points[i].icon_code, sizeof(r->hourly[i].icon_code) - 1);
            r->hourly[i].icon_code[sizeof(r->hourly[i].icon_code) - 1] = '\0';
            strncpy(r->hourly[i].weather_text, points[i].weather_text, sizeof(r->hourly[i].weather_text) - 1);
            r->hourly[i].weather_text[sizeof(r->hourly[i].weather_text) - 1] = '\0';
            r->hourly[i].temp = points[i].temp;
        }
        r->hourly_count = n;
    }
    if (r->hourly_count > 0 && r->selected_hour >= r->hourly_count) {
        r->selected_hour = r->hourly_count - 1;
    }
    if (r->selected_hour < 0) {
        r->selected_hour = 0;
    }
    r->base.needs_full_refresh_flag = true;
}

/* ------------------------------------------------------------------ */
/* Detail modal                                                        */
/* ------------------------------------------------------------------ */

static void draw_hour_detail_modal(weather_detail_page_t *r, uint8_t *fb, int width, int height)
{
    const weather_hour_point_t *point = &r->hourly[r->selected_hour];
    const rawdraw_color_t text = rawdraw_theme_color_for(THEME_TOKEN_TEXT_PRIMARY);
    const rawdraw_color_t secondary = rawdraw_theme_color_for(THEME_TOKEN_TEXT_SECONDARY);
    const rawdraw_color_t accent = rawdraw_theme_color_for(THEME_TOKEN_ACCENT);

    widget_modal_t modal;
    widget_modal_init(&modal);
    widget_modal_set_title(&modal, "小时详情");
    widget_modal_set_footer(&modal, "BOOT关闭");
    widget_modal_center_in_screen(&modal, width, height, 42);
    widget_modal_render(&modal, fb, width, height);

    const rawdraw_rect_t body = widget_modal_get_content_bounds(&modal);
    char temp_buf[16];
    snprintf(temp_buf, sizeof(temp_buf), "%d°C", (int)point->temp);
    rawdraw_draw_text(fb, width, height, body.x, body.y, point->label, r->title_font, accent);
    rawdraw_draw_text(fb, width, height, body.x, body.y + 24, temp_buf, r->title_font, text);
    rawdraw_draw_text(fb, width, height, body.x, body.y + 48, point->weather_text, r->font, secondary);
}

/* ------------------------------------------------------------------ */
/* Fallback timeline                                                   */
/* ------------------------------------------------------------------ */

static void build_fallback_timeline(weather_detail_page_t *r)
{
    r->hourly_count = 0;

    /* Prefer the daily forecast: one point per day (label/icon/text/temp). */
    if (r->data.forecast_count > 0) {
        const int n = RD_MIN(r->data.forecast_count, WEATHER_DETAIL_MAX_HOURS);
        for (int i = 0; i < n; ++i) {
            const weather_forecast_day_t *src = &r->data.forecast[i];
            weather_hour_point_t *dst = &r->hourly[i];
            strncpy(dst->label, src->label, sizeof(dst->label) - 1);
            dst->label[sizeof(dst->label) - 1] = '\0';
            strncpy(dst->icon_code, src->icon_code, sizeof(dst->icon_code) - 1);
            dst->icon_code[sizeof(dst->icon_code) - 1] = '\0';
            strncpy(dst->weather_text, src->weather_text, sizeof(dst->weather_text) - 1);
            dst->weather_text[sizeof(dst->weather_text) - 1] = '\0';
            dst->temp = (src->temp_min + src->temp_max) / 2;
            r->hourly_count++;
        }
        return;
    }

    /* No forecast either — synthesize hours from the current temperature. */
    const int now_temp = r->data.temp[0] != '\0' ? atoi(r->data.temp) : (int)r->data.temp_int;
    static const char *const fallback_labels[] = {"现在", "3时", "6时", "9时", "12时", "15时"};
    static const int offsets[] = {0, -1, -2, 0, 2, 1};
    for (int i = 0; i < 6; ++i) {
        weather_hour_point_t *dst = &r->hourly[i];
        strncpy(dst->label, fallback_labels[i], sizeof(dst->label) - 1);
        dst->label[sizeof(dst->label) - 1] = '\0';
        strncpy(dst->icon_code, r->data.weather_icon, sizeof(dst->icon_code) - 1);
        dst->icon_code[sizeof(dst->icon_code) - 1] = '\0';
        strncpy(dst->weather_text, r->data.weather_text, sizeof(dst->weather_text) - 1);
        dst->weather_text[sizeof(dst->weather_text) - 1] = '\0';
        dst->temp = now_temp + offsets[i];
        r->hourly_count++;
    }
}

/* ------------------------------------------------------------------ */
/* vtable instance                                                     */
/* ------------------------------------------------------------------ */

EXT_RAM_BSS_ATTR weather_detail_page_t s_weather_detail_instance;

const page_renderer_ops_t weather_detail_page_ops = {
    .init = weather_detail_page_init,
    .enter = weather_detail_page_enter,
    .render = weather_detail_page_render,
    .handle_input = weather_detail_page_handle_input,
    .get_dirty_rect = NULL,
    .needs_full_refresh = NULL,
    .mark_full_refresh = NULL,
    .clear_full_refresh_flag = NULL,
    .append_text = NULL,
    .begin_stream = NULL,
    .end_stream = NULL,
};

PAGE_REGISTER(UI_PAGE_WEATHER_DETAIL, "天气详情", NULL, false, 999, &weather_detail_page_ops,
              &s_weather_detail_instance.base);
