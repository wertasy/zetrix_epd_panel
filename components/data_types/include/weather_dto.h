/**
 * @file weather_dto.h
 * @brief QWeather DTO types — shared type-only layer.
 *
 * Pure data types used by both the network API client (weather_api) and the
 * UI page renderers. No functions, no implementation.
 */
#ifndef DATA_TYPES_WEATHER_DTO_H
#define DATA_TYPES_WEATHER_DTO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Data model                                                          */
/* ------------------------------------------------------------------ */

#define WEATHER_STR_LEN 48 /**< long enough for CJK weather text / city  */
#define WEATHER_ICON_LEN 8 /**< QWeather icon code, e.g. "100"            */
#define WEATHER_TIME_LEN 16 /**< update time "HH:MM"                       */
#define WEATHER_MAX_FORECAST 7 /**< forecast slots (v1 daily fills up to 7)   */

typedef struct {
    char label[16]; /**< 今天 / 明天 / 后天              */
    char weather_text[WEATHER_STR_LEN]; /**< 晴 / 多云 / 小雨                 */
    char icon_code[WEATHER_ICON_LEN]; /**< QWeather icon code string       */
    int32_t temp_min;
    int32_t temp_max;
} weather_forecast_day_t;

typedef struct {
    char city[WEATHER_STR_LEN];
    char city_name[32]; /**< IP-geolocation city (e.g. "新城") */
    char temp[WEATHER_STR_LEN]; /**< current temperature string      */
    char feels_like[WEATHER_STR_LEN];
    char weather_icon[WEATHER_ICON_LEN];
    char weather_text[WEATHER_STR_LEN];
    char wind_dir[WEATHER_STR_LEN];
    char wind_scale[WEATHER_ICON_LEN];
    char humidity[WEATHER_ICON_LEN];
    char update_time[WEATHER_TIME_LEN]; /**< "HH:MM"                         */
    char air_quality[WEATHER_STR_LEN]; /**< "优" / "良" ...                  */
    int32_t air_aqi; /**< AQI number, -1 if unknown        */
    int32_t temp_int; /**< numeric temperature             */
    int32_t uv_index; /**< UV index, -1 if unknown          */
    int forecast_count;
    weather_forecast_day_t forecast[WEATHER_MAX_FORECAST];
} weather_data_t;

/** Weather icon classification for rendering. */
typedef enum {
    WEATHER_ICON_UNKNOWN = 0,
    WEATHER_ICON_SUNNY,
    WEATHER_ICON_CLOUDY,
    WEATHER_ICON_OVERCAST,
    WEATHER_ICON_RAIN,
    WEATHER_ICON_SNOW,
    WEATHER_ICON_FOG,
} weather_icon_t;

#ifdef __cplusplus
}
#endif

#endif /* DATA_TYPES_WEATHER_DTO_H */
