/**
 * @file coding_plan_api.c
 * @brief Zhipu BigModel "Coding Plan" usage/quota API client (C port).
 *
 * Parsing logic is plain cJSON and builds/runs on a Linux host (covered by
 * tests/test_network.c). The HTTP fetch wraps http_get_with_headers and is
 * therefore target-only.
 */
#include "coding_plan_api.h"
#include "sleep_manager.h"

#include "cjson_util.h"
#include "http_client_util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef ESP_PLATFORM
#    include "esp_log.h"
#    include "nvs_state.h"
#    include <freertos/FreeRTOS.h>
#    include <freertos/task.h>
#    include "nvs_flash.h"
#    include "nvs.h"

static const char *TAG = "CodingPlanApi";
#    define LOGI(...) ESP_LOGI(TAG, __VA_ARGS__)
#    define LOGW(...) ESP_LOGW(TAG, __VA_ARGS__)
#    define LOGE(...) ESP_LOGE(TAG, __VA_ARGS__)
#else
#    define LOGI(...)                                                                                                  \
        do {                                                                                                           \
            fprintf(stderr, "[CP][I] " __VA_ARGS__);                                                                   \
            fputc('\n', stderr);                                                                                       \
        } while (0)
#    define LOGW(...)                                                                                                  \
        do {                                                                                                           \
            fprintf(stderr, "[CP][W] " __VA_ARGS__);                                                                   \
            fputc('\n', stderr);                                                                                       \
        } while (0)
#    define LOGE(...)                                                                                                  \
        do {                                                                                                           \
            fprintf(stderr, "[CP][E] " __VA_ARGS__);                                                                   \
            fputc('\n', stderr);                                                                                       \
        } while (0)
#endif

/* Kconfig defaults (available on target; stubbed for host tests). */
#ifndef CONFIG_CODING_PLAN_API_TOKEN
#    define CONFIG_CODING_PLAN_API_TOKEN ""
#endif
#ifndef CONFIG_CODING_PLAN_API_ORG
#    define CONFIG_CODING_PLAN_API_ORG "org-c0fb217715454D74b92930fE336e7BAd"
#endif
#ifndef CONFIG_CODING_PLAN_API_PROJECT
#    define CONFIG_CODING_PLAN_API_PROJECT "proj_f0a4484835804c13Bca7473E0563C567"
#endif

/* ------------------------------------------------------------------ */
/* Quota caps (mirror coding_plan_page.c CODING_PLAN_*_QUOTA_TOKENS)   */
/* ------------------------------------------------------------------ */

#define CP_5H_QUOTA_TOKENS 2000000ULL /* 2M tokens / 5h window */
#define CP_WEEK_QUOTA_TOKENS 10000000ULL /* 10M tokens / week     */

/* ------------------------------------------------------------------ */
/* Endpoint geometry                                                   */
/* ------------------------------------------------------------------ */

#define CP_API_HOST "https://open.bigmodel.cn"
#define CP_PATH_QUOTA "/api/monitor/usage/quota/limit"
#define CP_PATH_USAGE "/api/monitor/usage/model-usage"

#define CP_HTTP_BUF 8192
#define CP_TOKEN_LEN 512
#define CP_ORG_LEN 128
#define CP_PROJ_LEN 128

#define CP_7D_MS (7LL * 24 * 60 * 60 * 1000) /* 7 days in ms */

/* ------------------------------------------------------------------ */
/* Credentials (target: NVS → Kconfig; host: params only)             */
/* ------------------------------------------------------------------ */

static char s_token[CP_TOKEN_LEN] = {0};
static char s_org[CP_ORG_LEN] = {0};
static char s_project[CP_PROJ_LEN] = {0};

static coding_plan_api_data_t s_cached_data;
static bool s_has_cached_data = false;

#ifdef ESP_PLATFORM
static void save_cp_cache(const coding_plan_api_data_t *data)
{
    nvs_handle_t h;
    if (nvs_open("cp_cache", NVS_READWRITE, &h) != ESP_OK)
        return;
    nvs_set_blob(h, "data", data, sizeof(*data));
    nvs_commit(h);
    nvs_close(h);
    LOGI("Saved coding plan cache to NVS");
}

static bool load_cp_cache(coding_plan_api_data_t *out)
{
    nvs_handle_t h;
    if (nvs_open("cp_cache", NVS_READONLY, &h) != ESP_OK)
        return false;
    size_t len = sizeof(*out);
    esp_err_t err = nvs_get_blob(h, "data", out, &len);
    nvs_close(h);
    if (err == ESP_OK && len == sizeof(*out)) {
        LOGI("Loaded coding plan cache from NVS");
        return true;
    }
    return false;
}
#endif
void coding_plan_api_init(const char *token, const char *org, const char *project)
{
    if (token && token[0]) {
        snprintf(s_token, sizeof(s_token), "%s", token);
    }
    if (org && org[0]) {
        snprintf(s_org, sizeof(s_org), "%s", org);
    }
    if (project && project[0]) {
        snprintf(s_project, sizeof(s_project), "%s", project);
    }

#ifdef ESP_PLATFORM
    {
        char tok_buf[CP_TOKEN_LEN];
        if (nvs_state_get_string("coding_plan_token", tok_buf, sizeof(tok_buf)) && tok_buf[0]) {
            snprintf(s_token, sizeof(s_token), "%s", tok_buf);
        }
        char org_buf[CP_ORG_LEN];
        if (nvs_state_get_string("coding_plan_org", org_buf, sizeof(org_buf)) && org_buf[0]) {
            snprintf(s_org, sizeof(s_org), "%s", org_buf);
        }
        char proj_buf[CP_PROJ_LEN];
        if (nvs_state_get_string("coding_plan_project", proj_buf, sizeof(proj_buf)) && proj_buf[0]) {
            snprintf(s_project, sizeof(s_project), "%s", proj_buf);
        }
    }
#endif

    /* Fall back to Kconfig defaults when nothing was provided. */
    if (!s_token[0])
        snprintf(s_token, sizeof(s_token), "%s", CONFIG_CODING_PLAN_API_TOKEN);
    if (!s_org[0])
        snprintf(s_org, sizeof(s_org), "%s", CONFIG_CODING_PLAN_API_ORG);
    if (!s_project[0])
        snprintf(s_project, sizeof(s_project), "%s", CONFIG_CODING_PLAN_API_PROJECT);

#ifdef ESP_PLATFORM
    s_has_cached_data = load_cp_cache(&s_cached_data);
#endif
}

/* ------------------------------------------------------------------ */
/* Parsing helpers                                                     */
/* ------------------------------------------------------------------ */

/* cJSON numbers may arrive as string-encoded ints; accept both. */
static int64_t cp_get_int64(cJSON *obj, const char *key, int64_t def)
{
    cJSON *n = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!n)
        return def;
    if (cJSON_IsNumber(n)) {
        return (int64_t)n->valuedouble;
    }
    if (cJSON_IsString(n) && n->valuestring) {
        return (int64_t)strtoll(n->valuestring, NULL, 10);
    }
    return def;
}

static int cp_get_int(cJSON *obj, const char *key, int def)
{
    return (int)cp_get_int64(obj, key, (int64_t)def);
}

/* Format a Unix-ms timestamp as "MM-DD HH:MM" (local time). */
static void format_reset_time(int64_t ms, char *out, size_t out_size)
{
    if (ms <= 0 || !out || out_size == 0) {
        if (out && out_size)
            out[0] = '\0';
        return;
    }
    time_t t = (time_t)(ms / 1000);
    struct tm tmv;
    memset(&tmv, 0, sizeof(tmv));
    localtime_r(&t, &tmv);
    snprintf(out, out_size, "%02d-%02d %02d:%02d", tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min);
}

/* Case-insensitive string equality (cJSON strings). */
static bool str_ieq(const char *a, const char *b)
{
    if (!a || !b)
        return false;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return false;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

/* ------------------------------------------------------------------ */
/* quota/limit parser                                                  */
/* ------------------------------------------------------------------ */

bool parse_quota_limit_json(const char *json, coding_plan_api_data_t *out)
{
    if (!json || !out)
        return false;

    cJSON *root = cJSON_Parse(json);
    if (!root) {
        LOGW("quota/limit: JSON parse failed");
        return false;
    }

    cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (!data) {
        data = root; /* tolerate a flattened payload */
    }

    bool any = false;

    char root_reset[CODING_PLAN_RESET_TIME_LEN] = {0};
    /* nextResetTime at the data level (Unix ms). */
    if (cJSON_HasObjectItem(data, "nextResetTime")) {
        int64_t ms = cp_get_int64(data, "nextResetTime", 0);
        if (ms > 0) {
            format_reset_time(ms, root_reset, sizeof(root_reset));
        }
    }

    cJSON *limits = cJSON_GetObjectItemCaseSensitive(data, "limits");
    cJSON *limit;
    cJSON_ArrayForEach(limit, limits)
    {
        const char *type = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(limit, "type"));
        if (!str_ieq(type, "TOKENS_LIMIT")) {
            continue;
        }
        int unit = cp_get_int(limit, "unit", 0);
        int pct = cp_get_int(limit, "percentage", 0);
        if (pct < 0)
            pct = 0;
        if (pct > 100)
            pct = 100;

        if (unit == 3) {
            /* 5-hour window. */
            out->five_hour_pct = pct;
            out->five_hour_tokens = 2000000ULL * pct / 100;
            any = true;
            if (cJSON_HasObjectItem(limit, "nextResetTime")) {
                int64_t ms = cp_get_int64(limit, "nextResetTime", 0);
                if (ms > 0) {
                    format_reset_time(ms, out->five_hour_reset_time, sizeof(out->five_hour_reset_time));
                }
            }
        } else if (unit == 6) {
            /* Weekly window. */
            out->week_pct = pct;
            out->week_tokens = 10000000ULL * pct / 100;
            any = true;
            if (cJSON_HasObjectItem(limit, "nextResetTime")) {
                int64_t ms = cp_get_int64(limit, "nextResetTime", 0);
                if (ms > 0) {
                    format_reset_time(ms, out->week_reset_time, sizeof(out->week_reset_time));
                }
            }
        }
    }

    /* Fallback to root nextResetTime if limit-specific ones are missing. */
    if (out->five_hour_reset_time[0] == '\0') {
        strcpy(out->five_hour_reset_time, root_reset);
    }
    if (out->week_reset_time[0] == '\0') {
        strcpy(out->week_reset_time, root_reset);
    }

    cJSON_Delete(root);
    if (any) {
        LOGI("quota/limit: 5h=%d%% (reset=%s) week=%d%% (reset=%s)", out->five_hour_pct, out->five_hour_reset_time,
             out->week_pct, out->week_reset_time);
    }
    return any;
}

/* ------------------------------------------------------------------ */
/* model-usage parser                                                  */
/* ------------------------------------------------------------------ */

bool parse_model_usage_json(const char *json, coding_plan_api_data_t *out)
{
    if (!json || !out)
        return false;

    cJSON *root = cJSON_Parse(json);
    if (!root) {
        LOGW("model-usage: JSON parse failed");
        return false;
    }

    cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (!data) {
        data = root;
    }

    bool any = false;

    /* totalUsage → week_tokens + per_model[]. */
    cJSON *total = cJSON_GetObjectItemCaseSensitive(data, "totalUsage");
    if (total) {
        int64_t week = cp_get_int64(total, "totalTokensUsage", 0);
        if (week > 0) {
            out->week_tokens = (uint64_t)week;
            any = true;
        }

        cJSON *models = cJSON_GetObjectItemCaseSensitive(total, "modelSummaryList");
        cJSON *m;
        cJSON_ArrayForEach(m, models)
        {
            if (out->per_model_count >= CODING_PLAN_MAX_MODELS)
                break;
            coding_plan_model_usage_t *slot = &out->per_model[out->per_model_count];
            cjson_copy_str(m, "modelName", slot->name, sizeof(slot->name));
            int64_t tok = cp_get_int64(m, "totalTokens", 0);
            if (tok < 0)
                tok = 0;
            slot->tokens = (uint64_t)tok;
            if (slot->name[0] || slot->tokens) {
                out->per_model_count++;
                any = true;
            }
        }
    }

    /* tokensUsage[] → hourly_tokens[] (numbers, or objects with tokenUsage). */
    cJSON *hourly = cJSON_GetObjectItemCaseSensitive(data, "tokensUsage");
    cJSON *h;
    cJSON_ArrayForEach(h, hourly)
    {
        if (out->hourly_count >= CODING_PLAN_HOURS_7D)
            break;
        int64_t v = 0;
        if (cJSON_IsNumber(h)) {
            v = (int64_t)h->valuedouble;
        } else if (cJSON_IsString(h) && h->valuestring) {
            v = (int64_t)strtoll(h->valuestring, NULL, 10);
        } else if (cJSON_IsObject(h)) {
            v = cp_get_int64(h, "tokenUsage", cp_get_int64(h, "tokens", 0));
        }
        if (v < 0)
            v = 0;
        out->hourly_tokens[out->hourly_count++] = (uint64_t)v;
        any = true;
    }

    cJSON_Delete(root);
    if (any) {
        LOGI("model-usage: week=%llu models=%d hourly=%d", (unsigned long long)out->week_tokens, out->per_model_count,
             out->hourly_count);
    }
    return any;
}

/* ------------------------------------------------------------------ */
/* HTTP fetch (target only)                                            */
/* ------------------------------------------------------------------ */

#ifdef ESP_PLATFORM

static bool cp_http_get(const char *path, const char *query, char *buf, size_t buf_size)
{
    char url[512];
    if (query && query[0]) {
        snprintf(url, sizeof(url), "%s%s?%s", CP_API_HOST, path, query);
    } else {
        snprintf(url, sizeof(url), "%s%s", CP_API_HOST, path);
    }

    const char *headers[3] = {"authorization", "bigmodel-organization", "bigmodel-project"};
    const char *values[3] = {s_token, s_org, s_project};

    int n = http_get_with_headers(url, headers, values, 3, (uint8_t *)buf, buf_size - 1);
    if (n < 0) {
        LOGE("HTTP GET %s failed", path);
        return false;
    }
    if (n >= (int)buf_size)
        n = (int)buf_size - 1;
    buf[n] = '\0';
    return true;
}

bool coding_plan_api_fetch(coding_plan_api_data_t *out)
{
    if (!out)
        return false;
    if (!s_token[0]) {
        LOGE("API token not configured");
        return false;
    }

    memset(out, 0, sizeof(*out));

    char *buf = (char *)malloc(CP_HTTP_BUF);
    if (!buf) {
        LOGE("out of memory");
        return false;
    }

    bool any = false;

    /* 1. quota/limit?type=1 */
    if (cp_http_get(CP_PATH_QUOTA, "type=1", buf, CP_HTTP_BUF)) {
        if (parse_quota_limit_json(buf, out))
            any = true;
    } else {
        LOGW("quota/limit fetch failed");
    }

    /* 2. model-usage over the trailing 7 days. The API requires
     *    "yyyy-MM-dd HH:mm:ss" (not epoch ms) for startTime/endTime. */
    time_t now_t = time(NULL);
    if (now_t <= 0)
        now_t = 0;
    time_t start_t = now_t - (7 * 24 * 3600);
    struct tm start_tm, now_tm;
    localtime_r(&start_t, &start_tm);
    localtime_r(&now_t, &now_tm);
    char start_str[32], end_str[32];
    strftime(start_str, sizeof(start_str), "%Y-%m-%d %H:%M:%S", &start_tm);
    strftime(end_str, sizeof(end_str), "%Y-%m-%d %H:%M:%S", &now_tm);
    char query[128];
    /* URL-encode the spaces: the API wants "yyyy-MM-dd HH:mm:ss". */
    snprintf(query, sizeof(query), "startTime=%s&endTime=%s", start_str, end_str);
    for (char *c = query; *c; c++) {
        if (*c == ' ')
            *c = '+';
    }
    if (cp_http_get(CP_PATH_USAGE, query, buf, CP_HTTP_BUF)) {
        if (parse_model_usage_json(buf, out))
            any = true;
    } else {
        LOGW("model-usage fetch failed");
    }

    free(buf);
    return any;
}

#else /* host */

bool coding_plan_api_fetch(coding_plan_api_data_t *out)
{
    (void)out;
    return false;
}

#endif /* ESP_PLATFORM */

/* ------------------------------------------------------------------ */
/* Async fetch                                                         */
/* ------------------------------------------------------------------ */

static coding_plan_callback_t s_callback = NULL;
static void *s_callback_user_data = NULL;
static volatile bool s_fetch_in_progress = false;

bool coding_plan_api_has_cached_data(void)
{
    return s_has_cached_data;
}

const coding_plan_api_data_t *coding_plan_api_get_cached_data(void)
{
    return s_has_cached_data ? &s_cached_data : NULL;
}
void coding_plan_api_set_callback(coding_plan_callback_t cb, void *user_data)
{
    s_callback = cb;
    s_callback_user_data = user_data;
}

#ifdef ESP_PLATFORM
static void cp_fetch_task(void *arg)
{
    (void)arg;
    /* Allocate on heap — this struct is ~1.6KB and with the mbedTLS stack
     * usage (~10KB) would overflow a 16KB task stack. */
    coding_plan_api_data_t *data = (coding_plan_api_data_t *)malloc(sizeof(coding_plan_api_data_t));
    if (!data) {
        LOGE("out of memory for cp data");
        s_fetch_in_progress = false;
        sm_set_busy(SLEEP_BUSY_SRC_NET, false);
        vTaskDelete(NULL);
        return;
    }
    if (coding_plan_api_fetch(data)) {
        s_cached_data = *data;
        s_has_cached_data = true;
#    ifdef ESP_PLATFORM
        save_cp_cache(data);
#    endif
        if (s_callback) {
            s_callback(data, s_callback_user_data);
        }
    } else {
        LOGW("async fetch failed");
    }
    free(data);
    s_fetch_in_progress = false;
    sm_set_busy(SLEEP_BUSY_SRC_NET, false);
    vTaskDelete(NULL);
}

bool coding_plan_api_fetch_async(void)
{
    if (s_fetch_in_progress)
        return false;
    if (!s_token[0]) {
        LOGE("API token not configured");
        return false;
    }
    s_fetch_in_progress = true;
    BaseType_t ret = xTaskCreate(cp_fetch_task, "cp_fetch", 16384, NULL, 3, NULL);
    if (ret != pdPASS) {
        LOGE("Failed to create cp_fetch task");
        s_fetch_in_progress = false;
        return false;
    }
    sm_set_busy(SLEEP_BUSY_SRC_NET, true);
    return true;
}
#else
bool coding_plan_api_fetch_async(void)
{
    return false;
}
#endif
