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
 *
 * Pure data types (holiday_entry_t, holiday_cache_t) and the query helpers
 * are declared in holiday_query.h, which this header pulls in.
 */
#ifndef HOLIDAY_FETCHER_H
#define HOLIDAY_FETCHER_H

#include "holiday_query.h"

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif

#endif /* HOLIDAY_FETCHER_H */
