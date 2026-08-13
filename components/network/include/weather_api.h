/**
 * @file weather_api.h
 * @brief QWeather (和风天气) v1 API client — C port.
 *
 * Fetches real-time weather, 7-day forecast and air quality via the v1
 * REST API on the ESP-IDF target (esp_http_client). The API key is sent
 * in the X-QW-Api-Key header (not the URL). JSON parsing is performed with
 * the official cJSON library and is fully unit-testable on a Linux host.
 *
 * v1 endpoints (host configurable via Kconfig QWEATHER_API_HOST):
 *   current:  https://{host}/weather/v1/current/{lat}/{lon}?localTime=false&lang=zh
 *   daily:    https://{host}/weather/v1/daily/{lat}/{lon}?days=7&localTime=false&lang=zh
 *   air:      https://{host}/airquality/v1/current/{lat}/{lon}?lang=zh
 *
 * Location can be set explicitly (lat,lon) or auto-detected via IP
 * geolocation (weather_api_detect_location).
 *
 * Usage:
 *   weather_api_init("KEY", "34.16,108.95", my_cb, ctx);
 *   weather_api_fetch();              // target: HTTP fetch; host: no-op
 *   const weather_data_t *d = weather_api_get_cached();
 */
#ifndef WEATHER_API_H
#define WEATHER_API_H

#include <stdint.h>
#include <stdbool.h>
#include "weather_dto.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Callback                                                            */
/* ------------------------------------------------------------------ */

/** Called on the target when a fetch completes successfully. */
typedef void (*weather_callback_t)(const weather_data_t *data, void *user_data);

/* ------------------------------------------------------------------ */
/* Lifecycle / fetch                                                   */
/* ------------------------------------------------------------------ */

/**
 * @brief Initialise the weather API client.
 *
 * On the target this also arms an hourly auto-refresh esp_timer.
 *
 * @param api_key    QWeather API key (copied internally).
 * @param location   Latitude,longitude string, e.g. "34.16,108.95".
 * @param callback   Called with fresh data (may be NULL).
 * @param user_data  Opaque pointer passed back to @p callback.
 */
void weather_api_init(const char *api_key, const char *location, weather_callback_t callback, void *user_data);

/** Trigger a manual fetch. On the host this is a no-op returning false. */
bool weather_api_fetch(void);

/** Alias kept for the original C++ API name. */
bool weather_api_fetch_now(void);

void weather_api_set_location(const char *location);
void weather_api_set_key(const char *api_key);
const char *weather_api_get_location(void);
bool weather_api_is_ready(void);

/**
 * @brief Auto-detect geographic location via IP geolocation.
 *
 * Queries http://ip-api.com/json/ and caches lat/lon + city to NVS.
 * On the host this is a no-op returning false.
 * @return true on success.
 */
bool weather_api_detect_location(void);

/** Clear cached location and re-run IP geolocation on the next fetch. */
void weather_api_redetect_location(void);

/** City name from the last successful IP geolocation (or "" if unknown). */
const char *weather_api_get_city_name(void);

/** Pointer to the most recently fetched data (offline cache). */
const weather_data_t *weather_api_get_cached(void);

/** Alias kept for the original C++ API name. */
const weather_data_t *weather_api_get_last_data(void);

/* ------------------------------------------------------------------ */
/* JSON parsing (host-testable)                                        */
/* ------------------------------------------------------------------ */

/** Map weather condition text to an icon classification. */
weather_icon_t weather_api_parse_weather_icon(const char *text);

/** Parse a v1 /weather/current response into @p out. */
bool weather_api_parse_now_json(const char *json, weather_data_t *out);

/** Parse a v1 /weather/daily response into @p out (replaces forecast list). */
bool weather_api_parse_forecast_json(const char *json, weather_data_t *out);

/** Parse a v1 /airquality/current response into @p out. */
bool weather_api_parse_air_json(const char *json, weather_data_t *out);

/**
 * @brief Auto-detect and parse any QWeather v1 JSON payload.
 *
 * Selection: a "days" array → forecast; an "indexes" array → air;
 * otherwise a "condition" or "temperature" object → current weather.
 */
bool weather_api_parse_json(const char *json, weather_data_t *out);

#ifdef __cplusplus
}
#endif

#endif /* WEATHER_API_H */
