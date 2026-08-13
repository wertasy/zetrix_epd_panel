#include "../include/rawdraw_util.h"
#include "weather_card.h"
#include "../include/theme.h"
#include "../include/layout.h"
#include "../include/rawdraw_ext.h"
#include <string.h>
#include <stdio.h>

widget_weather_icon_t widget_weather_card_parse_icon(const char *text)
{
    if (!text || !text[0])
        return WIDGET_WEATHER_ICON_UNKNOWN;

    // Specific compound conditions first
    if (strstr(text, "晴间多云") != NULL)
        return WIDGET_WEATHER_ICON_CLOUDY;

    // Sunny variants
    if (strstr(text, "晴") != NULL)
        return WIDGET_WEATHER_ICON_SUNNY;

    // Cloudy
    if (strstr(text, "多云") != NULL)
        return WIDGET_WEATHER_ICON_CLOUDY;
    // Overcast
    if (strstr(text, "阴") != NULL)
        return WIDGET_WEATHER_ICON_OVERCAST;

    // Rain (all types)
    if (strstr(text, "雨") != NULL)
        return WIDGET_WEATHER_ICON_RAIN;

    // Snow
    if (strstr(text, "雪") != NULL)
        return WIDGET_WEATHER_ICON_SNOW;

    // Fog/Haze
    if (strstr(text, "雾") != NULL)
        return WIDGET_WEATHER_ICON_FOG;
    if (strstr(text, "霾") != NULL)
        return WIDGET_WEATHER_ICON_FOG;
    if (strstr(text, "沙尘") != NULL)
        return WIDGET_WEATHER_ICON_FOG;

    return WIDGET_WEATHER_ICON_UNKNOWN;
}

static void draw_weather_icon_char(uint8_t *fb, int width, int height, int x, int y, widget_weather_icon_t icon,
                                   const lv_font_t *icon_font)
{
    const char *icon_code;
    switch (icon) {
    case WIDGET_WEATHER_ICON_SUNNY:
        icon_code = "\xef\x83\x9e"; // U+F0DE (sun)
        break;
    case WIDGET_WEATHER_ICON_CLOUDY:
        icon_code = "\xef\x82\x82"; // U+F082 (cloud)
        break;
    case WIDGET_WEATHER_ICON_OVERCAST:
        icon_code = "\xef\x83\x82"; // U+F0C2 (cloud overcast)
        break;
    case WIDGET_WEATHER_ICON_RAIN:
        icon_code = "\xef\x83\xa9"; // U+F0E9 (rain)
        break;
    case WIDGET_WEATHER_ICON_SNOW:
        icon_code = "\xef\x8b\x9c"; // U+F2DC (snowflake)
        break;
    case WIDGET_WEATHER_ICON_FOG:
        icon_code = "\xef\x9a\x9f"; // U+F69F (smog/fog)
        break;
    default:
        icon_code = "\xef\x83\x9e"; // Default: sun
        break;
    }
    rawdraw_color_t accent = rawdraw_theme_color_for(THEME_TOKEN_ACCENT);
    rawdraw_draw_text(fb, width, height, x, y, icon_code, icon_font, (int)accent);
}

void widget_weather_card_init(widget_weather_card_t *card, int x, int y, int w)
{
    if (!card)
        return;
    card->x = x;
    card->y = y;
    card->w = w;
    card->city_name[0] = '\0';
    card->has_data = false;
    card->temp_font = &weather_icons_48;
    card->info_font = &SourceHanSansSC_Regular_slim;
    card->icon_font = &weather_icons_48;
    refresh_tracker_init(&card->refresh);
    memset(&card->data, 0, sizeof(card->data));
}

void widget_weather_card_set_position(widget_weather_card_t *card, int x, int y)
{
    if (!card)
        return;
    card->x = x;
    card->y = y;
}

void widget_weather_card_set_width(widget_weather_card_t *card, int w)
{
    if (!card)
        return;
    card->w = w;
}

void widget_weather_card_set_data(widget_weather_card_t *card, const widget_weather_data_t *data)
{
    if (!card || !data)
        return;
    card->data = *data;
    card->has_data = true;
    refresh_mark_dirty(&card->refresh);
}

void widget_weather_card_set_city_name(widget_weather_card_t *card, const char *name)
{
    if (!card)
        return;
    if (name) {
        strncpy(card->city_name, name, sizeof(card->city_name) - 1);
        card->city_name[sizeof(card->city_name) - 1] = '\0';
    } else {
        card->city_name[0] = '\0';
    }
}

rawdraw_rect_t widget_weather_card_get_bounds(const widget_weather_card_t *card)
{
    if (!card)
        return (rawdraw_rect_t){0, 0, 0, 0};
    return (rawdraw_rect_t){card->x, card->y, card->w, WIDGET_WEATHER_CARD_MAX_HEIGHT};
}

region_refresh_t *widget_weather_card_get_refresh_tracker(widget_weather_card_t *card)
{
    if (!card)
        return NULL;
    return &card->refresh;
}

bool widget_weather_card_render(widget_weather_card_t *card, uint8_t *fb, int width, int height)
{
    if (!card || !fb || !card->has_data)
        return false;

    rawdraw_rect_t base_rect = {card->x, card->y, card->w, WIDGET_WEATHER_CARD_MAX_HEIGHT};
    rawdraw_rect_t bounds = rawdraw_align_x8(base_rect);
    bounds = rawdraw_clamp_rect(bounds, width, height);
    if (rawdraw_rect_area(bounds) <= 0)
        return false;

    rawdraw_paint_style_t card_style = rawdraw_theme_component(ROLE_CARD_ELEVATED);
    rawdraw_paint_style_t badge_style = rawdraw_theme_style(THEME_TOKEN_BADGE);
    rawdraw_paint_style_t chip_style = rawdraw_theme_component(ROLE_CARD_DEFAULT);
    rawdraw_color_t text_color = rawdraw_theme_color_for(THEME_TOKEN_TEXT_PRIMARY);
    rawdraw_color_t secondary_color = rawdraw_theme_color_for(THEME_TOKEN_TEXT_SECONDARY);
    rawdraw_color_t border_color = rawdraw_theme_color_for(THEME_TOKEN_BORDER);

    rawdraw_paint_style_t bg_style = rawdraw_theme_style(THEME_TOKEN_BACKGROUND_PRIMARY);
    rawdraw_draw_styled_rect(fb, width, height, bounds, &bg_style);

    rawdraw_rect_t shadow_rect = {bounds.x + 2, bounds.y + 2, bounds.w, bounds.h};
    rawdraw_paint_style_t shadow_style = rawdraw_theme_style(THEME_TOKEN_SHADOW);
    rawdraw_draw_styled_round_rect(fb, width, height, shadow_rect, STYLE_CARD_RADIUS, &shadow_style);

    rawdraw_draw_styled_round_rect(fb, width, height, bounds, STYLE_BORDER_RADIUS_MD, &card_style);

    int card_left = bounds.x + 14;
    int card_top = bounds.y + 12;
    int card_right = bounds.x + bounds.w - 14;

    // Header tag
    const char *city = (card->city_name[0] != '\0') ? card->city_name : card->data.city;
    if (city[0] == '\0')
        city = "天气";
    int city_tag_w = rawdraw_measure_text_width(city, card->info_font) + 18;
    rawdraw_rect_t city_rect = {card_left, card_top, city_tag_w, 16};
    rawdraw_draw_styled_round_rect(fb, width, height, city_rect, STYLE_BORDER_RADIUS_PILL, &badge_style);
    rawdraw_draw_styled_text(fb, width, height, card_left + 9, card_top + 1, city, card->info_font, &badge_style);

    if (card->data.update_time[0] != '\0') {
        char update_buf[64];
        snprintf(update_buf, sizeof(update_buf), "%s 更新", card->data.update_time);
        int update_w = rawdraw_measure_text_width(update_buf, card->info_font);
        rawdraw_draw_text(fb, width, height, card_right - update_w, card_top + 1, update_buf, card->info_font,
                          (int)secondary_color);
    }

    int body_y = card_top + 26;
    widget_weather_icon_t icon = widget_weather_card_parse_icon(card->data.weather_text);
    int icon_x = card_left;
    int icon_y = body_y + 8;
    draw_weather_icon_char(fb, width, height, icon_x, icon_y, icon, card->icon_font);

    int col2_x = icon_x + 58;
    if (card->data.temp[0] != '\0') {
        char temp_display[32];
        snprintf(temp_display, sizeof(temp_display), "%s°C", card->data.temp);
        rawdraw_draw_text(fb, width, height, col2_x, body_y + 2, temp_display, card->info_font, (int)text_color);
    }

    int desc_y = body_y + 22;
    if (card->data.weather_text[0] != '\0') {
        rawdraw_draw_text(fb, width, height, col2_x, desc_y, card->data.weather_text, card->info_font, (int)text_color);
    }
    if (card->data.feels_like[0] != '\0') {
        char feels_buf[64];
        snprintf(feels_buf, sizeof(feels_buf), "体感 %s°C", card->data.feels_like);
        rawdraw_draw_text(fb, width, height, col2_x + 60, desc_y, feels_buf, card->info_font, (int)secondary_color);
    }

    int stats_y = body_y + 46;
    int chip_x = col2_x;

    if (card->data.wind_dir[0] != '\0') {
        char wind_buf[128];
        if (card->data.wind_scale[0] != '\0') {
            snprintf(wind_buf, sizeof(wind_buf), "%s %s级", card->data.wind_dir, card->data.wind_scale);
        } else {
            snprintf(wind_buf, sizeof(wind_buf), "%s", card->data.wind_dir);
        }
        int chip_w = rawdraw_measure_text_width(wind_buf, card->info_font) + 14;
        rawdraw_rect_t chip_rect = {chip_x, stats_y, chip_w, 18};
        rawdraw_draw_styled_round_rect(fb, width, height, chip_rect, STYLE_BORDER_RADIUS_PILL, &chip_style);
        rawdraw_draw_styled_text(fb, width, height, chip_x + 7, stats_y + 2, wind_buf, card->info_font, &chip_style);
        chip_x += chip_w + 6;
    }

    if (card->data.humidity[0] != '\0') {
        char hum_buf[64];
        snprintf(hum_buf, sizeof(hum_buf), "湿度 %s%%", card->data.humidity);
        int chip_w = rawdraw_measure_text_width(hum_buf, card->info_font) + 14;
        rawdraw_rect_t chip_rect = {chip_x, stats_y, chip_w, 18};
        rawdraw_draw_styled_round_rect(fb, width, height, chip_rect, STYLE_BORDER_RADIUS_PILL, &chip_style);
        rawdraw_draw_styled_text(fb, width, height, chip_x + 7, stats_y + 2, hum_buf, card->info_font, &chip_style);
    }

    // Bottom summary strip
    int strip_y = bounds.y + bounds.h - 28;
    rawdraw_draw_hline(fb, width, height, strip_y - 6, bounds.x + 12, bounds.x + bounds.w - 12, border_color);

    if (card->data.weather_text[0] != '\0') {
        rawdraw_draw_text(fb, width, height, card_left, strip_y, card->data.weather_text, card->info_font,
                          (int)text_color);
    }

    if (card->data.temp[0] != '\0') {
        char footer_buf[64];
        snprintf(footer_buf, sizeof(footer_buf), "当前 %s°C", card->data.temp);
        int footer_w = rawdraw_measure_text_width(footer_buf, card->info_font);
        rawdraw_draw_text(fb, width, height, card_right - footer_w, strip_y, footer_buf, card->info_font,
                          (int)secondary_color);
    }

    refresh_update_counter(&card->refresh, esp_timer_get_time());

    return true;
}
