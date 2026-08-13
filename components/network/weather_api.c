/**
 * @file weather_api.c
 * @brief QWeather v1 API client — C port of weather_api.{h,cc}.
 *
 * v1 API migration: lat/lon path URLs, X-QW-Api-Key header authentication,
 * flat JSON (no code/now wrapper). IP geolocation (ip-api.com) auto-detects
 * location when none is configured. JSON parsing uses cJSON and runs on both
 * the ESP-IDF target and a Linux host. HTTP transport and the hourly
 * auto-refresh timer are target-only.
 */
#include "weather_api.h"
#include "sleep_manager.h"
#include "cJSON.h"
#include "http_client_util.h"
#ifdef ESP_PLATFORM
#    include "miniz.h"
#else
#    include <zlib.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#    include "freertos/FreeRTOS.h"
#    include "freertos/task.h"
#    include "esp_log.h"
#    include "esp_timer.h"
#    include "nvs_state.h"
#    include "nvs_flash.h"
#    include "nvs.h"
static const char *TAG = "WeatherApi";
#    define LOGI(...) ESP_LOGI(TAG, __VA_ARGS__)
#    define LOGW(...) ESP_LOGW(TAG, __VA_ARGS__)
#    define LOGE(...) ESP_LOGE(TAG, __VA_ARGS__)
#    define LOGD(...) ESP_LOGD(TAG, __VA_ARGS__)
#else
#    include <stdio.h>
#    define LOGI(...)                                                                                                  \
        do {                                                                                                           \
            fprintf(stderr, "[WTH][I] " __VA_ARGS__);                                                                  \
            fputc('\n', stderr);                                                                                       \
        } while (0)
#    define LOGW(...)                                                                                                  \
        do {                                                                                                           \
            fprintf(stderr, "[WTH][W] " __VA_ARGS__);                                                                  \
            fputc('\n', stderr);                                                                                       \
        } while (0)
#    define LOGE(...)                                                                                                  \
        do {                                                                                                           \
            fprintf(stderr, "[WTH][E] " __VA_ARGS__);                                                                  \
            fputc('\n', stderr);                                                                                       \
        } while (0)
#    define LOGD(...)                                                                                                  \
        do {                                                                                                           \
            fprintf(stderr, "[WTH][D] " __VA_ARGS__);                                                                  \
            fputc('\n', stderr);                                                                                       \
        } while (0)
#endif

#ifndef CONFIG_QWEATHER_API_HOST
#    define CONFIG_QWEATHER_API_HOST "mg3aarxm84.re.qweatherapi.com"
#endif
#ifndef CONFIG_WEATHER_DEFAULT_LOCATION
#    define CONFIG_WEATHER_DEFAULT_LOCATION "34.16,108.95"
#endif

/* ============================================================ */
/* Static state                                                 */
/* ============================================================ */

static char s_api_key[64] = {0};
static char s_location[32] = {0}; /* "lat,lon" e.g. "34.1600,108.9500" */
static char s_api_host[128] = {0};
static char s_city_name[32] = {0}; /* from IP geolocation */
static bool s_need_auto_detect = false;
static weather_callback_t s_callback = NULL;
static void *s_user_data = NULL;
static bool s_initialized = false;

static weather_data_t s_last_data;

#ifdef ESP_PLATFORM
static bool s_in_progress = false;
static esp_timer_handle_t s_timer = NULL;

static void save_weather_cache(void)
{
    nvs_handle_t h;
    if (nvs_open("weather_cache", NVS_READWRITE, &h) != ESP_OK)
        return;
    nvs_set_blob(h, "data", &s_last_data, sizeof(s_last_data));
    nvs_commit(h);
    nvs_close(h);
}

static bool load_weather_cache(void)
{
    nvs_handle_t h;
    if (nvs_open("weather_cache", NVS_READONLY, &h) != ESP_OK)
        return false;
    size_t len = sizeof(s_last_data);
    esp_err_t err = nvs_get_blob(h, "data", &s_last_data, &len);
    nvs_close(h);
    if (err == ESP_OK && len == sizeof(s_last_data)) {
        LOGI("Loaded weather cache from NVS");
        return true;
    }
    return false;
}
#endif

/* ============================================================ */
/* Icon mapping                                                 */
/* ============================================================ */

weather_icon_t weather_api_parse_weather_icon(const char *text)
{
    if (!text || !text[0])
        return WEATHER_ICON_UNKNOWN;
    if (strstr(text, "\xe6\x99\xb4") != NULL)
        return WEATHER_ICON_SUNNY; /* 晴 */
    if (strstr(text, "\xe5\xa4\x9a\xe4\xba\x91") != NULL)
        return WEATHER_ICON_CLOUDY; /* 多云 */
    if (strstr(text, "\xe9\x98\xb4") != NULL)
        return WEATHER_ICON_OVERCAST; /* 阴 */
    if (strstr(text, "\xe9\x9b\xa8") != NULL)
        return WEATHER_ICON_RAIN; /* 雨 */
    if (strstr(text, "\xe9\x9b\xaa") != NULL)
        return WEATHER_ICON_SNOW; /* 雪 */
    if (strstr(text, "\xe9\x9b\xbe") != NULL)
        return WEATHER_ICON_FOG; /* 雾 */
    if (strstr(text, "\xe9\x9c\xbe") != NULL)
        return WEATHER_ICON_FOG; /* 霾 */
    if (strstr(text, "\xe6\xb2\x99\xe5\xb0\x98") != NULL)
        return WEATHER_ICON_FOG; /* 沙尘 */
    return WEATHER_ICON_UNKNOWN;
}

/* ============================================================ */
/* JSON parsing helpers                                         */
/* ============================================================ */

/* Copy a string member into a fixed buffer (safe truncation). */
static void w_copy_str(cJSON *obj, const char *key, char *dst, size_t dst_size)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(item) && item->valuestring) {
        snprintf(dst, dst_size, "%s", item->valuestring);
    }
    /* leave previous value untouched on miss */
}

/* Integer member that also accepts string-encoded numbers ("25"). */
static int w_get_int(cJSON *obj, const char *key, int def)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!item)
        return def;
    if (cJSON_IsString(item) && item->valuestring)
        return atoi(item->valuestring);
    return (int)cJSON_GetNumberValue(item);
}

/* Double member that also accepts string-encoded numbers. */
static double w_get_double(cJSON *obj, const char *key, double def)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!item)
        return def;
    if (cJSON_IsString(item) && item->valuestring)
        return atof(item->valuestring);
    return cJSON_GetNumberValue(item);
}

/* ============================================================ */
/* v1 JSON parsing                                              */
/* ============================================================ */

bool weather_api_parse_now_json(const char *json, weather_data_t *out)
{
    cJSON *root;
    cJSON *condition;
    cJSON *temperature;
    cJSON *feels;
    cJSON *wind;
    cJSON *humidity;

    if (!json || !out)
        return false;

    root = cJSON_Parse(json);
    if (!root) {
        LOGE("Now API parse error");
        return false;
    }

    /* v1 has no code/now wrapper — the payload is the top-level object.
     * Require at least a condition or temperature object to succeed. */
    condition = cJSON_GetObjectItemCaseSensitive(root, "condition");
    temperature = cJSON_GetObjectItemCaseSensitive(root, "temperature");
    if (!cJSON_IsObject(condition) && !cJSON_IsObject(temperature)) {
        LOGE("Now API: no condition/temperature in v1 response");
        cJSON_Delete(root);
        return false;
    }

    /* condition.text / condition.code */
    if (condition) {
        w_copy_str(condition, "text", out->weather_text, sizeof(out->weather_text));
        w_copy_str(condition, "code", out->weather_icon, sizeof(out->weather_icon));
    }

    /* temperature.value (float) → temp string + temp_int */
    if (temperature) {
        double tv = w_get_double(temperature, "value", 0.0);
        snprintf(out->temp, sizeof(out->temp), "%.0f", tv);
        out->temp_int = (int)tv;
    }

    /* feelsLike.value (float) */
    feels = cJSON_GetObjectItemCaseSensitive(root, "feelsLike");
    if (feels) {
        double fv = w_get_double(feels, "value", 0.0);
        snprintf(out->feels_like, sizeof(out->feels_like), "%.0f", fv);
    }

    /* humidity (0-1 float) → percentage string */
    humidity = cJSON_GetObjectItemCaseSensitive(root, "humidity");
    if (humidity) {
        double hv = w_get_double(root, "humidity", 0.0);
        snprintf(out->humidity, sizeof(out->humidity), "%d", (int)(hv * 100));
    }

    /* wind.direction.compass → wind_dir; wind.scale (int) → wind_scale */
    wind = cJSON_GetObjectItemCaseSensitive(root, "wind");
    if (wind) {
        cJSON *dir = cJSON_GetObjectItemCaseSensitive(wind, "direction");
        if (dir) {
            w_copy_str(dir, "compass", out->wind_dir, sizeof(out->wind_dir));
        }
        {
            int scale = w_get_int(wind, "scale", 0);
            snprintf(out->wind_scale, sizeof(out->wind_scale), "%d", scale);
        }
    }

    /* uvIndex (top-level number) → uv_index */
    out->uv_index = w_get_int(root, "uvIndex", -1);

    cJSON_Delete(root);
    return true;
}

bool weather_api_parse_forecast_json(const char *json, weather_data_t *out)
{
    cJSON *root;
    cJSON *days;
    int count;
    int i;
    static const char *labels[3] = {
        "\xe4\xbb\x8a\xe5\xa4\xa9", /* 今天 */
        "\xe6\x98\x8e\xe5\xa4\xa9", /* 明天 */
        "\xe5\x90\x8e\xe5\xa4\xa9" /* 后天 */
    };
    if (!json || !out)
        return false;

    root = cJSON_Parse(json);
    if (!root)
        return false;

    /* v1 daily: top-level "days" array (not "daily"). */
    days = cJSON_GetObjectItemCaseSensitive(root, "days");
    if (!days) {
        /* backward-compat: also accept legacy "daily" key */
        days = cJSON_GetObjectItemCaseSensitive(root, "daily");
    }
    if (!days) {
        LOGE("No 'days' array in forecast response");
        cJSON_Delete(root);
        return false;
    }

    count = cJSON_IsArray(days) ? cJSON_GetArraySize(days) : 0;
    if (count > WEATHER_MAX_FORECAST)
        count = WEATHER_MAX_FORECAST;
    out->forecast_count = 0;

    for (i = 0; i < count; i++) {
        cJSON *day = cJSON_GetArrayItem(days, i);
        weather_forecast_day_t *item;
        if (!day)
            break;
        item = &out->forecast[out->forecast_count];
        memset(item, 0, sizeof(*item));
        if (i < 3) {
            snprintf(item->label, sizeof(item->label), "%s", labels[i]);
        }
        /* v1: daytime.condition.text / daytime.condition.code */
        {
            cJSON *daytime = cJSON_GetObjectItemCaseSensitive(day, "daytime");
            if (daytime) {
                cJSON *cond = cJSON_GetObjectItemCaseSensitive(daytime, "condition");
                if (cond) {
                    w_copy_str(cond, "text", item->weather_text, sizeof(item->weather_text));
                    w_copy_str(cond, "code", item->icon_code, sizeof(item->icon_code));
                }
            }
        }
        /* v1: temperatureMax.value / temperatureMin.value (floats) */
        {
            cJSON *tmax = cJSON_GetObjectItemCaseSensitive(day, "temperatureMax");
            cJSON *tmin = cJSON_GetObjectItemCaseSensitive(day, "temperatureMin");
            if (tmax)
                item->temp_max = (int)w_get_double(tmax, "value", 0.0);
            if (tmin)
                item->temp_min = (int)w_get_double(tmin, "value", 0.0);
        }
        out->forecast_count++;
    }

    cJSON_Delete(root);
    return out->forecast_count > 0;
}

bool weather_api_parse_air_json(const char *json, weather_data_t *out)
{
    cJSON *root;
    cJSON *indexes;
    int count;
    int i;
    if (!json || !out)
        return false;

    root = cJSON_Parse(json);
    if (!root)
        return false;

    /* v1 air: top-level "indexes" array; find code=="cn-mee" entry. */
    indexes = cJSON_GetObjectItemCaseSensitive(root, "indexes");
    if (!indexes || !cJSON_IsArray(indexes)) {
        cJSON_Delete(root);
        return false;
    }

    count = cJSON_GetArraySize(indexes);
    for (i = 0; i < count; i++) {
        cJSON *entry = cJSON_GetArrayItem(indexes, i);
        cJSON *code;
        if (!entry)
            continue;
        code = cJSON_GetObjectItemCaseSensitive(entry, "code");
        if (cJSON_IsString(code) && code->valuestring && strcmp(code->valuestring, "cn-mee") == 0) {
            out->air_aqi = w_get_int(entry, "aqi", -1);
            w_copy_str(entry, "category", out->air_quality, sizeof(out->air_quality));
            cJSON_Delete(root);
            return true;
        }
    }

    cJSON_Delete(root);
    return false;
}

bool weather_api_parse_json(const char *json, weather_data_t *out)
{
    cJSON *root;
    if (!json || !out)
        return false;

    root = cJSON_Parse(json);
    if (!root) {
        LOGE("Unrecognised weather JSON payload");
        return false;
    }

    /* v1 forecast: "days" or legacy "daily" array. */
    if (cJSON_GetObjectItemCaseSensitive(root, "days") || cJSON_GetObjectItemCaseSensitive(root, "daily")) {
        cJSON_Delete(root);
        return weather_api_parse_forecast_json(json, out);
    }
    /* v1 air: "indexes" array. */
    if (cJSON_GetObjectItemCaseSensitive(root, "indexes")) {
        cJSON_Delete(root);
        return weather_api_parse_air_json(json, out);
    }
    /* v1 current: has "condition" or "temperature" at top level. */
    if (cJSON_GetObjectItemCaseSensitive(root, "condition") || cJSON_GetObjectItemCaseSensitive(root, "temperature")) {
        cJSON_Delete(root);
        return weather_api_parse_now_json(json, out);
    }

    LOGE("Unrecognised weather JSON payload");
    cJSON_Delete(root);
    return false;
}

/* ============================================================ */
/* HTTP client + fetch (target only)                            */
/* ============================================================ */

#ifdef ESP_PLATFORM

#    define WEATHER_HTTP_BUF_SIZE 4096 /* raw compressed response */
#    define WEATHER_JSON_BUF_SIZE 16384 /* decompressed JSON output */

static int parse_gzip_header(const uint8_t *src, size_t src_len)
{
    if (src_len < 10)
        return -1;
    if (src[0] != 0x1f || src[1] != 0x8b)
        return -1;
    if (src[2] != 8)
        return -1;

    uint8_t flags = src[3];
    int offset = 10;

    if (flags & 4) {
        if (offset + 2 > src_len)
            return -1;
        uint16_t xlen = src[offset] | (src[offset + 1] << 8);
        offset += 2 + xlen;
    }

    if (flags & 8) {
        while (offset < src_len && src[offset] != 0) {
            offset++;
        }
        offset++;
    }

    if (flags & 16) {
        while (offset < src_len && src[offset] != 0) {
            offset++;
        }
        offset++;
    }

    if (flags & 2) {
        offset += 2;
    }

    if (offset > src_len)
        return -1;
    return offset;
}

static bool decompress_gzip(const uint8_t *src, size_t src_len, uint8_t *dst, size_t *dst_len)
{
#    ifdef ESP_PLATFORM
    /* tinfl_decompressor is ~10 KB; must be heap-allocated to avoid stack overflow. */
    int offset = parse_gzip_header(src, src_len);
    if (offset < 0 || src_len <= (size_t)(offset + 8)) {
        return false;
    }
    size_t deflate_len = src_len - offset - 8;

    tinfl_decompressor *decomp = malloc(sizeof(tinfl_decompressor));
    if (!decomp) {
        return false;
    }
    tinfl_init(decomp);

    size_t in_avail = deflate_len;
    size_t out_avail = *dst_len;
    tinfl_status status = tinfl_decompress(decomp, (const mz_uint8 *)(src + offset), &in_avail, dst, dst, &out_avail,
                                           TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    free(decomp);

    if (status == TINFL_STATUS_DONE) {
        *dst_len = out_avail;
        return true;
    }
    return false;
#    else
    z_stream stream;
    memset(&stream, 0, sizeof(stream));
    stream.next_in = (Bytef *)src;
    stream.avail_in = src_len;
    stream.next_out = dst;
    stream.avail_out = *dst_len;

    int status = inflateInit2(&stream, 31);
    if (status != Z_OK) {
        return false;
    }
    status = inflate(&stream, Z_FINISH);
    if (status == Z_STREAM_END) {
        *dst_len = stream.total_out;
        inflateEnd(&stream);
        return true;
    }
    inflateEnd(&stream);
    return false;
#    endif
}

static bool weather_http_get_and_decompress(const char *url, char *out_buf, size_t out_buf_size)
{
    uint8_t *raw_resp = malloc(WEATHER_HTTP_BUF_SIZE);
    if (!raw_resp) {
        LOGE("Failed to allocate raw response buffer");
        return false;
    }

    const char *headers[2] = {"X-QW-Api-Key", "accept"};
    const char *values[2] = {s_api_key, "application/json"};
    int n = http_get_with_headers(url, headers, values, 2, raw_resp, WEATHER_HTTP_BUF_SIZE);
    if (n < 0) {
        free(raw_resp);
        return false;
    }

    if (n >= 2 && raw_resp[0] == 0x1f && raw_resp[1] == 0x8b) {
        size_t decompressed_len = out_buf_size - 1;
        if (!decompress_gzip(raw_resp, n, (uint8_t *)out_buf, &decompressed_len)) {
            LOGE("Failed to decompress weather response");
            free(raw_resp);
            return false;
        }
        out_buf[decompressed_len] = '\0';
    } else {
        int copy_len = n < (int)out_buf_size ? n : (int)out_buf_size - 1;
        memcpy(out_buf, raw_resp, copy_len);
        out_buf[copy_len] = '\0';
    }

    free(raw_resp);
    return true;
}

static void do_fetch(void *arg)
{
    weather_data_t *data;
    char url[512];
    char *resp;
    (void)arg;

    if (!s_initialized) {
        s_in_progress = false;
        return;
    }
    if (!s_api_key[0]) {
        LOGE("API key not set");
        s_in_progress = false;
        return;
    }

    /* Lazy IP geolocation: if no location and auto-detect is pending. */
    if (!s_location[0] && s_need_auto_detect) {
        if (!weather_api_detect_location()) {
            LOGW("Auto-detect location failed; will retry next cycle");
            s_in_progress = false;
            return;
        }
        s_need_auto_detect = false;
    }
    if (!s_location[0]) {
        LOGE("Location not set");
        s_in_progress = false;
        return;
    }

    char lat[16] = {0};
    char lon[16] = {0};
    const char *comma = strchr(s_location, ',');
    if (comma) {
        int lat_len = comma - s_location;
        if (lat_len < sizeof(lat)) {
            memcpy(lat, s_location, lat_len);
        }
        snprintf(lon, sizeof(lon), "%s", comma + 1);
    } else {
        LOGE("Invalid location format: %s", s_location);
        s_in_progress = false;
        return;
    }

    data = malloc(sizeof(weather_data_t));
    resp = malloc(WEATHER_JSON_BUF_SIZE);
    if (!data || !resp) {
        LOGE("Failed to allocate memory for weather fetch");
        free(data);
        free(resp);
        s_in_progress = false;
        return;
    }

    memset(data, 0, sizeof(*data));
    data->uv_index = -1;
    data->air_aqi = -1;
    snprintf(data->city_name, sizeof(data->city_name), "%s", s_city_name);

    /* v1 current */
    snprintf(url, sizeof(url), "https://%s/weather/v1/current/%s/%s?localTime=false&lang=zh", s_api_host, lat, lon);
    LOGI("Fetching current weather");
    if (!weather_http_get_and_decompress(url, resp, WEATHER_JSON_BUF_SIZE) || !weather_api_parse_now_json(resp, data)) {
        LOGE("Failed to fetch or parse current weather");
        s_in_progress = false;
        free(data);
        free(resp);
        return;
    }

    /* v1 daily (7-day) */
    snprintf(url, sizeof(url), "https://%s/weather/v1/daily/%s/%s?days=7&localTime=false&lang=zh", s_api_host, lat,
             lon);
    LOGI("Fetching forecast");
    if (weather_http_get_and_decompress(url, resp, WEATHER_JSON_BUF_SIZE)) {
        if (!weather_api_parse_forecast_json(resp, data)) {
            LOGW("Failed to parse forecast");
        }
    } else {
        LOGW("Failed to fetch forecast");
    }

    /* v1 air quality */
    snprintf(url, sizeof(url), "https://%s/airquality/v1/current/%s/%s?lang=zh", s_api_host, lat, lon);
    LOGI("Fetching air quality");
    if (weather_http_get_and_decompress(url, resp, WEATHER_JSON_BUF_SIZE)) {
        if (!weather_api_parse_air_json(resp, data)) {
            LOGW("Failed to parse air quality");
        }
    } else {
        LOGW("Failed to fetch air quality");
    }

    s_last_data = *data;
    LOGI("Weather: %s %sC AQI=%d UV=%d forecast=%d", s_last_data.weather_text, s_last_data.temp,
         (int)s_last_data.air_aqi, (int)s_last_data.uv_index, s_last_data.forecast_count);

    save_weather_cache();

    if (s_callback) {
        s_callback(&s_last_data, s_user_data);
    }
    s_in_progress = false;
    free(data);
    free(resp);
}
static void timer_callback(void *arg)
{
    (void)arg;
    weather_api_fetch();
}

#endif /* ESP_PLATFORM */

/* ============================================================ */
/* IP geolocation (target only)                                 */
/* ============================================================ */

#ifdef ESP_PLATFORM

bool weather_api_detect_location(void)
{
    char resp[1024];
    cJSON *root;
    cJSON *status;
    cJSON *lat_item;
    cJSON *lon_item;
    cJSON *city_item;
    int n;

    LOGI("Detecting location via IP geolocation");
    n = http_get_text("http://ip-api.com/json/?lang=zh-CN&fields=61439", resp, sizeof(resp));
    if (n < 0) {
        LOGE("IP geolocation request failed");
        return false;
    }

    root = cJSON_Parse(resp);
    if (!root) {
        LOGE("IP geolocation JSON parse error");
        return false;
    }

    status = cJSON_GetObjectItemCaseSensitive(root, "status");
    if (!cJSON_IsString(status) || strcmp(status->valuestring, "success") != 0) {
        LOGE("IP geolocation status not success");
        cJSON_Delete(root);
        return false;
    }

    lat_item = cJSON_GetObjectItemCaseSensitive(root, "lat");
    lon_item = cJSON_GetObjectItemCaseSensitive(root, "lon");
    if (!lat_item || !lon_item) {
        LOGE("IP geolocation: missing lat/lon");
        cJSON_Delete(root);
        return false;
    }

    {
        double lat = lat_item->valuedouble;
        double lon = lon_item->valuedouble;
        snprintf(s_location, sizeof(s_location), "%.4f,%.4f", lat, lon);
    }

    city_item = cJSON_GetObjectItemCaseSensitive(root, "city");
    if (cJSON_IsString(city_item) && city_item->valuestring) {
        snprintf(s_city_name, sizeof(s_city_name), "%s", city_item->valuestring);
    }

    cJSON_Delete(root);
    LOGI("Location detected: %s (city=%s)", s_location, s_city_name);

    /* Cache to NVS for subsequent boots. */
    nvs_state_set_string("weather_location", s_location);
    nvs_state_set_string("weather_city", s_city_name);

    return true;
}

void weather_api_redetect_location(void)
{
    s_location[0] = '\0';
    s_city_name[0] = '\0';
    s_need_auto_detect = true;
#    ifdef ESP_PLATFORM
    nvs_state_set_string("weather_location", "");
    nvs_state_set_string("weather_city", "");
#    endif
    LOGI("Location cache cleared; will re-detect on next fetch");
}

#else

bool weather_api_detect_location(void)
{
    return false;
}

void weather_api_redetect_location(void)
{
    s_location[0] = '\0';
    s_city_name[0] = '\0';
    s_need_auto_detect = true;
}

#endif /* ESP_PLATFORM */

/* ============================================================ */
/* Public API                                                   */
/* ============================================================ */

void weather_api_init(const char *api_key, const char *location, weather_callback_t callback, void *user_data)
{
    if (api_key) {
        snprintf(s_api_key, sizeof(s_api_key), "%s", api_key);
    }

    /* API host: NVS override → Kconfig default. */
    s_api_host[0] = '\0';
#ifdef ESP_PLATFORM
    {
        char host_buf[128];
        if (nvs_state_get_string("weather_host", host_buf, sizeof(host_buf)) && host_buf[0]) {
            snprintf(s_api_host, sizeof(s_api_host), "%s", host_buf);
        }
    }
#endif
    if (!s_api_host[0]) {
        snprintf(s_api_host, sizeof(s_api_host), "%s", CONFIG_QWEATHER_API_HOST);
    }

    /* Location priority: explicit param → NVS cache → Kconfig → auto-detect. */
    s_location[0] = '\0';
    s_city_name[0] = '\0';
    s_need_auto_detect = false;

    if (location && location[0]) {
        snprintf(s_location, sizeof(s_location), "%s", location);
    } else {
#ifdef ESP_PLATFORM
        char loc_buf[32];
        if (nvs_state_get_string("weather_location", loc_buf, sizeof(loc_buf)) && loc_buf[0]) {
            snprintf(s_location, sizeof(s_location), "%s", loc_buf);
            {
                char city_buf[32];
                if (nvs_state_get_string("weather_city", city_buf, sizeof(city_buf)) && city_buf[0]) {
                    snprintf(s_city_name, sizeof(s_city_name), "%s", city_buf);
                }
            }
        }
#endif
        if (!s_location[0]) {
            snprintf(s_location, sizeof(s_location), "%s", CONFIG_WEATHER_DEFAULT_LOCATION);
        }
        if (!s_location[0]) {
            s_need_auto_detect = true;
        }
    }

    s_callback = callback;
    s_user_data = user_data;
    s_initialized = true;

#ifdef ESP_PLATFORM
    if (!s_timer) {
        const esp_timer_create_args_t timer_args = {
            .callback = timer_callback,
            .arg = NULL,
            .name = "weather_refresh",
        };
        if (esp_timer_create(&timer_args, &s_timer) == ESP_OK) {
            /* Hourly auto-refresh. */
            esp_timer_start_periodic(s_timer, 60ULL * 60ULL * 1000000ULL);
        }
    }
#endif

#ifdef ESP_PLATFORM
    if (load_weather_cache() && s_callback) {
        s_callback(&s_last_data, s_user_data);
    }
#endif
    LOGI("Weather API initialised (location=%s host=%s)", s_location, s_api_host);
}

#ifdef ESP_PLATFORM
static void weather_fetch_task(void *arg)
{
    do_fetch(arg);
    sm_set_busy(SLEEP_BUSY_SRC_NET, false);
    vTaskDelete(NULL);
}
#endif

bool weather_api_fetch(void)
{
#ifdef ESP_PLATFORM
    if (!s_initialized || s_in_progress)
        return false;
    s_in_progress = true;
    BaseType_t ret = xTaskCreate(&weather_fetch_task, "weather_fetch", 8192, NULL, 3, NULL);
    if (ret != pdPASS) {
        s_in_progress = false;
        return false;
    }
    sm_set_busy(SLEEP_BUSY_SRC_NET, true);
    return true;
#else
    return false;
#endif
}

bool weather_api_fetch_now(void)
{
    return weather_api_fetch();
}

void weather_api_set_location(const char *location)
{
    if (location) {
        snprintf(s_location, sizeof(s_location), "%s", location);
    }
}

void weather_api_set_key(const char *api_key)
{
    if (api_key) {
        snprintf(s_api_key, sizeof(s_api_key), "%s", api_key);
    }
}

const char *weather_api_get_location(void)
{
    return s_location;
}

const char *weather_api_get_city_name(void)
{
    return s_city_name;
}

bool weather_api_is_ready(void)
{
    return s_initialized;
}

const weather_data_t *weather_api_get_cached(void)
{
    return &s_last_data;
}

const weather_data_t *weather_api_get_last_data(void)
{
    return &s_last_data;
}
