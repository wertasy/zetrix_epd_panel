/**
 * @file weather_card.h
 * @brief Weather card widget for 1bpp ePaper display.
 */
#ifndef WIDGETS_WEATHER_CARD_H_
#define WIDGETS_WEATHER_CARD_H_

#include <stdint.h>
#include <stdbool.h>
#include "../include/rawdraw.h"
#include "../include/font_engine.h"
#include "../include/style.h"
#include "../include/refresh.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIDGET_WEATHER_CARD_MAX_WIDTH 400
#define WIDGET_WEATHER_CARD_MAX_HEIGHT 132
#define WIDGET_WEATHER_CARD_TEMP_SIZE 48
#define WIDGET_WEATHER_CARD_ICON_SIZE 48

typedef enum {
    WIDGET_WEATHER_ICON_SUNNY,
    WIDGET_WEATHER_ICON_CLOUDY,
    WIDGET_WEATHER_ICON_OVERCAST,
    WIDGET_WEATHER_ICON_RAIN,
    WIDGET_WEATHER_ICON_SNOW,
    WIDGET_WEATHER_ICON_FOG,
    WIDGET_WEATHER_ICON_UNKNOWN
} widget_weather_icon_t;

typedef struct {
    char    city[32];
    char    temp[16];
    char    feels_like[16];
    char    weather_icon[16];
    char    weather_text[32];
    char    wind_dir[32];
    char    wind_scale[16];
    char    humidity[16];
    char    update_time[32];
    char    air_quality[16];
    int32_t air_aqi;
    int32_t temp_int;
} widget_weather_data_t;

typedef struct {
    int x;
    int y;
    int w;

    widget_weather_data_t data;
    char                  city_name[32];
    bool                  has_data;

    region_refresh_t refresh;

    const lv_font_t *temp_font;
    const lv_font_t *info_font;
    const lv_font_t *icon_font;
} widget_weather_card_t;

/* ---- lifecycle ---- */
void widget_weather_card_init(widget_weather_card_t *card, int x, int y, int w);

/* ---- configuration ---- */
void widget_weather_card_set_position(widget_weather_card_t *card, int x, int y);
void widget_weather_card_set_width(widget_weather_card_t *card, int w);
void widget_weather_card_set_data(widget_weather_card_t *card, const widget_weather_data_t *data);
void widget_weather_card_set_city_name(widget_weather_card_t *card, const char *name);

/* ---- geometry / state ---- */
rawdraw_rect_t    widget_weather_card_get_bounds(const widget_weather_card_t *card);
region_refresh_t *widget_weather_card_get_refresh_tracker(widget_weather_card_t *card);

/* ---- rendering ---- */
bool widget_weather_card_render(widget_weather_card_t *card, uint8_t *fb, int width, int height);

/* ---- utility ---- */
widget_weather_icon_t widget_weather_card_parse_icon(const char *weather_text);

#ifdef __cplusplus
}
#endif

#endif /* WIDGETS_WEATHER_CARD_H_ */
