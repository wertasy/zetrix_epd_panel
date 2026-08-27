/* components/network/fridge_memo_api.c */
/**
 * @file fridge_memo_api.c
 * @brief Fridge memo REST client — parsing/date-math/sorting host-testable,
 *        HTTP + NVS cache target-only (design doc v1.2 §7.1).
 */
#include "fridge_memo_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cJSON.h>

#ifdef ESP_PLATFORM
#    include "esp_log.h"
#    include "nvs.h"
#    include "settings.h"
#    include "sleep_manager.h"
#    include <freertos/FreeRTOS.h>
#    include <freertos/task.h>
#    include "http_client_util.h"

static const char *TAG = "FridgeMemoApi";
#    define LOGI(...) ESP_LOGI(TAG, __VA_ARGS__)
#    define LOGW(...) ESP_LOGW(TAG, __VA_ARGS__)
#    define LOGE(...) ESP_LOGE(TAG, __VA_ARGS__)
#else
#    define LOGI(...)                                                                                                  \
        do {                                                                                                           \
            fprintf(stderr, "[FM][I] " __VA_ARGS__);                                                                   \
            fputc('\n', stderr);                                                                                       \
        } while (0)
#    define LOGW(...)                                                                                                  \
        do {                                                                                                           \
            fprintf(stderr, "[FM][W] " __VA_ARGS__);                                                                   \
            fputc('\n', stderr);                                                                                       \
        } while (0)
#    define LOGE(...)                                                                                                  \
        do {                                                                                                           \
            fprintf(stderr, "[FM][E] " __VA_ARGS__);                                                                   \
            fputc('\n', stderr);                                                                                       \
        } while (0)
#endif

#define FM_BASE_URL_LEN 96
#define FM_HTTP_BUF 24576 /* contract-max 64-item payload ≈ 16.3 KB */

static char s_base_url[FM_BASE_URL_LEN] = {0};
static fridge_memo_snapshot_t s_snapshot;
static bool s_has_snapshot = false;
static fridge_memo_callback_t s_cb = NULL;
static void *s_cb_ctx = NULL;
static fridge_memo_error_callback_t s_err_cb = NULL;
static void *s_err_cb_ctx = NULL;

/* qsort comparators get no context argument, so the sort key's "today" is
 * threaded through this file-static for the duration of one qsort call. */
static const struct tm *s_sort_today = NULL;

/* Re-entry guard: fetch and delete both mutate the snapshot (and share the
 * s_sort_today qsort context), so only one may be in flight at a time. */
static volatile bool s_busy = false;

/* ------------------------------------------------------------------ */
/* Date helpers (pure)                                                 */
/* ------------------------------------------------------------------ */

/* Parse "YYYY-MM-DD" into a normalized tm (midnight). */
static bool parse_iso_date(const char *iso, struct tm *out)
{
    if (!iso || strlen(iso) < 10 || iso[4] != '-' || iso[7] != '-')
        return false;
    int y = atoi(iso);
    int m = atoi(iso + 5);
    int d = atoi(iso + 8);
    if (y < 1970 || m < 1 || m > 12 || d < 1 || d > 31)
        return false;
    memset(out, 0, sizeof(*out));
    out->tm_year = y - 1900;
    out->tm_mon = m - 1;
    out->tm_mday = d;
    return true;
}

static int days_between(const struct tm *from, const struct tm *to)
{
    /* Calendar-day difference: normalize both sides to local midnight first,
     * otherwise a today with tm_hour=12 truncates +1.5d diffs to 1 day. */
    struct tm a = *from, b = *to;
    a.tm_hour = 0;
    a.tm_min = 0;
    a.tm_sec = 0;
    b.tm_hour = 0;
    b.tm_min = 0;
    b.tm_sec = 0;
    return (int)((mktime(&b) - mktime(&a)) / (24 * 60 * 60));
}

int fridge_memo_days_since(const char *iso_date, const struct tm *today)
{
    struct tm d;
    if (!today || !parse_iso_date(iso_date, &d))
        return -1;
    return days_between(&d, today) + 1; /* same-day counts as day 1 */
}

int fridge_memo_days_until(const char *iso_date, const struct tm *today)
{
    struct tm d;
    if (!today || !parse_iso_date(iso_date, &d))
        return -1000;
    return days_between(today, &d);
}

fridge_memo_status_t fridge_memo_derive_status(const fridge_memo_item_t *item, const struct tm *today)
{
    if (!item || !today || item->expires_at[0] == '\0')
        return FRIDGE_MEMO_STATUS_UNKNOWN;
    int days = fridge_memo_days_until(item->expires_at, today);
    if (days == -1000)
        return FRIDGE_MEMO_STATUS_UNKNOWN; /* unparseable date ranks like no expiry */
    if (days < 0)
        return FRIDGE_MEMO_STATUS_EXPIRED;
    if (days <= 2)
        return FRIDGE_MEMO_STATUS_NEAR;
    return FRIDGE_MEMO_STATUS_OK;
}

/* ------------------------------------------------------------------ */
/* Sort (pure)                                                         */
/* ------------------------------------------------------------------ */
static int cmp_degraded(const void *a, const void *b)
{
    const fridge_memo_item_t *ia = a, *ib = b;
    return -strcmp(ia->added_at, ib->added_at); /* newest first */
}

static int cmp_urgent(const void *a, const void *b)
{
    const fridge_memo_item_t *ia = a, *ib = b;
    fridge_memo_status_t sa = fridge_memo_derive_status(ia, s_sort_today);
    fridge_memo_status_t sb = fridge_memo_derive_status(ib, s_sort_today);
    if (sa != sb)
        return (int)sb - (int)sa; /* EXPIRED(3) > NEAR(2) > OK(1); UNKNOWN(0) tail */
    if (sa == FRIDGE_MEMO_STATUS_UNKNOWN)
        return -strcmp(ia->added_at, ib->added_at);
    /* smaller = more urgent; EXPIRED uses -days so most overdue sorts first */
    int ra = fridge_memo_days_until(ia->expires_at, s_sort_today);
    int rb = fridge_memo_days_until(ib->expires_at, s_sort_today);
    return ra - rb;
}

/* Unparseable expiry ranks with the no-expiry tail, matching derive_status
 * routing invalid dates to UNKNOWN. */
static bool expiry_ranks_last(const fridge_memo_item_t *it)
{
    struct tm d;
    return it->expires_at[0] == '\0' || !parse_iso_date(it->expires_at, &d);
}

/* Clock-free urgency for the parse-time >64 pre-truncate: earliest expiry
 * first (ISO dates sort lexicographically = chronologically, so expired
 * items are kept), no-expiry items last; among two no-expiry items the
 * newest-added sorts first so overflow drops the oldest (design §4.1 tail). */
static int cmp_truncate(const void *a, const void *b)
{
    const fridge_memo_item_t *ia = a, *ib = b;
    bool a_last = expiry_ranks_last(ia);
    bool b_last = expiry_ranks_last(ib);
    if (a_last && b_last)
        return -strcmp(ia->added_at, ib->added_at); /* newest first */
    if (a_last)
        return 1;
    if (b_last)
        return -1;
    return strcmp(ia->expires_at, ib->expires_at);
}

void fridge_memo_sort_snapshot(fridge_memo_snapshot_t *snap, const struct tm *today)
{
    if (!snap || snap->count <= 0)
        return;
    if (!today) {
        qsort(snap->items, (size_t)snap->count, sizeof(snap->items[0]), cmp_degraded);
    } else {
        s_sort_today = today;
        qsort(snap->items, (size_t)snap->count, sizeof(snap->items[0]), cmp_urgent);
        s_sort_today = NULL;
    }
    if (snap->count > FRIDGE_MEMO_MAX_ITEMS)
        snap->count = FRIDGE_MEMO_MAX_ITEMS; /* drop the least urgent tail */
}

int fridge_memo_count_by_status(const fridge_memo_snapshot_t *snap, fridge_memo_status_t status, const struct tm *today)
{
    int n = 0;
    for (int i = 0; i < snap->count; ++i)
        if (fridge_memo_derive_status(&snap->items[i], today) == status)
            ++n;
    return n;
}

/* ------------------------------------------------------------------ */
/* JSON parsing (pure, cJSON)                                          */
/* ------------------------------------------------------------------ */

static void read_str(cJSON *obj, const char *key, char *out, size_t out_len)
{
    cJSON *n = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(n) && n->valuestring)
        snprintf(out, out_len, "%s", n->valuestring);
    else
        out[0] = '\0';
}

bool fridge_memo_parse_items_json(const char *json, fridge_memo_snapshot_t *out)
{
    if (!json || !out)
        return false;
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        LOGW("items: JSON parse failed");
        return false;
    }
    cJSON *items = NULL;
    cJSON *updated = NULL;
    if (cJSON_IsArray(root)) {
        items = root; /* bare-array tolerance */
    } else {
        items = cJSON_GetObjectItemCaseSensitive(root, "items");
        updated = cJSON_GetObjectItemCaseSensitive(root, "updated_at");
    }
    if (!cJSON_IsArray(items)) {
        cJSON_Delete(root);
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (updated && cJSON_IsString(updated) && updated->valuestring)
        snprintf(out->updated_at, sizeof(out->updated_at), "%s", updated->valuestring);

    /* Parse the whole wire list into an exact-size heap staging array so
     * truncation drops entries by urgency, never by wire position. Heap,
     * not stack: on target this runs from tasks with ~6KB stacks; the cJSON
     * tree is heap-resident already, so staging adds no peak-memory class. */
    int total = cJSON_GetArraySize(items);
    if (total < 0)
        total = 0;
    fridge_memo_item_t *stage = NULL;
    int n = 0;
    if (total > 0) {
        stage = malloc(sizeof(stage[0]) * (size_t)total);
        if (!stage) {
            cJSON_Delete(root);
            return false;
        }
        memset(stage, 0, sizeof(stage[0]) * (size_t)total);
        cJSON *it = NULL;
        cJSON_ArrayForEach(it, items)
        {
            if (!cJSON_IsObject(it))
                continue;
            read_str(it, "id", stage[n].id, FRIDGE_MEMO_ID_LEN);
            read_str(it, "name", stage[n].name, FRIDGE_MEMO_NAME_LEN);
            read_str(it, "quantity", stage[n].quantity, FRIDGE_MEMO_QTY_LEN);
            read_str(it, "added_at", stage[n].added_at, FRIDGE_MEMO_DATE_LEN);
            read_str(it, "expires_at", stage[n].expires_at, FRIDGE_MEMO_DATE_LEN);
            read_str(it, "note", stage[n].note, FRIDGE_MEMO_NOTE_LEN);
            read_str(it, "storage", stage[n].storage, FRIDGE_MEMO_STORAGE_LEN);
            ++n;
        }
    }

    /* Clock-free urgency pre-truncate (no "today" available at parse time):
     * ISO dates sort lexicographically = chronologically, so the earliest
     * expiries — the already-expired ones included — are kept; no-expiry
     * items sort last (newest-added kept). Only overflow reorders — small
     * lists keep wire order. */
    if (n > FRIDGE_MEMO_MAX_ITEMS) {
        qsort(stage, (size_t)n, sizeof(stage[0]), cmp_truncate);
        n = FRIDGE_MEMO_MAX_ITEMS;
    }
    if (n > 0)
        memcpy(out->items, stage, sizeof(stage[0]) * (size_t)n);
    out->count = n;
    free(stage);
    cJSON_Delete(root);
    return true;
}

/* ------------------------------------------------------------------ */
/* Config + cache (target)                                             */
/* ------------------------------------------------------------------ */

#ifdef ESP_PLATFORM

static void load_base_url_from_nvs(void)
{
    settings_handle_t h = settings_open("fridge", false);
    if (!h)
        return;
    char buf[FM_BASE_URL_LEN];
    settings_get_string(h, "base_url", buf, sizeof(buf), "");
    if (buf[0])
        snprintf(s_base_url, sizeof(s_base_url), "%s", buf);
    settings_close(h);
}

static void save_cache(const fridge_memo_snapshot_t *snap)
{
    /* Blob, not JSON string: NVS string writes are single-page (~4000B),
     * which a 64-item snapshot (~11.2KB raw, more serialized) silently
     * exceeds; blobs span pages. Same pattern as cp_cache. */
    nvs_handle_t h;
    esp_err_t err = nvs_open("fridge_memo", NVS_READWRITE, &h);
    if (err != ESP_OK) {
        LOGE("fridge memo cache write failed: %s", esp_err_to_name(err));
        return;
    }
    err = nvs_set_blob(h, "data", snap, sizeof(*snap));
    if (err == ESP_OK)
        err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK)
        LOGE("fridge memo cache write failed: %s", esp_err_to_name(err));
}

/* Clock helper: false when time is not trustworthy (pre-2024). */
static bool get_local_today(struct tm *out)
{
    time_t t = time(NULL);
    struct tm tmv;
    if (!localtime_r(&t, &tmv) || tmv.tm_year + 1900 < 2024)
        return false;
    *out = tmv;
    return true;
}

static void load_cache(void)
{
    /* Read the blob straight into the file-static snapshot — nothing big
     * ever lives on the caller's stack (init runs on the ~8KB main task). */
    nvs_handle_t h;
    if (nvs_open("fridge_memo", NVS_READONLY, &h) != ESP_OK)
        return;
    size_t len = sizeof(s_snapshot);
    esp_err_t err = nvs_get_blob(h, "data", &s_snapshot, &len);
    nvs_close(h);
    if (err != ESP_OK)
        return;
    if (len != sizeof(s_snapshot)) {
        LOGW("fridge memo cache size mismatch (got %u, want %u) — stale DTO version, ignored", (unsigned)len,
             (unsigned)sizeof(s_snapshot));
        return;
    }
    struct tm today;
    bool have_time = get_local_today(&today);
    fridge_memo_sort_snapshot(&s_snapshot, have_time ? &today : NULL);
    s_has_snapshot = true;
    LOGI("loaded fridge memo cache (%d items)", s_snapshot.count);
}

#endif /* ESP_PLATFORM */

void fridge_memo_api_init(const char *base_url)
{
    if (base_url && base_url[0])
        snprintf(s_base_url, sizeof(s_base_url), "%s", base_url);
#ifdef ESP_PLATFORM
    load_base_url_from_nvs();
    load_cache();
#endif
}

void fridge_memo_api_set_base_url(const char *url)
{
    snprintf(s_base_url, sizeof(s_base_url), "%s", url ? url : "");
#ifdef ESP_PLATFORM
    settings_handle_t h = settings_open("fridge", true);
    if (h) {
        settings_set_string(h, "base_url", s_base_url);
        settings_close(h);
    }
#endif
}

void fridge_memo_api_get_base_url(char *out, size_t len)
{
    snprintf(out, len, "%s", s_base_url);
}

bool fridge_memo_api_has_cached_data(void)
{
    return s_has_snapshot;
}

const fridge_memo_snapshot_t *fridge_memo_api_get_cached_data(void)
{
    return s_has_snapshot ? &s_snapshot : NULL;
}

void fridge_memo_api_set_callback(fridge_memo_callback_t cb, void *user_data)
{
    s_cb = cb;
    s_cb_ctx = user_data;
}

void fridge_memo_api_set_error_callback(fridge_memo_error_callback_t cb, void *user_data)
{
    s_err_cb = cb;
    s_err_cb_ctx = user_data;
}

/* ------------------------------------------------------------------ */
/* HTTP (target only)                                                  */
/* ------------------------------------------------------------------ */

#ifdef ESP_PLATFORM

static void dispatch_snapshot(fridge_memo_snapshot_t *snap)
{
    struct tm today;
    bool have_time = get_local_today(&today);
    fridge_memo_sort_snapshot(snap, have_time ? &today : NULL);
    s_snapshot = *snap;
    s_has_snapshot = true;
    save_cache(&s_snapshot);
    if (s_cb)
        s_cb(&s_snapshot, s_cb_ctx);
}

static void dispatch_error(const char *msg)
{
    if (s_err_cb)
        s_err_cb(msg, s_err_cb_ctx);
}

static void fetch_task(void *arg)
{
    (void)arg;
    sm_set_busy(SLEEP_BUSY_SRC_NET, true);
    /* The snapshot is ~11.2KB — heap, never stack: these tasks run on 16KB
     * stacks next to ~10KB of mbedTLS usage (see cp_fetch_task). */
    fridge_memo_snapshot_t *snap = (fridge_memo_snapshot_t *)malloc(sizeof(*snap));
    char url[FM_BASE_URL_LEN + 48];
    char *buf = malloc(FM_HTTP_BUF);
    if (!snap || !buf) {
        dispatch_error("内存不足");
    } else {
        snprintf(url, sizeof(url), "%s/api/v1/fridge/items", s_base_url);
        int n = http_get_text(url, buf, FM_HTTP_BUF);
        if (n < 0) {
            LOGW("GET items failed (base_url=%s)", s_base_url);
            dispatch_error("后端不可达");
        } else if (fridge_memo_parse_items_json(buf, snap)) {
            dispatch_snapshot(snap);
        } else {
            dispatch_error("响应解析失败");
        }
    }
    free(buf);
    free(snap);
    sm_set_busy(SLEEP_BUSY_SRC_NET, false);
    s_busy = false;
    vTaskDelete(NULL);
}

static void delete_task(void *arg)
{
    sm_set_busy(SLEEP_BUSY_SRC_NET, true);
    fridge_memo_snapshot_t *snap = (fridge_memo_snapshot_t *)malloc(sizeof(*snap));
    char url[FM_BASE_URL_LEN + 64];
    char *buf = malloc(FM_HTTP_BUF);
    if (!snap || !buf) {
        dispatch_error("内存不足");
    } else {
        snprintf(url, sizeof(url), "%s/api/v1/fridge/items/%s", s_base_url, (const char *)arg);
        int n = http_delete_text(url, buf, FM_HTTP_BUF);
        if (n < 0) {
            LOGW("DELETE failed (url=%s)", url);
            dispatch_error("删除失败：后端不可达");
        } else if (fridge_memo_parse_items_json(buf, snap)) {
            /* 200 and 404 both carry the authoritative full list (design §7.1). */
            dispatch_snapshot(snap);
        } else {
            dispatch_error("响应解析失败");
        }
    }
    free(buf);
    free(snap);
    free(arg);
    sm_set_busy(SLEEP_BUSY_SRC_NET, false);
    s_busy = false;
    vTaskDelete(NULL);
}

bool fridge_memo_api_fetch_async(void)
{
    if (s_busy) /* one mutation in flight already */
        return false;
    if (!s_base_url[0]) {
        dispatch_error("未配置冰箱后端地址");
        return false;
    }
    s_busy = true;
    if (xTaskCreate(fetch_task, "fm_fetch", 16384, NULL, 5, NULL) != pdPASS) {
        s_busy = false;
        return false;
    }
    return true;
}

bool fridge_memo_api_delete_async(const char *item_id)
{
    if (s_busy) /* one mutation in flight already */
        return false;
    if (!item_id || !item_id[0])
        return false; /* nothing selectable — silent, no error banner */
    if (!s_base_url[0])
        return false; /* silent: app layer is the single error reporter */
    char *id_copy = malloc(FRIDGE_MEMO_ID_LEN);
    if (!id_copy)
        return false;
    snprintf(id_copy, FRIDGE_MEMO_ID_LEN, "%s", item_id);
    s_busy = true;
    if (xTaskCreate(delete_task, "fm_del", 16384, id_copy, 5, NULL) != pdPASS) {
        free(id_copy);
        s_busy = false;
        return false;
    }
    return true;
}

#else /* host stubs */

bool fridge_memo_api_fetch_async(void)
{
    return false;
}

bool fridge_memo_api_delete_async(const char *item_id)
{
    (void)item_id;
    return false;
}

#endif

bool fridge_memo_api_is_busy(void)
{
    return s_busy;
}
