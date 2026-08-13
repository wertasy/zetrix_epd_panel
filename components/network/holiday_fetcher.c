/**
 * @file holiday_fetcher.c
 * @brief Chinese official holiday schedule — C port of holiday_fetcher.{h,cc}.
 *
 * JSON parsing uses the official cJSON library and is host-testable.
 * HTTP transport and NVS caching are target-only (the host has no network/NVS,
 * so fetch()/init() are no-ops there; parse_json() works everywhere).
 */
#include "holiday_fetcher.h"
#include "cJSON.h"
#include "cjson_util.h"
#include "http_client_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef ESP_PLATFORM
#    include "esp_log.h"
#    include "nvs_flash.h"
#    include <freertos/FreeRTOS.h>
#    include <freertos/task.h>
#    include "nvs.h"
extern const char globalsign_root_ca_pem[] asm("_binary_globalsign_root_ca_pem_start");

static const char *TAG = "HolidayFetcher";
#    define LOGI(...) ESP_LOGI(TAG, __VA_ARGS__)
#    define LOGW(...) ESP_LOGW(TAG, __VA_ARGS__)
#    define LOGE(...) ESP_LOGE(TAG, __VA_ARGS__)
#else
#    include <stdio.h>
#    define LOGI(...)                                                                                                  \
        do {                                                                                                           \
            fprintf(stderr, "[HOL][I] " __VA_ARGS__);                                                                  \
            fputc('\n', stderr);                                                                                       \
        } while (0)
#    define LOGW(...)                                                                                                  \
        do {                                                                                                           \
            fprintf(stderr, "[HOL][W] " __VA_ARGS__);                                                                  \
            fputc('\n', stderr);                                                                                       \
        } while (0)
#    define LOGE(...)                                                                                                  \
        do {                                                                                                           \
            fprintf(stderr, "[HOL][E] " __VA_ARGS__);                                                                  \
            fputc('\n', stderr);                                                                                       \
        } while (0)
#endif

#define HOLIDAY_NVS_NAMESPACE "holiday_cache"
#define HOLIDAY_MAX_RESPONSE 8192
#define HOLIDAY_BLOB_MAX 4096 /* NVS blob ceiling: sizeof(int) + HOLIDAY_MAX_ENTRIES*sizeof(holiday_entry_t) */

/* In-memory cache (the active year). */
static holiday_cache_t s_cache;
static bool s_loaded = false;

/* ============================================================ */
/* JSON parsing                                                 */
/* ============================================================ */

bool holiday_fetcher_parse_json(int year, const char *json)
{
    cJSON *root;
    cJSON *hol;
    cJSON *member;
    if (!json)
        return false;

    /* Parse the payload and locate the "holiday" object. */
    root = cJSON_Parse(json);
    if (!root)
        return false;
    hol = cJSON_GetObjectItemCaseSensitive(root, "holiday");
    if (!cJSON_IsObject(hol)) {
        LOGE("No 'holiday' key in response");
        cJSON_Delete(root);
        return false;
    }

    s_cache.year = year;
    s_cache.entry_count = 0;

    for (member = hol->child; member != NULL && s_cache.entry_count < HOLIDAY_MAX_ENTRIES; member = member->next) {
        char date_key[24];
        char name[HOLIDAY_NAME_LEN] = {0};
        int y, m, d;
        holiday_entry_t *e;

        /* The member key is a date "YYYY-MM-DD". */
        if (!member->string)
            continue;
        snprintf(date_key, sizeof(date_key), "%s", member->string);

        if (sscanf(date_key, "%d-%d-%d", &y, &m, &d) == 3) {
            if (y != year)
                continue;
        } else if (sscanf(date_key, "%d-%d", &m, &d) == 2) {
            y = year;
        } else {
            continue;
        }
        if (m < 1 || m > 12 || d < 1 || d > 31)
            continue;

        /* The "holiday" boolean is the authoritative flag: true = rest day,
         * false = compensatory workday (补班). The "rest" field is a wage
         * multiplier (1=normal, 2=200%, 3=300%) and cannot be used to
         * distinguish holidays from makeup days. */
        cJSON *holiday_flag = cJSON_GetObjectItemCaseSensitive(member, "holiday");
        bool is_holiday = cJSON_IsTrue(holiday_flag);

        cjson_copy_str(member, "name", name, sizeof(name));

        e = &s_cache.entries[s_cache.entry_count++];
        e->year = (int16_t)year;
        e->month = (int8_t)m;
        e->day = (int8_t)d;
        e->is_rest = is_holiday;
        snprintf(e->name, sizeof(e->name), "%s", name);
    }

    cJSON_Delete(root);
    LOGI("Parsed %d holiday entries for %d", s_cache.entry_count, year);
    s_loaded = true; /* cache is now valid for queries */
    return true;
}

/* ============================================================ */
/* NVS persistence (target only)                                */
/* ============================================================ */

#ifdef ESP_PLATFORM

static void save_cache(void)
{
    char key[32];
    nvs_handle_t handle;
    esp_err_t err;
    int blob_size;
    uint8_t blob[HOLIDAY_BLOB_MAX];

    snprintf(key, sizeof(key), "holiday_%d", s_cache.year);
    err = nvs_open(HOLIDAY_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        LOGE("NVS open failed: %s", esp_err_to_name(err));
        return;
    }

    blob_size = (int)sizeof(int) + s_cache.entry_count * (int)sizeof(holiday_entry_t);
    if (blob_size > HOLIDAY_BLOB_MAX) {
        nvs_close(handle);
        return;
    }
    memcpy(blob, &s_cache.entry_count, sizeof(int));
    if (s_cache.entry_count > 0) {
        memcpy(blob + sizeof(int), s_cache.entries, (size_t)s_cache.entry_count * sizeof(holiday_entry_t));
    }

    err = nvs_set_blob(handle, key, blob, (size_t)blob_size);
    if (err == ESP_OK)
        err = nvs_commit(handle);
    if (err != ESP_OK) {
        LOGE("NVS save failed: %s", esp_err_to_name(err));
    } else {
        LOGI("Saved %d entries for %d to NVS", s_cache.entry_count, s_cache.year);
    }
    nvs_close(handle);
}

static bool load_cache(int year)
{
    char key[32];
    nvs_handle_t handle;
    esp_err_t err;
    size_t blob_size = 0;
    uint8_t blob[HOLIDAY_BLOB_MAX];
    int count;

    snprintf(key, sizeof(key), "holiday_%d", year);
    err = nvs_open(HOLIDAY_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK)
        return false;

    err = nvs_get_blob(handle, key, NULL, &blob_size);
    if (err != ESP_OK || blob_size < sizeof(int)) {
        nvs_close(handle);
        return false;
    }
    if (blob_size > sizeof(blob)) {
        nvs_close(handle);
        return false;
    }
    err = nvs_get_blob(handle, key, blob, &blob_size);
    nvs_close(handle);
    if (err != ESP_OK)
        return false;

    memcpy(&count, blob, sizeof(int));
    if (count < 0 || count > HOLIDAY_MAX_ENTRIES)
        return false;
    if (blob_size < sizeof(int) + (size_t)count * sizeof(holiday_entry_t))
        return false;

    s_cache.year = year;
    s_cache.entry_count = count;
    if (count > 0) {
        memcpy(s_cache.entries, blob + sizeof(int), (size_t)count * sizeof(holiday_entry_t));
    }
    LOGI("Loaded %d cached entries for %d", count, year);
    return true;
}

#endif /* ESP_PLATFORM */

/* ============================================================ */

static const holiday_entry_t *find_entry(int year, int month, int day)
{
    int i;
    if (!s_loaded || s_cache.year != year)
        return NULL;
    for (i = 0; i < s_cache.entry_count; i++) {
        const holiday_entry_t *e = &s_cache.entries[i];
        if (e->month == month && e->day == day) {
            return e;
        }
    }
    return NULL;
}

/* ============================================================ */
/* Public API                                                   */
/* ============================================================ */

bool holiday_fetcher_init(void)
{
#ifdef ESP_PLATFORM
    time_t now;
    struct tm tm_buf;
    int year;
    now = time(NULL);
    localtime_r(&now, &tm_buf);
    year = tm_buf.tm_year + 1900;
    s_loaded = load_cache(year);
    return s_loaded;
#else
    /* Host: no NVS. Cache starts empty; tests call parse_json() directly. */
    s_loaded = false;
    return false;
#endif
}

bool holiday_fetcher_fetch(int year)
{
#ifdef ESP_PLATFORM
    char url[80];
    int n;
    char *resp = (char *)malloc(HOLIDAY_MAX_RESPONSE + 1);
    if (!resp) {
        LOGE("out of memory for response buffer");
        return false;
    }

    snprintf(url, sizeof(url), "https://timor.tech/api/holiday/year/%d", year);
    LOGI("Fetching holiday data: %s", url);

    /* Cloudflare requires a User-Agent header and HTTPS. */
    const char *hdr_k[] = {"User-Agent"};
    const char *hdr_v[] = {"Mozilla/5.0 (ESP32-S3 EPD Panel)"};

    /* Retry up to 3 times — Cloudflare occasionally sends incomplete
     * chunked data on the first attempt. */
    for (int attempt = 0; attempt < 3; attempt++) {
        n = http_get_with_headers_cert(url, hdr_k, hdr_v, 1, (uint8_t *)resp, HOLIDAY_MAX_RESPONSE + 1,
                                       globalsign_root_ca_pem);
        if (n >= 0)
            break;
        if (attempt < 2) {
            LOGW("Retry %d/3 for holiday data", attempt + 2);
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }
    if (n < 0) {
        LOGE("HTTP request failed after 3 attempts");
        free(resp);
        return false;
    }
    if (n > HOLIDAY_MAX_RESPONSE) {
        n = HOLIDAY_MAX_RESPONSE;
    }
    resp[n] = '\0';
    LOGI("Received %d bytes", n);

    if (!holiday_fetcher_parse_json(year, resp)) {
        LOGE("Failed to parse response");
        free(resp);
        return false;
    }
    free(resp);
    s_loaded = true;
    save_cache();
    return true;
#else
    (void)year;
    return false;
#endif
}

bool holiday_fetcher_is_holiday(int year, int month, int day)
{
    const holiday_entry_t *e = find_entry(year, month, day);
    return e != NULL && e->is_rest;
}

bool holiday_fetcher_is_makeup_workday(int year, int month, int day)
{
    const holiday_entry_t *e = find_entry(year, month, day);
    return e != NULL && !e->is_rest;
}

const char *holiday_fetcher_get_holiday_name(int year, int month, int day)
{
    const holiday_entry_t *e = find_entry(year, month, day);
    if (e && e->is_rest && e->name[0] != '\0') {
        return e->name;
    }
    return NULL;
}

const char *holiday_fetcher_get_makeup_label(int year, int month, int day)
{
    const holiday_entry_t *e = find_entry(year, month, day);
    if (e && !e->is_rest) {
        return "\xe7\x8f\xad"; /* 班 */
    }
    return NULL;
}

const holiday_cache_t *holiday_fetcher_get_cache(void)
{
    return &s_cache;
}
