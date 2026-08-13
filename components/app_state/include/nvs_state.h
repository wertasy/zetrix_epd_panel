/**
 * @file nvs_state.h
 * @brief Unified NVS persistent state for app-level preferences (write-through cache)
 *
 * C port of the original C++ `nvs_state` namespace. Persistence covers:
 *  - Weather preferences (city, auto-update, interval)
 *  - Calendar display preferences (show lunar, selected date)
 *  - EPD refresh counters (partial count, lifetime, last-refresh timestamp)
 *  - UI navigation state (last page, scroll, life-bar visibility)
 *  - BLE enabled flag
 *  - AP-transfer boot mode flag
 *
 * Cache model:
 *  - A single in-RAM `system_settings_t` cache is the live source of truth.
 *  - `nvs_state_load()`  hydrates the cache (and the caller's copy) from flash.
 *  - `nvs_state_save()`  flushes a full settings snapshot to flash.
 *  - `nvs_state_get_*()` read from the RAM cache (fast, no flash I/O).
 *  - `nvs_state_set_*()` update the RAM cache immediately and persist the
 *    affected key to flash (write-through). Repeated sets therefore coalesce
 *    to one commit per call — no N+1 flash erase cycles — and a crash never
 *    loses committed state.
 *
 * Generic key/value access:
 *  Known typed keys are mapped to `system_settings_t` fields. Arbitrary keys
 *  (e.g. "wifi_ssid") pass straight through to the backing store, so callers
 *  may store ad-hoc strings/i32s without extending the struct.
 */

#ifndef NVS_STATE_H
#define NVS_STATE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Field capacity limits (mirror the NVS key/value sizes on target)   */
/* ------------------------------------------------------------------ */
#define NVS_CITY_MAX_LEN 64 /* weather city string            */
#define NVS_STR_VALUE_MAX_LEN 128 /* generic get/set_string buffer  */

/* ------------------------------------------------------------------ */
/* Persistent preference structs (1:1 with the C++ originals)         */
/* ------------------------------------------------------------------ */

/** @brief Persistent weather preferences */
typedef struct {
    char city[NVS_CITY_MAX_LEN]; /**< preferred city (e.g. "西安")      */
    bool auto_update; /**< auto-refresh weather (default on) */
    int update_interval; /**< hours between auto-updates (def 1)*/
} nvs_weather_prefs_t;

/** @brief Persistent calendar display preferences */
typedef struct {
    bool show_lunar; /**< show lunar dates (default on)     */
    int selected_year; /**< last viewed year                  */
    int selected_month; /**< last viewed month                 */
    int selected_day; /**< last selected day (0 = none)      */
} nvs_calendar_prefs_t;

/** @brief Persistent EPD refresh counters */
typedef struct {
    int partial_count; /**< partials since last full refresh  */
    int lifetime_refreshes; /**< total refreshes (wear tracking)   */
    int last_refresh_timestamp; /**< unix ts of last refresh           */
} nvs_epd_refresh_state_t;

/** @brief Persistent UI navigation state */
typedef struct {
    int last_page; /**< last active page index            */
    int summary_scroll; /**< summary page scroll offset        */
    int lifebar_visible; /**< life-bar visibility toggle (0/1)  */
} nvs_ui_nav_state_t;

/** @brief Persistent BLE state */
typedef struct {
    bool enabled; /**< BLE enabled (0=off, 1=on)         */
} nvs_ble_state_t;

/**
 * @brief Aggregate of all persisted app preferences — the write-through cache.
 */
typedef struct {
    nvs_weather_prefs_t weather;
    nvs_calendar_prefs_t calendar;
    nvs_epd_refresh_state_t epd;
    nvs_ui_nav_state_t ui;
    nvs_ble_state_t ble;
    bool ap_transfer_boot; /**< boot into AP transfer mode */
} system_settings_t;

/* ------------------------------------------------------------------ */
/* Initialization / lifecycle                                         */
/* ------------------------------------------------------------------ */

/**
 * @brief Initialize the NVS state store and hydrate the RAM cache.
 * @return true if the backing store is accessible.
 *
 * On target this opens the "app_state" NVS namespace; on host it opens (or
 * creates) the `./nvs_sim.txt` key-value file.
 */
bool nvs_state_init(void);

/**
 * @brief Flush any pending cached writes and release backing-store handles.
 */
void nvs_state_deinit(void);

/* ------------------------------------------------------------------ */
/* Whole-cache load / save                                            */
/* ------------------------------------------------------------------ */

/**
 * @brief Load all persisted preferences into @p settings (and the RAM cache).
 * @return true on success.
 */
bool nvs_state_load(system_settings_t *settings);

/**
 * @brief Persist a full settings snapshot to flash and refresh the cache.
 * @return true on success.
 */
bool nvs_state_save(const system_settings_t *settings);

/* ------------------------------------------------------------------ */
/* Generic typed access (cache + write-through persistence)           */
/* ------------------------------------------------------------------ */

/**
 * @brief Read a string value for @p key.
 *
 * Known typed keys are served from the RAM cache; unknown keys fall through
 * to the backing store. @p out is always NUL-terminated on success.
 * @return true if a value was found.
 */
bool nvs_state_get_string(const char *key, char *out, size_t out_len);

/**
 * @brief Write a string value for @p key.
 *
 * Updates the RAM cache (for known keys) and persists to flash (write-through).
 * @return true on success.
 */
bool nvs_state_set_string(const char *key, const char *value);

/**
 * @brief Read an int32 value for @p key (cache for known keys, else backing).
 * @return true if a value was found.
 */
bool nvs_state_get_i32(const char *key, int32_t *out);

/**
 * @brief Write an int32 value for @p key (cache + write-through persistence).
 * @return true on success.
 */
bool nvs_state_set_i32(const char *key, int32_t value);

#ifdef __cplusplus
}
#endif

#endif /* NVS_STATE_H */
