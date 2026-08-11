/**
 * @file holiday_fetcher.h
 * @brief Chinese official holiday schedule (State Council / 国务院调休) — C port.
 *
 * Fetches from the timor.tech free API on the target and caches in NVS.
 * JSON parsing is performed with the official cJSON library and is
 * fully unit-testable on a Linux host.
 *
 * API response format:
 *   {"code":0, "holiday":{"2026-01-01":{"name":"元旦","rest":1}, ...}}
 *
 *   rest = 1  → holiday / rest day (休)
 *   rest = 0  → compensatory workday (补班)
 *
 * Usage:
 *   holiday_fetcher_init();              // at boot (loads NVS cache)
 *   holiday_fetcher_fetch(2026);         // fetch + cache
 *   holiday_fetcher_is_holiday(2026, 5, 1);      // -> true
 *   holiday_fetcher_is_makeup_workday(2026, 5, 4); // -> true (补班)
 */
#ifndef HOLIDAY_FETCHER_H
#define HOLIDAY_FETCHER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HOLIDAY_MAX_ENTRIES 50 /**< max holiday/workday entries per year */
#define HOLIDAY_NAME_LEN 16 /**< "春节", "国庆节" ...                */

/** A single holiday or adjustment entry. */
typedef struct {
    int16_t year; /**< e.g. 2026                          */
    int8_t  month; /**< 1-12                               */
    int8_t  day; /**< 1-31                               */
    char    name[HOLIDAY_NAME_LEN];
    bool    is_rest; /**< true = rest day, false = 补班       */
} holiday_entry_t;

/** Cached holiday data for one year. */
typedef struct {
    int             year;
    int             entry_count;
    holiday_entry_t entries[HOLIDAY_MAX_ENTRIES];
} holiday_cache_t;

/* ------------------------------------------------------------------ */
/* Lifecycle / fetch                                                   */
/* ------------------------------------------------------------------ */

/**
 * @brief Initialise the module (loads the current + next year from NVS).
 * @return true if a cache was loaded successfully.
 *
 * On the host NVS is unavailable; this is a no-op returning false.
 */
bool holiday_fetcher_init(void);

/**
 * @brief Fetch holiday data for a year from the API and update the cache.
 *
 * Blocks until complete. On the host this is a no-op returning false.
 * @return true on success.
 */
bool holiday_fetcher_fetch(int year);

/**
 * @brief Parse a holiday API JSON payload into the in-memory cache.
 *
 * Host-testable: after this returns, the query helpers reflect @p json.
 * @param year Year the payload describes (used to filter date keys).
 * @param json NUL-terminated JSON string.
 * @return true if at least the "holiday" object was found.
 */
bool holiday_fetcher_parse_json(int year, const char *json);

/* ------------------------------------------------------------------ */
/* Queries                                                             */
/* ------------------------------------------------------------------ */

bool holiday_fetcher_is_holiday(int year, int month, int day);
bool holiday_fetcher_is_makeup_workday(int year, int month, int day);

/** Holiday name (e.g. "春节") or NULL if @p date is not a rest day. */
const char *holiday_fetcher_get_holiday_name(int year, int month, int day);

/** "班" if @p date is a compensatory workday, otherwise NULL. */
const char *holiday_fetcher_get_makeup_label(int year, int month, int day);

/** Read-only view of the active year cache (for inspection / tests). */
const holiday_cache_t *holiday_fetcher_get_cache(void);

#ifdef __cplusplus
}
#endif

#endif /* HOLIDAY_FETCHER_H */
