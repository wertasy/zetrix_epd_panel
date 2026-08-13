/**
 * @file nvs_state.c
 * @brief Unified NVS persistent state implementation (write-through cache)
 *
 * Backing store:
 *  - Target (ESP-IDF): the "app_state" NVS namespace via nvs.h.
 *  - Host (gcc): a flat `./nvs_sim.txt` key=value file rewritten per set.
 *
 * The RAM `s_cache` is hydrated on init/load and is the source of truth for
 * reads. Setters mutate the cache then persist the affected key immediately
 * (write-through), so commits coalesce to one per call and no state is lost
 * on power loss.
 */

#include "nvs_state.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef ESP_PLATFORM
#    include "esp_log.h"
#    include "nvs_flash.h"
#    include "nvs.h"

static const char *TAG = "NvsState";
#    define NVS_NAMESPACE "app_state"

/* Single long-lived NVS_READWRITE handle opened in nvs_state_init and closed
 * in nvs_state_deinit — every kv_* call reuses it instead of open/close. */
static nvs_handle_t s_handle = 0;

#    define LOGI(...) ESP_LOGI(TAG, __VA_ARGS__)
#    define LOGE(...) ESP_LOGE(TAG, __VA_ARGS__)
#else
/* ---- Host simulation: POSIX file-backed key/value store ---- */
#    define NVS_SIM_PATH "./nvs_sim.txt"

#    define LOGI(...) ((void)0)
#    define LOGE(...) ((void)0)
#endif

/* ================================================================== */
/* Backing-store primitives                                            */
/* ================================================================== */

#ifdef ESP_PLATFORM
/* ---------- Target: NVS flash ---------- */

static bool kv_get_str(const char *key, char *out, size_t len)
{
    size_t    required = len;
    esp_err_t err      = nvs_get_str(s_handle, key, out, &required);
    if (err != ESP_OK) {
        if (len)
            out[0] = '\0';
        return false;
    }
    return true;
}

static bool kv_set_str(const char *key, const char *value)
{
    esp_err_t err = nvs_set_str(s_handle, key, value);
    if (err == ESP_OK)
        err = nvs_commit(s_handle);
    return err == ESP_OK;
}

static bool kv_get_i32(const char *key, int32_t *out)
{
    return nvs_get_i32(s_handle, key, out) == ESP_OK;
}

static bool kv_set_i32(const char *key, int32_t value)
{
    esp_err_t err = nvs_set_i32(s_handle, key, value);
    if (err == ESP_OK)
        err = nvs_commit(s_handle);
    return err == ESP_OK;
}

#else
/* ---------- Host: ./nvs_sim.txt key=value file ---------- */

static bool host_read_all(char *buf, size_t buf_len)
{
    if (buf_len == 0)
        return false;
    buf[0]  = '\0';
    FILE *f = fopen(NVS_SIM_PATH, "r");
    if (!f)
        return false;
    size_t n = fread(buf, 1, buf_len - 1, f);
    buf[n]   = '\0';
    fclose(f);
    return true;
}

static bool host_find_value(const char *key, char *out, size_t out_len)
{
    char buf[4096];
    if (!host_read_all(buf, sizeof(buf))) {
        if (out_len)
            out[0] = '\0';
        return false;
    }
    size_t klen = strlen(key);
    char  *line = buf;
    while (*line) {
        char  *nl   = strchr(line, '\n');
        size_t llen = nl ? (size_t)(nl - line) : strlen(line);
        if (llen > klen && line[klen] == '=' && strncmp(line, key, klen) == 0) {
            size_t vlen = llen - klen - 1;
            if (vlen >= out_len)
                vlen = out_len ? out_len - 1 : 0;
            memcpy(out, line + klen + 1, vlen);
            out[vlen] = '\0';
            return true;
        }
        if (!nl)
            break;
        line = nl + 1;
    }
    if (out_len)
        out[0] = '\0';
    return false;
}

static bool host_write_value(const char *key, const char *value)
{
    char buf[4096];
    host_read_all(buf, sizeof(buf)); /* tolerate missing file */
    FILE *f = fopen(NVS_SIM_PATH, "w");
    if (!f)
        return false;
    size_t klen     = strlen(key);
    bool   replaced = false;
    char  *line     = buf;
    while (*line) {
        char  *nl   = strchr(line, '\n');
        size_t llen = nl ? (size_t)(nl - line) : strlen(line);
        if (llen > klen && line[klen] == '=' && strncmp(line, key, klen) == 0) {
            fprintf(f, "%s=%s\n", key, value);
            replaced = true;
        } else {
            fwrite(line, 1, llen, f);
            fputc('\n', f);
        }
        if (!nl)
            break;
        line = nl + 1;
    }
    if (!replaced)
        fprintf(f, "%s=%s\n", key, value);
    fclose(f);
    return true;
}

static bool kv_get_str(const char *key, char *out, size_t len)
{
    if (len == 0)
        return false;
    return host_find_value(key, out, len);
}

static bool kv_set_str(const char *key, const char *value)
{
    return host_write_value(key, value);
}

static bool kv_get_i32(const char *key, int32_t *out)
{
    char v[32];
    if (!host_find_value(key, v, sizeof(v)))
        return false;
    *out = (int32_t)strtol(v, NULL, 10);
    return true;
}

static bool kv_set_i32(const char *key, int32_t value)
{
    char v[32];
    snprintf(v, sizeof(v), "%d", (int)value);
    return host_write_value(key, v);
}
#endif /* ESP_PLATFORM */

/* ================================================================== */
/* RAM cache (write-through)                                           */
/* ================================================================== */

/* NVS key constants — mirror the C++ originals exactly. */
#define K_WEATHER_CITY "weather_city"
#define K_WEATHER_AUTO "weather_auto"
#define K_WEATHER_INTERVAL "weather_interval"
#define K_CAL_LUNAR "cal_lunar"
#define K_CAL_YEAR "cal_year"
#define K_CAL_MONTH "cal_month"
#define K_CAL_DAY "cal_day"
#define K_EPD_PARTIAL "epd_partial"
#define K_EPD_LIFETIME "epd_lifetime"
#define K_EPD_LAST_TS "epd_last_ts"
#define K_UI_PAGE "ui_page"
#define K_UI_SCROLL "ui_scroll"
#define K_UI_LIFEBAR "ui_lifebar"
#define K_BT_ENABLED "bt_enabled"
#define K_AP_XFER_BOOT "ap_xfer_boot"

static system_settings_t s_cache;
static bool              s_initialized = false;

static void cache_apply_defaults(system_settings_t *s)
{
    memset(s, 0, sizeof(*s));
    s->weather.auto_update     = true;
    s->weather.update_interval = 1;
    s->calendar.show_lunar     = true;
    s->calendar.selected_year  = 2026;
    s->calendar.selected_month = 1;
    s->calendar.selected_day   = 0;
    s->ui.lifebar_visible      = 1;
}

static void cache_load_from_backing(system_settings_t *s)
{
    cache_apply_defaults(s);

    kv_get_str(K_WEATHER_CITY, s->weather.city, sizeof(s->weather.city));

    int32_t v;
    if (kv_get_i32(K_WEATHER_AUTO, &v))
        s->weather.auto_update = (v != 0);
    if (kv_get_i32(K_WEATHER_INTERVAL, &v))
        s->weather.update_interval = (int)v;
    if (kv_get_i32(K_CAL_LUNAR, &v))
        s->calendar.show_lunar = (v != 0);
    if (kv_get_i32(K_CAL_YEAR, &v))
        s->calendar.selected_year = (int)v;
    if (kv_get_i32(K_CAL_MONTH, &v))
        s->calendar.selected_month = (int)v;
    if (kv_get_i32(K_CAL_DAY, &v))
        s->calendar.selected_day = (int)v;
    if (kv_get_i32(K_EPD_PARTIAL, &v))
        s->epd.partial_count = (int)v;
    if (kv_get_i32(K_EPD_LIFETIME, &v))
        s->epd.lifetime_refreshes = (int)v;
    if (kv_get_i32(K_EPD_LAST_TS, &v))
        s->epd.last_refresh_timestamp = (int)v;
    if (kv_get_i32(K_UI_PAGE, &v))
        s->ui.last_page = (int)v;
    if (kv_get_i32(K_UI_SCROLL, &v))
        s->ui.summary_scroll = (int)v;
    if (kv_get_i32(K_UI_LIFEBAR, &v))
        s->ui.lifebar_visible = (int)v;
    if (kv_get_i32(K_BT_ENABLED, &v))
        s->ble.enabled = (v != 0);
    if (kv_get_i32(K_AP_XFER_BOOT, &v))
        s->ap_transfer_boot = (v != 0);
}

static bool cache_flush_to_backing(const system_settings_t *s)
{
    bool ok = true;
    ok &= kv_set_str(K_WEATHER_CITY, s->weather.city);
    ok &= kv_set_i32(K_WEATHER_AUTO, s->weather.auto_update ? 1 : 0);
    ok &= kv_set_i32(K_WEATHER_INTERVAL, s->weather.update_interval);
    ok &= kv_set_i32(K_CAL_LUNAR, s->calendar.show_lunar ? 1 : 0);
    ok &= kv_set_i32(K_CAL_YEAR, s->calendar.selected_year);
    ok &= kv_set_i32(K_CAL_MONTH, s->calendar.selected_month);
    ok &= kv_set_i32(K_CAL_DAY, s->calendar.selected_day);
    ok &= kv_set_i32(K_EPD_PARTIAL, s->epd.partial_count);
    ok &= kv_set_i32(K_EPD_LIFETIME, s->epd.lifetime_refreshes);
    ok &= kv_set_i32(K_EPD_LAST_TS, s->epd.last_refresh_timestamp);
    ok &= kv_set_i32(K_UI_PAGE, s->ui.last_page);
    ok &= kv_set_i32(K_UI_SCROLL, s->ui.summary_scroll);
    ok &= kv_set_i32(K_UI_LIFEBAR, s->ui.lifebar_visible);
    ok &= kv_set_i32(K_BT_ENABLED, s->ble.enabled ? 1 : 0);
    ok &= kv_set_i32(K_AP_XFER_BOOT, s->ap_transfer_boot ? 1 : 0);
    return ok;
}

/* ================================================================== */
/* Key <-> cache-field mapping for the generic typed accessors         */
/* ================================================================== */

/** @return true if @p key is a known string field served from the cache. */
static bool map_get_string(const char *key, char *out, size_t len)
{
    if (len == 0)
        return false;
    if (strcmp(key, K_WEATHER_CITY) == 0) {
        strncpy(out, s_cache.weather.city, len - 1);
        out[len - 1] = '\0';
        return s_cache.weather.city[0] != '\0';
    }
    return false;
}

static bool map_set_string(const char *key, const char *value)
{
    if (strcmp(key, K_WEATHER_CITY) == 0) {
        strncpy(s_cache.weather.city, value, sizeof(s_cache.weather.city) - 1);
        s_cache.weather.city[sizeof(s_cache.weather.city) - 1] = '\0';
        return kv_set_str(key, s_cache.weather.city);
    }
    return false;
}

/* ---- i32 key dispatch table -------------------------------------- *
 * Replaces the prior 14-way strcmp cascade with a sorted lookup table
 * + binary search (~4 comparisons worst case) followed by a switch on a
 * small enum id. Keys are sorted by strcmp() order; keep s_i32_keys in
 * lock-step with nvs_i32_key_id if you add or remove a field. */
enum nvs_i32_key_id {
    NVS_I32_AP_XFER_BOOT = 0,
    NVS_I32_BT_ENABLED,
    NVS_I32_CAL_DAY,
    NVS_I32_CAL_LUNAR,
    NVS_I32_CAL_MONTH,
    NVS_I32_CAL_YEAR,
    NVS_I32_EPD_LAST_TS,
    NVS_I32_EPD_LIFETIME,
    NVS_I32_EPD_PARTIAL,
    NVS_I32_UI_LIFEBAR,
    NVS_I32_UI_PAGE,
    NVS_I32_UI_SCROLL,
    NVS_I32_WEATHER_AUTO,
    NVS_I32_WEATHER_INTERVAL,
    NVS_I32_KEY_COUNT
};

typedef struct {
    const char *key;
    int         id;
} nvs_i32_key_entry_t;

/* Sorted ascending by strcmp() over .key. */
static const nvs_i32_key_entry_t s_i32_keys[NVS_I32_KEY_COUNT] = {
    { K_AP_XFER_BOOT,     NVS_I32_AP_XFER_BOOT     },
    { K_BT_ENABLED,       NVS_I32_BT_ENABLED       },
    { K_CAL_DAY,          NVS_I32_CAL_DAY          },
    { K_CAL_LUNAR,        NVS_I32_CAL_LUNAR        },
    { K_CAL_MONTH,        NVS_I32_CAL_MONTH        },
    { K_CAL_YEAR,         NVS_I32_CAL_YEAR         },
    { K_EPD_LAST_TS,      NVS_I32_EPD_LAST_TS      },
    { K_EPD_LIFETIME,     NVS_I32_EPD_LIFETIME     },
    { K_EPD_PARTIAL,      NVS_I32_EPD_PARTIAL      },
    { K_UI_LIFEBAR,       NVS_I32_UI_LIFEBAR       },
    { K_UI_PAGE,          NVS_I32_UI_PAGE          },
    { K_UI_SCROLL,        NVS_I32_UI_SCROLL        },
    { K_WEATHER_AUTO,     NVS_I32_WEATHER_AUTO     },
    { K_WEATHER_INTERVAL, NVS_I32_WEATHER_INTERVAL },
};

/* bsearch comparator: needle is a bare const char *, haystack slot is an
 * nvs_i32_key_entry_t. Compares on the entry's .key only. */
static int i32_key_cmp(const void *needle, const void *slot)
{
    const char                *key = (const char *)needle;
    const nvs_i32_key_entry_t *ent = (const nvs_i32_key_entry_t *)slot;
    return strcmp(key, ent->key);
}

/** @return the nvs_i32_key_id for @p key, or -1 if it is not a known i32 field. */
static int map_i32_key(const char *key)
{
    const nvs_i32_key_entry_t *hit = (const nvs_i32_key_entry_t *)bsearch(
        key, s_i32_keys, NVS_I32_KEY_COUNT, sizeof(s_i32_keys[0]), i32_key_cmp);
    return hit ? hit->id : -1;
}

static bool map_get_i32(const char *key, int32_t *out)
{
    switch (map_i32_key(key)) {
    case NVS_I32_AP_XFER_BOOT: *out = s_cache.ap_transfer_boot ? 1 : 0; return true;
    case NVS_I32_BT_ENABLED:   *out = s_cache.ble.enabled ? 1 : 0; return true;
    case NVS_I32_CAL_DAY:      *out = s_cache.calendar.selected_day; return true;
    case NVS_I32_CAL_LUNAR:    *out = s_cache.calendar.show_lunar ? 1 : 0; return true;
    case NVS_I32_CAL_MONTH:    *out = s_cache.calendar.selected_month; return true;
    case NVS_I32_CAL_YEAR:     *out = s_cache.calendar.selected_year; return true;
    case NVS_I32_EPD_LAST_TS:  *out = s_cache.epd.last_refresh_timestamp; return true;
    case NVS_I32_EPD_LIFETIME: *out = s_cache.epd.lifetime_refreshes; return true;
    case NVS_I32_EPD_PARTIAL:  *out = s_cache.epd.partial_count; return true;
    case NVS_I32_UI_LIFEBAR:   *out = s_cache.ui.lifebar_visible; return true;
    case NVS_I32_UI_PAGE:      *out = s_cache.ui.last_page; return true;
    case NVS_I32_UI_SCROLL:    *out = s_cache.ui.summary_scroll; return true;
    case NVS_I32_WEATHER_AUTO: *out = s_cache.weather.auto_update ? 1 : 0; return true;
    case NVS_I32_WEATHER_INTERVAL: *out = s_cache.weather.update_interval; return true;
    default: return false;
    }
}

static bool map_set_i32(const char *key, int32_t value)
{
    switch (map_i32_key(key)) {
    case NVS_I32_AP_XFER_BOOT: s_cache.ap_transfer_boot = (value != 0); break;
    case NVS_I32_BT_ENABLED:   s_cache.ble.enabled = (value != 0); break;
    case NVS_I32_CAL_DAY:      s_cache.calendar.selected_day = (int)value; break;
    case NVS_I32_CAL_LUNAR:    s_cache.calendar.show_lunar = (value != 0); break;
    case NVS_I32_CAL_MONTH:    s_cache.calendar.selected_month = (int)value; break;
    case NVS_I32_CAL_YEAR:     s_cache.calendar.selected_year = (int)value; break;
    case NVS_I32_EPD_LAST_TS:  s_cache.epd.last_refresh_timestamp = (int)value; break;
    case NVS_I32_EPD_LIFETIME: s_cache.epd.lifetime_refreshes = (int)value; break;
    case NVS_I32_EPD_PARTIAL:  s_cache.epd.partial_count = (int)value; break;
    case NVS_I32_UI_LIFEBAR:   s_cache.ui.lifebar_visible = (int)value; break;
    case NVS_I32_UI_PAGE:      s_cache.ui.last_page = (int)value; break;
    case NVS_I32_UI_SCROLL:    s_cache.ui.summary_scroll = (int)value; break;
    case NVS_I32_WEATHER_AUTO: s_cache.weather.auto_update = (value != 0); break;
    case NVS_I32_WEATHER_INTERVAL: s_cache.weather.update_interval = (int)value; break;
    default: return false;
    }
    return kv_set_i32(key, value);
}

/* ================================================================== */
/* Public API                                                          */
/* ================================================================== */

bool nvs_state_init(void)
{
    if (s_initialized) {
        return true;
    }
#ifdef ESP_PLATFORM
    /* Standard NVS flash init; idempotent if the app already initialized it. */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        LOGI("NVS re-erasing partition: %s", esp_err_to_name(err));
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        LOGE("nvs_flash_init failed: %s", esp_err_to_name(err));
        /* Still hydrate defaults so the cache is usable. */
        cache_apply_defaults(&s_cache);
        s_initialized = true;
        return false;
    }
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_handle) != ESP_OK) {
        LOGE("nvs_open failed for namespace '%s'", NVS_NAMESPACE);
        /* Hydrate defaults so the cache stays usable; kv writes will fail-safe. */
        cache_apply_defaults(&s_cache);
        s_initialized = true;
        return false;
    }
#endif
    cache_load_from_backing(&s_cache);
    s_initialized = true;
    LOGI("NVS state initialized (namespace=%s)", NVS_NAMESPACE);
    return true;
}

void nvs_state_deinit(void)
{
#ifdef ESP_PLATFORM
    if (s_handle != 0) {
        nvs_close(s_handle);
        s_handle = 0;
    }
#endif
    s_initialized = false;
}

bool nvs_state_load(system_settings_t *settings)
{
    if (!settings)
        return false;
    cache_load_from_backing(&s_cache);
    s_initialized = true;
    *settings     = s_cache;
    return true;
}

bool nvs_state_save(const system_settings_t *settings)
{
    if (!settings)
        return false;
    s_cache = *settings;
    bool ok = cache_flush_to_backing(&s_cache);
    s_initialized = true;
    return ok;
}

bool nvs_state_get_string(const char *key, char *out, size_t out_len)
{
    if (!key || !out || out_len == 0)
        return false;
    if (s_initialized && map_get_string(key, out, out_len))
        return true;
    /* Unknown/passthrough key: read straight from the backing store. */
    out[0] = '\0';
    return kv_get_str(key, out, out_len);
}

bool nvs_state_set_string(const char *key, const char *value)
{
    if (!key || !value)
        return false;
    if (s_initialized && map_set_string(key, value))
        return true;
    return kv_set_str(key, value);
}

bool nvs_state_get_i32(const char *key, int32_t *out)
{
    if (!key || !out)
        return false;
    if (s_initialized && map_get_i32(key, out))
        return true;
    return kv_get_i32(key, out);
}

bool nvs_state_set_i32(const char *key, int32_t value)
{
    if (!key)
        return false;
    if (s_initialized && map_set_i32(key, value))
        return true;
    return kv_set_i32(key, value);
}
