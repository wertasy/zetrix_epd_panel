/**
 * @file weather_renderer.c
 * @brief Weather page renderer — C port of C++ rawdraw::WeatherRenderer.
 */
#include "weather_page.h"
#include "page_registry.h"
#include "data_refresh.h"

#include "rawdraw_ext.h"
#include "theme.h"
#include "style.h"
#include "layout.h"
#include "ui_text_util.h"
#include "weather_icons.h"

#include <stdio.h>
#include <string.h>

static const lv_font_t *const weather_font = &SourceHanSansSC_Regular_slim;
static const lv_font_t *const weather_title_font = &SourceHanSansSC_Medium_slim;

/* ------------------------------------------------------------------ */
/* Forecaster helper                                                   */
/* ------------------------------------------------------------------ */

typedef struct {
    char label[16];
    char weather_text[WEATHER_STR_LEN];
    char icon_code[WEATHER_ICON_LEN];
    int32_t temp_min;
    int32_t temp_max;
} forecast_render_item_t;

static int build_forecast_items(const weather_data_t *data, forecast_render_item_t *items, int max_items)
{
    if (!data)
        return 0;
    int count = 0;
    for (int i = 0; i < data->forecast_count && count < max_items; ++i) {
        const weather_forecast_day_t *src = &data->forecast[i];

        strncpy(items[count].label, src->label, sizeof(items[count].label) - 1);
        items[count].label[sizeof(items[count].label) - 1] = '\0';
        strncpy(items[count].weather_text, src->weather_text, sizeof(items[count].weather_text) - 1);
        items[count].weather_text[sizeof(items[count].weather_text) - 1] = '\0';
        strncpy(items[count].icon_code, src->icon_code, sizeof(items[count].icon_code) - 1);
        items[count].icon_code[sizeof(items[count].icon_code) - 1] = '\0';
        items[count].temp_min = src->temp_min;
        items[count].temp_max = src->temp_max;

        ++count;
    }
    return count;
}

/* ------------------------------------------------------------------ */
/* PageRenderer vtable                                                 */
/* ------------------------------------------------------------------ */

void weather_page_init(page_renderer_t *self, int width, int height)
{
    weather_page_t *r = (weather_page_t *)self;
    r->base.width = width;
    r->base.height = height;
    r->base.needs_full_refresh_flag = true;
    r->font = weather_font;
    r->title_font = weather_title_font;
    r->has_data = false;
    r->page_index = 0;
    r->city_name[0] = '\0';
    r->firmware_version[0] = '\0';
    memset(&r->current_data, 0, sizeof(r->current_data));
}

/* Page gained focus: request a redraw but keep previously fetched data. */
static void weather_page_enter(page_renderer_t *self)
{
    weather_page_t *r = (weather_page_t *)self;
    if (!r)
        return;
    r->base.needs_full_refresh_flag = true;
}

void weather_page_render(page_renderer_t *self, uint8_t *fb, int width, int height)
{
    weather_page_t *r = (weather_page_t *)self;
    if (!fb)
        return;

    const rawdraw_paint_style_t bg_style = rawdraw_theme_style(THEME_TOKEN_BACKGROUND_PRIMARY);
    const rawdraw_paint_style_t selected_style = rawdraw_theme_style(THEME_TOKEN_SELECTED);
    const rawdraw_paint_style_t card_style = rawdraw_theme_component(ROLE_CARD_DEFAULT);
    const rawdraw_paint_style_t panel_style = rawdraw_theme_component(ROLE_PANEL);
    const rawdraw_color_t text = rawdraw_theme_color_for(THEME_TOKEN_TEXT_PRIMARY);
    const rawdraw_color_t secondary = rawdraw_theme_color_for(THEME_TOKEN_TEXT_SECONDARY);
    const rawdraw_color_t border = rawdraw_theme_color_for(THEME_TOKEN_BORDER);
    const rawdraw_color_t accent = rawdraw_theme_color_for(THEME_TOKEN_ACCENT);

    const int content_top = STYLE_STATUS_BAR_HEIGHT + 2;
    rawdraw_draw_styled_rect(fb, width, height,
                             (rawdraw_rect_t){0, STYLE_STATUS_BAR_HEIGHT, width, height - STYLE_STATUS_BAR_HEIGHT},
                             &bg_style);

    if (!r->has_data) {
        const char *empty_text = "暂无天气数据";
        const char *hint = "长按刷新";
        int text_w = rawdraw_measure_text_width(empty_text, r->font);
        int hint_w = rawdraw_measure_text_width(hint, r->font);
        int center_y = content_top + (height - content_top) / 2;
        const int empty_baseline = rawdraw_layout_calc_baseline_y(r->font, center_y - 10, STYLE_VISUAL_TEXT_OFFSET);
        const int hint_baseline = rawdraw_layout_calc_baseline_y(r->font, center_y + 16, STYLE_VISUAL_TEXT_OFFSET);
        rawdraw_draw_text(fb, width, height, (width - text_w) / 2,
                          rawdraw_layout_top_y_from_baseline(r->font, empty_baseline), empty_text, r->font, text);
        rawdraw_draw_text(fb, width, height, (width - hint_w) / 2,
                          rawdraw_layout_top_y_from_baseline(r->font, hint_baseline), hint, r->font, secondary);
    } else {
        char location[WEATHER_STR_LEN];
        if (r->city_name[0] != '\0') {
            strncpy(location, r->city_name, sizeof(location) - 1);
            location[sizeof(location) - 1] = '\0';
        } else if (r->current_data.city[0] != '\0') {
            strncpy(location, r->current_data.city, sizeof(location) - 1);
            location[sizeof(location) - 1] = '\0';
        } else {
            strcpy(location, "西安");
        }
        char location_line[WEATHER_STR_LEN];
        ui_text_fit_to_width(location, r->title_font, 180, location_line, sizeof(location_line));

        /* Top summary: three equal-height blocks on one visual baseline. */
        const int summary_y = STYLE_STATUS_BAR_HEIGHT + 8;
        const int summary_h = 68;
        const rawdraw_rect_t location_box = {24, summary_y, 92, summary_h};
        const rawdraw_rect_t temp_box = {136, summary_y, 112, summary_h};
        const rawdraw_rect_t aqi_box = {276, summary_y, 92, summary_h};

        rawdraw_draw_styled_round_rect(fb, width, height, location_box, STYLE_BORDER_RADIUS_MD, &selected_style);
        const int pin_cx = location_box.x + location_box.w / 2;
        const int pin_cy = location_box.y + 17;
        rawdraw_draw_circle_border(fb, width, height, (rawdraw_point_t){pin_cx, pin_cy}, 5, 1, selected_style.fg);
        rawdraw_draw_line(fb, width, height, (rawdraw_point_t){pin_cx, pin_cy + 5},
                          (rawdraw_point_t){pin_cx - 4, pin_cy + 13}, selected_style.fg);
        rawdraw_draw_line(fb, width, height, (rawdraw_point_t){pin_cx, pin_cy + 5},
                          (rawdraw_point_t){pin_cx + 4, pin_cy + 13}, selected_style.fg);
        const int loc_w = rawdraw_measure_text_width(location_line, r->title_font);
        rawdraw_draw_text(
            fb, width, height, location_box.x + (location_box.w - loc_w) / 2,
            rawdraw_layout_ink_centered_text_top_y_in_box(r->title_font, location_line, location_box.y + 30, 28, 0),
            location_line, r->title_font, selected_style.fg);

        rawdraw_draw_styled_round_rect(fb, width, height, temp_box, STYLE_BORDER_RADIUS_MD, &card_style);
        char temp_buf[24];
        const char *temp_src = r->current_data.temp[0] != '\0' ? r->current_data.temp : "--";
        snprintf(temp_buf, sizeof(temp_buf), "%.8s°C", temp_src);
        const int temp_w = rawdraw_measure_text_width(temp_buf, r->title_font);
        const int temp_x = temp_box.x + (temp_box.w - temp_w) / 2;
        const int temp_y = rawdraw_layout_ink_centered_text_top_y(r->title_font, temp_buf, temp_box.y + 24, 0);
        rawdraw_draw_text(fb, width, height, temp_x, temp_y, temp_buf, r->title_font, text);
        rawdraw_draw_hline(fb, width, height, temp_y + r->title_font->line_height, temp_x, temp_x + temp_w, accent);

        char feels_buf[24];
        const char *feels_src = r->current_data.feels_like[0] != '\0'
                                    ? r->current_data.feels_like
                                    : (r->current_data.temp[0] != '\0' ? r->current_data.temp : "--");
        snprintf(feels_buf, sizeof(feels_buf), "体感 %.8s°C", feels_src);
        const int feels_w = rawdraw_measure_text_width(feels_buf, r->font);
        rawdraw_draw_text(fb, width, height, temp_box.x + (temp_box.w - feels_w) / 2,
                          rawdraw_layout_ink_centered_text_top_y(r->font, feels_buf, temp_box.y + 52, 0), feels_buf,
                          r->font, secondary);

        rawdraw_draw_styled_round_rect(fb, width, height, aqi_box, STYLE_BORDER_RADIUS_MD, &card_style);
        rawdraw_draw_text(fb, width, height, aqi_box.x + 16,
                          rawdraw_layout_ink_centered_text_top_y(r->font, "空气质量", aqi_box.y + 17, 0), "空气质量",
                          r->font, secondary);
        char aqi_buf[40];
        if (r->current_data.air_aqi >= 0) {
            snprintf(aqi_buf, sizeof(aqi_buf), "%d", (int)r->current_data.air_aqi);
        } else {
            snprintf(aqi_buf, sizeof(aqi_buf), "--");
        }
        const int aqi_w = rawdraw_measure_text_width(aqi_buf, r->title_font);
        rawdraw_draw_text(fb, width, height, aqi_box.x + (aqi_box.w - aqi_w) / 2,
                          rawdraw_layout_ink_centered_text_top_y(r->title_font, aqi_buf, aqi_box.y + 42, 0), aqi_buf,
                          r->title_font, text);
        const char *air = r->current_data.air_quality[0] != '\0' ? r->current_data.air_quality : "优";
        const int air_w = rawdraw_measure_text_width(air, r->font);
        rawdraw_draw_text(fb, width, height, aqi_box.x + (aqi_box.w - air_w) / 2,
                          rawdraw_layout_ink_centered_text_top_y(r->font, air, aqi_box.y + 58, 0), air, r->font,
                          secondary);

        /* Weather condition stack: icon above text. */
        char desc_buf[WEATHER_STR_LEN];
        const char *weather_src = r->current_data.weather_text[0] != '\0' ? r->current_data.weather_text : "天气 --";
        ui_text_fit_to_width(weather_src, r->font, 80, desc_buf, sizeof(desc_buf));
        const char *desc_glyph =
            ui_text_icon_glyph_for_code(r->current_data.weather_icon, r->current_data.weather_text);
        const int condition_center_x = 70;
        const int desc_icon_w = rawdraw_measure_text_width(desc_glyph, &weather_icons_16);
        rawdraw_draw_text(
            fb, width, height, condition_center_x - desc_icon_w / 2,
            rawdraw_layout_ink_centered_text_top_y(&weather_icons_16, desc_glyph, summary_y + summary_h + 16, 0),
            desc_glyph, &weather_icons_16, accent);
        const int desc_w = rawdraw_measure_text_width(desc_buf, r->font);
        rawdraw_draw_text(fb, width, height, condition_center_x - desc_w / 2,
                          rawdraw_layout_ink_centered_text_top_y(r->font, desc_buf, summary_y + summary_h + 35, 0),
                          desc_buf, r->font, text);

        const int metrics_y = 156;
        const char *labels[] = {"湿度", "风向", "风力", "紫外线"};
        char values[4][16];
        snprintf(values[0], sizeof(values[0]), "%.7s%%",
                 r->current_data.humidity[0] != '\0' ? r->current_data.humidity : "--");
        snprintf(values[1], sizeof(values[1]), "%.14s",
                 r->current_data.wind_dir[0] != '\0' ? r->current_data.wind_dir : "--");
        snprintf(values[2], sizeof(values[2]), "%.6s级",
                 r->current_data.wind_scale[0] != '\0' ? r->current_data.wind_scale : "--");
        {
            /* UV index mapping: >=8 强, >=3 中, >0 弱, else -- */
            const char *uv;
            if (r->current_data.uv_index >= 8)
                uv = "\xe5\xbc\xba"; /* 强 */
            else if (r->current_data.uv_index >= 3)
                uv = "\xe4\xb8\xad"; /* 中 */
            else if (r->current_data.uv_index > 0)
                uv = "\xe5\xbc\xb1"; /* 弱 */
            else
                uv = "--";
            strcpy(values[3], uv);
        }
        const int metric_x[] = {42, 128, 224, 318};
        for (int i = 0; i < 4; ++i) {
            rawdraw_draw_text(fb, width, height, metric_x[i],
                              rawdraw_layout_ink_centered_text_top_y(r->font, labels[i], metrics_y + 8, 0), labels[i],
                              r->font, secondary);
            rawdraw_draw_text(fb, width, height, metric_x[i],
                              rawdraw_layout_ink_centered_text_top_y(r->font, values[i], metrics_y + 32, 0), values[i],
                              r->font, text);
        }

        forecast_render_item_t forecast_items[4];
        const int forecast_count = build_forecast_items(&r->current_data, forecast_items, 4);
        if (r->page_index >= forecast_count && forecast_count > 0) {
            r->page_index = forecast_count - 1;
        }

        const rawdraw_rect_t forecast_panel = {28, 214, width - 56, 62};
        rawdraw_draw_styled_round_rect(fb, width, height, forecast_panel, STYLE_BORDER_RADIUS_MD, &panel_style);
        const int card_w = forecast_panel.w / 4;
        for (int i = 0; i < forecast_count && i < 4; ++i) {
            const forecast_render_item_t *item = &forecast_items[i];
            const int x = forecast_panel.x + i * card_w;
            if (i > 0) {
                for (int y = forecast_panel.y + 8; y < forecast_panel.y + forecast_panel.h - 8; y += 4) {
                    rawdraw_set_pixel(fb, width, height, x, y, border);
                }
            }
            rawdraw_draw_text(fb, width, height, x + 28,
                              rawdraw_layout_ink_centered_text_top_y(r->font, item->label, forecast_panel.y + 12, 0),
                              item->label, r->font, secondary);
            const char *glyph = ui_text_icon_glyph_for_code(item->icon_code, item->weather_text);
            const int icon_center_y = forecast_panel.y + 30;
            rawdraw_draw_text(fb, width, height, x + 34,
                              rawdraw_layout_ink_centered_text_top_y(&weather_icons_16, glyph, icon_center_y, 0), glyph,
                              &weather_icons_16, accent);
            char temp_range[24];
            snprintf(temp_range, sizeof(temp_range), "%d/%d°C", (int)item->temp_min, (int)item->temp_max);
            rawdraw_draw_text(fb, width, height, x + 20,
                              rawdraw_layout_ink_centered_text_top_y(r->font, temp_range, forecast_panel.y + 48, 0),
                              temp_range, r->font, text);
        }
    }

    r->base.needs_full_refresh_flag = false;
}

bool weather_page_handle_input(page_renderer_t *self, const ui_button_event_t *event)
{
    weather_page_t *r = (weather_page_t *)self;
    forecast_render_item_t items[4];
    const int max_cards = build_forecast_items(&r->current_data, items, 4);
    switch (event->type) {
    case BTN_UP_CLICK:
        if (max_cards > 0) {
            r->page_index = RD_MAX(0, r->page_index - 1);
            r->base.needs_full_refresh_flag = true;
            return true;
        }
        return false;
    case BTN_DOWN_CLICK:
        if (max_cards > 0) {
            r->page_index = RD_MIN(max_cards - 1, r->page_index + 1);
            r->base.needs_full_refresh_flag = true;
            return true;
        }
        return false;
    case BTN_UP_LONG_PRESS:
    case BTN_DOWN_LONG_PRESS:
    case BTN_BOOT_LONG_PRESS:
        data_refresh_request(UI_PAGE_WEATHER);
        r->base.needs_full_refresh_flag = true;
        return true;
    default:
        return false;
    }
}

/* ------------------------------------------------------------------ */
/* Data interface                                                      */
/* ------------------------------------------------------------------ */

void weather_page_update(page_renderer_t *self, const weather_data_t *data)
{
    weather_page_t *r = (weather_page_t *)self;
    if (!data)
        return;
    r->current_data = *data;
    r->has_data = true;
    forecast_render_item_t items[4];
    const int max_cards = build_forecast_items(&r->current_data, items, 4);
    if (r->page_index >= max_cards) {
        r->page_index = max_cards > 0 ? max_cards - 1 : 0;
    }
    r->base.needs_full_refresh_flag = true;
}

void weather_page_set_city_name(page_renderer_t *self, const char *name)
{
    weather_page_t *r = (weather_page_t *)self;
    if (name) {
        strncpy(r->city_name, name, sizeof(r->city_name) - 1);
        r->city_name[sizeof(r->city_name) - 1] = '\0';
    } else {
        r->city_name[0] = '\0';
    }
    r->base.needs_full_refresh_flag = true;
}

void weather_page_set_firmware_version(page_renderer_t *self, const char *version)
{
    weather_page_t *r = (weather_page_t *)self;
    if (version) {
        strncpy(r->firmware_version, version, sizeof(r->firmware_version) - 1);
        r->firmware_version[sizeof(r->firmware_version) - 1] = '\0';
    } else {
        r->firmware_version[0] = '\0';
    }
    r->base.needs_full_refresh_flag = true;
}

/* ------------------------------------------------------------------ */
/* vtable instance                                                     */
/* ------------------------------------------------------------------ */

EXT_RAM_BSS_ATTR weather_page_t s_weather_instance;

const page_renderer_ops_t weather_page_ops = {
    .init = weather_page_init,
    .enter = weather_page_enter,
    .render = weather_page_render,
    .handle_input = weather_page_handle_input,
    .get_dirty_rect = NULL,
    .needs_full_refresh = NULL,
    .mark_full_refresh = NULL,
    .clear_full_refresh_flag = NULL,
    .append_text = NULL,
    .begin_stream = NULL,
    .end_stream = NULL,
};

PAGE_REGISTER(UI_PAGE_WEATHER, "天气", NULL, true, 20, &weather_page_ops, &s_weather_instance.base);
