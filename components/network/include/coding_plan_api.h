/**
 * @file coding_plan_api.h
 * @brief Zhipu BigModel "Coding Plan" usage/quota API client (C port).
 *
 * Fetches the 5-hour + weekly token quota and the trailing-7-day hourly
 * usage series from the open.bigmodel.cn dashboard API. JSON parsing is
 * host-testable (cJSON only); the HTTP fetch is target-only (it wraps
 * esp_http_client via http_client_util).
 *
 * Two endpoints (host fixed to https://open.bigmodel.cn):
 *   - quota/limit?type=1            current quota caps + next reset time
 *   - model-usage?startTime=&endTime=  7-day hourly + per-model token usage
 *
 * Three request headers are sent on every call:
 *   authorization: <token>
 *   bigmodel-organization: <org>
 *   bigmodel-project: <project>
 *
 * Usage:
 *   coding_plan_api_init(CONFIG_CODING_PLAN_API_TOKEN,
 *                        CONFIG_CODING_PLAN_API_ORG,
 *                        CONFIG_CODING_PLAN_API_PROJECT);
 *   coding_plan_api_data_t data;
 *   if (coding_plan_api_fetch(&data)) { ... render ... }
 */
#ifndef CODING_PLAN_API_H
#define CODING_PLAN_API_H

#include <stdint.h>
#include <stdbool.h>
#include "coding_plan_dto.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Lifecycle / credentials                                            */
/* ------------------------------------------------------------------ */

/**
 * @brief Initialise the client with credentials.
 *
 * On the target, each non-NULL/non-empty parameter is stored; an NVS
 * override (namespace "coding_plan", keys "token"/"org"/"project") then
 * takes precedence. On the host this just stores the parameters.
 */
void coding_plan_api_init(const char *token, const char *org, const char *project);

/* ------------------------------------------------------------------ */
/* Async fetch with callback                                          */
/* ------------------------------------------------------------------ */

typedef void (*coding_plan_callback_t)(const coding_plan_api_data_t *data, void *user_data);

/**
 * @brief Register a callback invoked after a successful fetch.
 */
void coding_plan_api_set_callback(coding_plan_callback_t cb, void *user_data);

/**
 * @brief Asynchronously fetch data (spawns a background task).
 * @return true if the task was launched.
 */
bool coding_plan_api_fetch_async(void);

/* ------------------------------------------------------------------ */
/* Cached data (loaded from NVS on init for instant render)           */
/* ------------------------------------------------------------------ */

/** Returns true if cached coding plan data was loaded from NVS. */
bool coding_plan_api_has_cached_data(void);

/** Returns a pointer to the cached data, or NULL if no cache exists. */
const coding_plan_api_data_t *coding_plan_api_get_cached_data(void);

/* ------------------------------------------------------------------ */
/* JSON parsing (host-testable)                                       */
/* ------------------------------------------------------------------ */

/**
 * @brief Parse a quota/limit response into @p out.
 *
 * Reads data.limits[]: an entry with type=="TOKENS_LIMIT" and unit==3 is
 * the 5-hour window (used = 2M × percentage/100); unit==6 is the weekly
 * window (used = 10M × percentage/100). data.nextResetTime (Unix ms) is
 * formatted into reset_time as "MM-DD HH:MM".
 * @return true if any quota field was populated.
 */
bool parse_quota_limit_json(const char *json, coding_plan_api_data_t *out);

/**
 * @brief Parse a model-usage response into @p out.
 *
 * Reads data.totalUsage.modelSummaryList[] → per_model[] (modelName /
 * totalTokens); data.tokensUsage[] → hourly_tokens[]; and
 * data.totalUsage.totalTokensUsage → week_tokens.
 * @return true if any usage field was populated.
 */
bool parse_model_usage_json(const char *json, coding_plan_api_data_t *out);

/* ------------------------------------------------------------------ */
/* HTTP fetch (target only; host no-op returning false)               */
/* ------------------------------------------------------------------ */

/**
 * @brief Fetch both endpoints and fill @p out.
 *
 * Performs two GETs (quota/limit + a 7-day model-usage range) using the
 * stored credentials. On the host this is a no-op returning false.
 * @return true if at least one endpoint returned usable data.
 */
bool coding_plan_api_fetch(coding_plan_api_data_t *out);

#ifdef __cplusplus
}
#endif

#endif /* CODING_PLAN_API_H */
