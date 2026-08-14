/*
 * Host unit tests for the network component (WeatherApi, HolidayFetcher,
 * PhotoStorage). Pure C, compiles with plain gcc — no cJSON linkage.
 *
 * Build & run:
 *   gcc -Icomponents/network/include -Icomponents/bsp/include \
 *       -Icomponents/rawdraw/include -o /tmp/test_network \
 *       tests/test_network.c components/network/weather_api.c \
 *       components/network/holiday_fetcher.c components/network/photo_storage.c \
 *       components/bsp/storage_manager.c -lm && /tmp/test_network
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <dirent.h>
#include <sys/stat.h>

#include "weather_api.h"
#include "holiday_fetcher.h"
#include "photo_storage.h"
#include "coding_plan_api.h"

static int g_tests  = 0;
static int g_failed = 0;

#define CHECK(cond, ...)                                                                                               \
    do {                                                                                                               \
        g_tests++;                                                                                                     \
        if (!(cond)) {                                                                                                 \
            g_failed++;                                                                                                \
            printf("  FAIL [%s:%d] ", __FILE__, __LINE__);                                                             \
            printf(__VA_ARGS__);                                                                                       \
            printf("\n");                                                                                              \
        }                                                                                                              \
    } while (0)

/* ------------------------------------------------------------------ */
/* Test fixtures                                                        */
/* ------------------------------------------------------------------ */

/* QWeather v1 /weather/current sample (flat JSON, no code/now wrapper).
 * condition.text=晴, condition.code=100, temperature=25.0, feelsLike=27.0,
 * humidity=0.45 (→45%), wind.direction.compass=sw, wind.scale=3, uvIndex=2. */
static const char *now_json = "{\"condition\":{\"text\":\"\xe6\x99\xb4\",\"code\":\"100\"},"
                              "\"temperature\":{\"value\":25.0},"
                              "\"feelsLike\":{\"value\":27.0},"
                              "\"humidity\":0.45,"
                              "\"wind\":{\"direction\":{\"compass\":\"sw\"},\"scale\":3},"
                              "\"uvIndex\":2.0}";

/* QWeather v1 /weather/daily sample (3 days, days[] array). */
static const char *forecast_json =
    "{\"days\":["
    "{\"daytime\":{\"condition\":{\"text\":\"\xe6\x99\xb4\",\"code\":\"100\"}},"
    "\"temperatureMax\":{\"value\":25.0},\"temperatureMin\":{\"value\":10.0}},"
    "{\"daytime\":{\"condition\":{\"text\":\"\xe5\xa4\x9a\xe4\xba\x91\",\"code\":\"101\"}},"
    "\"temperatureMax\":{\"value\":22.0},\"temperatureMin\":{\"value\":12.0}},"
    "{\"daytime\":{\"condition\":{\"text\":\"\xe5\xb0\x8f\xe9\x9b\xa8\",\"code\":\"305\"}},"
    "\"temperatureMax\":{\"value\":18.0},\"temperatureMin\":{\"value\":8.0}}"
    "]}";

/* QWeather v1 /airquality/current sample (indexes[] array, cn-mee entry). */
static const char *air_json = "{\"indexes\":["
                              "{\"code\":\"cn-mee\",\"aqi\":45,\"category\":\"\xe4\xbc\x98\"},"
                              "{\"code\":\"qaqi\",\"aqi\":50,\"category\":\"Good\"}"
                              "]}";

/* timor.tech holiday sample for 2026. */
static const char *holiday_json =
    "{\"code\":0,\"holiday\":{"
    "\"2026-01-01\":{\"holiday\":true,\"name\":\"元旦\",\"wage\":3,\"date\":\"2026-01-01\",\"rest\":1},"
    "\"2026-02-15\":{\"holiday\":false,\"name\":\"春节\",\"wage\":1,\"date\":\"2026-02-15\",\"rest\":0},"
    "\"2026-02-17\":{\"holiday\":true,\"name\":\"春节\",\"wage\":3,\"date\":\"2026-02-17\",\"rest\":1}"
    "}}";

static void clean_spiffs_dir(void)
{
    DIR           *dir = opendir("./spiffs");
    struct dirent *entry;
    if (!dir)
        return;
    while ((entry = readdir(dir)) != NULL) {
        char        path[280];
        const char *name = entry->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            continue;
        snprintf(path, sizeof(path), "./spiffs/%s", name);
        remove(path);
    }
    closedir(dir);
}

/* ------------------------------------------------------------------ */
/* WeatherApi tests                                                     */
/* ------------------------------------------------------------------ */

static void test_weather_now(void)
{
    weather_data_t d;
    memset(&d, 0, sizeof(d));
    printf("Running test_weather_now...\n");
    CHECK(weather_api_parse_now_json(now_json, &d), "parse_now_json failed\n");
    CHECK(strcmp(d.temp, "25") == 0, "temp=%s\n", d.temp);
    CHECK(d.temp_int == 25, "temp_int=%d\n", d.temp_int);
    CHECK(strcmp(d.feels_like, "27") == 0, "feels_like=%s\n", d.feels_like);
    CHECK(strcmp(d.weather_icon, "100") == 0, "icon=%s\n", d.weather_icon);
    CHECK(strcmp(d.weather_text, "\xe6\x99\xb4") == 0, "text=%s\n", d.weather_text);
    /* v1 wind.direction.compass → wind_dir */
    CHECK(strcmp(d.wind_dir, "sw") == 0, "wind_dir=%s\n", d.wind_dir);
    CHECK(strcmp(d.wind_scale, "3") == 0, "wind_scale=%s\n", d.wind_scale);
    /* v1 humidity is 0-1 float → percentage */
    CHECK(strcmp(d.humidity, "45") == 0, "humidity=%s\n", d.humidity);
    /* v1 uvIndex */
    CHECK(d.uv_index == 2, "uv_index=%d\n", d.uv_index);
    CHECK(weather_api_parse_weather_icon(d.weather_text) == WEATHER_ICON_SUNNY, "icon class wrong\n");
}

static void test_weather_forecast(void)
{
    weather_data_t d;
    memset(&d, 0, sizeof(d));
    printf("Running test_weather_forecast...\n");
    CHECK(weather_api_parse_forecast_json(forecast_json, &d), "parse_forecast_json failed\n");
    CHECK(d.forecast_count == 3, "forecast_count=%d\n", d.forecast_count);
    CHECK(strcmp(d.forecast[0].label, "今天") == 0, "label0=%s\n", d.forecast[0].label);
    CHECK(strcmp(d.forecast[1].label, "明天") == 0, "label1=%s\n", d.forecast[1].label);
    CHECK(strcmp(d.forecast[2].label, "后天") == 0, "label2=%s\n", d.forecast[2].label);
    CHECK(strcmp(d.forecast[0].weather_text, "晴") == 0, "text0=%s\n", d.forecast[0].weather_text);
    CHECK(strcmp(d.forecast[2].weather_text, "小雨") == 0, "text2=%s\n", d.forecast[2].weather_text);
    CHECK(d.forecast[0].temp_min == 10, "min0=%d\n", d.forecast[0].temp_min);
    CHECK(d.forecast[0].temp_max == 25, "max0=%d\n", d.forecast[0].temp_max);
    CHECK(d.forecast[2].temp_min == 8, "min2=%d\n", d.forecast[2].temp_min);
    CHECK(d.forecast[2].temp_max == 18, "max2=%d\n", d.forecast[2].temp_max);
}

static void test_weather_air(void)
{
    weather_data_t d;
    memset(&d, 0, sizeof(d));
    printf("Running test_weather_air...\n");
    CHECK(weather_api_parse_air_json(air_json, &d), "parse_air_json failed\n");
    CHECK(d.air_aqi == 45, "aqi=%d\n", d.air_aqi);
    CHECK(strcmp(d.air_quality, "优") == 0, "category=%s\n", d.air_quality);
}

static void test_weather_autodetect(void)
{
    weather_data_t d;
    memset(&d, 0, sizeof(d));
    printf("Running test_weather_autodetect...\n");
    /* now */
    CHECK(weather_api_parse_json(now_json, &d), "autodetect now failed\n");
    CHECK(strcmp(d.temp, "25") == 0, "autodetect temp=%s\n", d.temp);
    /* forecast */
    memset(&d, 0, sizeof(d));
    CHECK(weather_api_parse_json(forecast_json, &d), "autodetect forecast failed\n");
    CHECK(d.forecast_count == 3, "autodetect fc=%d\n", d.forecast_count);
    /* air */
    memset(&d, 0, sizeof(d));
    CHECK(weather_api_parse_json(air_json, &d), "autodetect air failed\n");
    CHECK(d.air_aqi == 45, "autodetect aqi=%d\n", d.air_aqi);
    /* error code path */
    CHECK(!weather_api_parse_json("{\"code\":\"404\"}", &d), "expected failure\n");
}

/* ------------------------------------------------------------------ */
/* HolidayFetcher tests                                                */
/* ------------------------------------------------------------------ */

static void test_holiday_parse(void)
{
    printf("Running test_holiday_parse...\n");
    CHECK(holiday_fetcher_parse_json(2026, holiday_json), "parse failed\n");
    /* 元旦 (2026-01-01) is a rest day / holiday */
    CHECK(holiday_fetcher_is_holiday(2026, 1, 1), "01-01 should be holiday\n");
    CHECK(!holiday_fetcher_is_makeup_workday(2026, 1, 1), "01-01 not makeup\n");
    {
        const char *name = holiday_fetcher_get_holiday_name(2026, 1, 1);
        CHECK(name && strcmp(name, "元旦") == 0, "name=%s\n", name ? name : "(null)");
    }
    /* 春节 actual (2026-02-17) rest day */
    CHECK(holiday_fetcher_is_holiday(2026, 2, 17), "02-17 should be holiday\n");
    /* 春节 补班 (2026-02-15) compensatory workday */
    CHECK(!holiday_fetcher_is_holiday(2026, 2, 15), "02-15 not holiday\n");
    CHECK(holiday_fetcher_is_makeup_workday(2026, 2, 15), "02-15 should be makeup\n");
    {
        const char *lbl = holiday_fetcher_get_makeup_label(2026, 2, 15);
        CHECK(lbl && strcmp(lbl, "班") == 0, "label=%s\n", lbl ? lbl : "(null)");
    }
    CHECK(holiday_fetcher_get_holiday_name(2026, 2, 15) == NULL, "makeup day has no holiday name\n");
    /* A random non-listed date */
    CHECK(!holiday_fetcher_is_holiday(2026, 7, 15), "07-15 not holiday\n");
    CHECK(!holiday_fetcher_is_makeup_workday(2026, 7, 15), "07-15 not makeup\n");
    /* Cache inspection */
    {
        const holiday_cache_t *c = holiday_fetcher_get_cache();
        CHECK(c != NULL, "cache NULL\n");
        CHECK(c->year == 2026, "cache year=%d\n", c->year);
        CHECK(c->entry_count == 3, "entry_count=%d\n", c->entry_count);
    }
    /* Wrong year does not match */
    CHECK(!holiday_fetcher_is_holiday(2025, 1, 1), "2025 should miss\n");
}

/* ------------------------------------------------------------------ */
/* PhotoStorage tests                                                  */
/* ------------------------------------------------------------------ */

static void test_photo_storage_cycle(void)
{
    photo_info_t info;
    photo_info_t out;
    uint8_t      data[8];
    int          rc;
    printf("Running test_photo_storage_cycle...\n");

    clean_spiffs_dir();
    CHECK(photo_storage_init() == 0, "init failed\n");
    CHECK(photo_get_count() == 0, "expected empty, got %d\n", photo_get_count());

    memset(&info, 0, sizeof(info));
    snprintf(info.id, sizeof(info.id), "%s", "photo01");
    snprintf(info.title, sizeof(info.title), "%s", "那年今日");
    snprintf(info.location, sizeof(info.location), "%s", "西安");
    snprintf(info.body, sizeof(info.body), "%s", "西湖的清晨");
    info.width     = 400;
    info.height    = 300;
    info.file_size = sizeof(data);
    info.timestamp = 1700000000u;
    memset(data, 0xAB, sizeof(data));

    CHECK(photo_save(&info, data) == 0, "save failed\n");
    CHECK(photo_get_count() == 1, "count after save=%d\n", photo_get_count());
    CHECK(photo_exists("photo01"), "exists failed\n");

    rc = photo_get_by_index(0, &out);
    CHECK(rc == 0, "get_by_index rc=%d\n", rc);
    CHECK(strcmp(out.id, "photo01") == 0, "out id=%s\n", out.id);
    CHECK(strcmp(out.title, "那年今日") == 0, "out title=%s\n", out.title);
    CHECK(strcmp(out.location, "西安") == 0, "out loc=%s\n", out.location);
    CHECK(strcmp(out.body, "西湖的清晨") == 0, "out body=%s\n", out.body);
    CHECK(out.width == 400, "out width=%u\n", (unsigned)out.width);
    CHECK(out.height == 300, "out height=%u\n", (unsigned)out.height);
    CHECK(out.file_size == sizeof(data), "out file_size=%lu\n", (unsigned long)out.file_size);
    CHECK(out.timestamp == 1700000000u, "out ts=%lu\n", (unsigned long)out.timestamp);

    /* Reload index from disk to verify persistence. */
    CHECK(photo_storage_reload_index() == 0, "reload failed\n");
    CHECK(photo_get_count() == 1, "count after reload=%d\n", photo_get_count());
    rc = photo_get_by_index(0, &out);
    CHECK(rc == 0 && strcmp(out.title, "那年今日") == 0, "reload title mismatch\n");
    CHECK(rc == 0 && out.file_size == sizeof(data), "reload size mismatch\n");

    /* Delete and confirm removal. */
    CHECK(photo_delete("photo01") == 0, "delete failed\n");
    CHECK(photo_get_count() == 0, "count after delete=%d\n", photo_get_count());
    CHECK(!photo_exists("photo01"), "exists after delete\n");
}
/* ------------------------------------------------------------------ */
/* CodingPlanApi tests                                                 */
/* ------------------------------------------------------------------ */

/* quota/limit sample: unit=3 @50% (5h → 2M*0.5=1M), unit=6 @25% (week →
 * 10M*0.25=2.5M), nextResetTime 1723000000000ms. */
static const char *quota_json = "{\"data\":{\"limits\":["
                                "{\"type\":\"TOKENS_LIMIT\",\"unit\":3,\"number\":5,\"percentage\":50},"
                                "{\"type\":\"TOKENS_LIMIT\",\"unit\":6,\"number\":1,\"percentage\":25}"
                                "],\"nextResetTime\":1723000000000}}";

/* model-usage sample: 2 models + 5 hourly points + weekly total 5M. */
static const char *usage_json = "{\"data\":{\"totalUsage\":{\"totalTokensUsage\":5000000,"
                                "\"modelSummaryList\":["
                                "{\"modelName\":\"glm-4\",\"totalTokens\":3000000},"
                                "{\"modelName\":\"glm-4-air\",\"totalTokens\":2000000}"
                                "]},\"tokensUsage\":[100,200,300,400,500]}}";

static void test_coding_plan_quota(void)
{
    coding_plan_api_data_t d;
    printf("Running test_coding_plan_quota...\n");
    memset(&d, 0, sizeof(d));
    CHECK(parse_quota_limit_json(quota_json, &d), "quota parse should succeed\n");
    /* 2M * 50% = 1,000,000 */
    CHECK(d.five_hour_tokens == 1000000ULL, "5h tokens=%llu expected 1000000\n",
          (unsigned long long)d.five_hour_tokens);
    /* 10M * 25% = 2,500,000 */
    CHECK(d.week_tokens == 2500000ULL, "week tokens=%llu expected 2500000\n", (unsigned long long)d.week_tokens);
    /* reset times formatted "MM-DD HH:MM" (11 chars). */
    CHECK(d.five_hour_reset_time[0] != '\0', "five_hour_reset_time empty\n");
    CHECK(strlen(d.five_hour_reset_time) >= 11, "five_hour_reset_time too short: \"%s\"\n", d.five_hour_reset_time);
    CHECK(d.five_hour_reset_time[2] == '-' && d.five_hour_reset_time[5] == ' ' && d.five_hour_reset_time[8] == ':',
          "five_hour_reset_time bad format: \"%s\"\n", d.five_hour_reset_time);
    CHECK(d.week_reset_time[0] != '\0', "week_reset_time empty\n");
    CHECK(strlen(d.week_reset_time) >= 11, "week_reset_time too short: \"%s\"\n", d.week_reset_time);
    CHECK(d.week_reset_time[2] == '-' && d.week_reset_time[5] == ' ' && d.week_reset_time[8] == ':',
          "week_reset_time bad format: \"%s\"\n", d.week_reset_time);

    /* Malformed input is rejected. */
    memset(&d, 0, sizeof(d));
    CHECK(!parse_quota_limit_json("not json", &d), "garbage should fail\n");
    CHECK(!parse_quota_limit_json(NULL, &d), "NULL should fail\n");
}

static void test_coding_plan_usage(void)
{
    coding_plan_api_data_t d;
    printf("Running test_coding_plan_usage...\n");
    memset(&d, 0, sizeof(d));
    CHECK(parse_model_usage_json(usage_json, &d), "usage parse should succeed\n");
    CHECK(d.week_tokens == 5000000ULL, "week tokens=%llu expected 5000000\n", (unsigned long long)d.week_tokens);
    CHECK(d.per_model_count == 2, "model count=%d expected 2\n", d.per_model_count);
    CHECK(strcmp(d.per_model[0].name, "glm-4") == 0, "model[0] name=%s\n", d.per_model[0].name);
    CHECK(d.per_model[0].tokens == 3000000ULL, "model[0] tokens=%llu\n", (unsigned long long)d.per_model[0].tokens);
    CHECK(d.per_model[1].tokens == 2000000ULL, "model[1] tokens=%llu\n", (unsigned long long)d.per_model[1].tokens);
    CHECK(d.hourly_count == 5, "hourly count=%d expected 5\n", d.hourly_count);
    CHECK(d.hourly_tokens[0] == 100 && d.hourly_tokens[4] == 500, "hourly[0]=%llu [4]=%llu\n",
          (unsigned long long)d.hourly_tokens[0], (unsigned long long)d.hourly_tokens[4]);

    /* Empty payload leaves everything zero and returns false. */
    memset(&d, 0, sizeof(d));
    CHECK(!parse_model_usage_json("{}", &d), "empty object should fail\n");
}

int main(void)
{
    printf("Starting network component tests...\n");
    test_weather_now();
    test_weather_forecast();
    test_weather_air();
    test_weather_autodetect();
    test_holiday_parse();
    test_photo_storage_cycle();
    test_coding_plan_quota();
    test_coding_plan_usage();
    printf("\n%d tests, %d failures\n", g_tests, g_failed);
    if (g_failed != 0) {
        printf("NETWORK TESTS FAILED\n");
        return 1;
    }
    printf("All network component tests passed!\n");
    return 0;
}
