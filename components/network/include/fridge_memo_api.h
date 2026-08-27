/* components/network/include/fridge_memo_api.h */
/**
 * @file fridge_memo_api.h
 * @brief Fridge memo REST client (design doc v1.2 §7.1).
 *
 * JSON parsing / date math / sorting are plain cJSON + libc and build+run on
 * the Linux host (tests/test_fridge_memo.c). HTTP fetch/delete and the NVS
 * cache are target-only (wrap http_client_util + settings), guarded by
 * #ifdef ESP_PLATFORM. Modeled on coding_plan_api.
 *
 * Endpoints (base_url from NVS namespace "fridge", key "base_url"):
 *   GET    {base}/api/v1/fridge/items          -> {"updated_at", "items":[...]}
 *   DELETE {base}/api/v1/fridge/items/{id}     -> {"ok", "updated_at", "items":[...]}
 */
#ifndef FRIDGE_MEMO_API_H_
#define FRIDGE_MEMO_API_H_

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include "fridge_memo_dto.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- lifecycle / config (target: NVS override; host: params only) ---- */

/** Initialise. On target also loads NVS "fridge"/"base_url" and the cached
 *  snapshot from NVS "fridge_memo" (blob key "data"). base_url may be NULL. */
void fridge_memo_api_init(const char *base_url);

/** Set + (on target) persist base_url. Empty string clears it. */
void fridge_memo_api_set_base_url(const char *url);

void fridge_memo_api_get_base_url(char *out, size_t len);

/* ---- cached snapshot (cache-first render, design §4.4) ---- */

bool fridge_memo_api_has_cached_data(void);
const fridge_memo_snapshot_t *fridge_memo_api_get_cached_data(void);

/* ---- async fetch / delete with callbacks ---- */

typedef void (*fridge_memo_callback_t)(const fridge_memo_snapshot_t *snap, void *user_data);
typedef void (*fridge_memo_error_callback_t)(const char *message, void *user_data);

void fridge_memo_api_set_callback(fridge_memo_callback_t cb, void *user_data);
void fridge_memo_api_set_error_callback(fridge_memo_error_callback_t cb, void *user_data);

/** Async GET (target: background task; host: no-op returning false).
 *  Returns false without side effects while another fetch/delete runs. */
bool fridge_memo_api_fetch_async(void);

/** Async DELETE of one item; response's full items[] drives the callback.
 *  Guard rejections (busy/invalid id/unconfigured) return false WITHOUT
 *  dispatching errors — the caller is the single error reporter. */
bool fridge_memo_api_delete_async(const char *item_id);

/** True while a fetch/delete is in flight. Host stubs never set it. */
bool fridge_memo_api_is_busy(void);

/* ---- pure, host-testable ---- */

/**
 * Parse a GET/DELETE response body into @p out (unsorted; caller sorts).
 * Accepts both {"updated_at","items":[...]} and a bare [...].
 * If the list exceeds FRIDGE_MEMO_MAX_ITEMS, a clock-free urgency
 * pre-truncate (whole list staged, then sorted: earliest expiry first — ISO
 * dates compare lexicographically = chronologically; no-expiry items last,
 * newest-added kept among them) drops the least urgent — wire order alone
 * never decides what gets dropped. Caller re-sorts with a real clock.
 * @return true if at least the items array was found.
 */
bool fridge_memo_parse_items_json(const char *json, fridge_memo_snapshot_t *out);

/** Days stored = today - added_at + 1 (same-day = 1). -1 on parse failure. */
int fridge_memo_days_since(const char *iso_date, const struct tm *today);

/** Days remaining = expires_at - today. -1000 on missing/invalid date. */
int fridge_memo_days_until(const char *iso_date, const struct tm *today);

/** Derive display status. today==NULL -> UNKNOWN for items with expiry too
 *  (degraded mode when clock is not synced). */
fridge_memo_status_t fridge_memo_derive_status(const fridge_memo_item_t *item, const struct tm *today);

/**
 * In-place sort: EXPIRED first (most overdue first), then NEAR (ascending
 * remaining), then OK (ascending remaining); UNKNOWN tail sorted by added_at
 * descending. today==NULL -> whole list by added_at descending (degraded).
 * After sorting, count is clamped to FRIDGE_MEMO_MAX_ITEMS.
 */
void fridge_memo_sort_snapshot(fridge_memo_snapshot_t *snap, const struct tm *today);

/** Count items currently in @p status (degraded: only UNKNOWN counts). */
int fridge_memo_count_by_status(const fridge_memo_snapshot_t *snap, fridge_memo_status_t status,
                                const struct tm *today);

#ifdef __cplusplus
}
#endif

#endif /* FRIDGE_MEMO_API_H_ */
