/**
 * @file coding_plan_dto.h
 * @brief Zhipu BigModel "Coding Plan" DTO types — shared type-only layer.
 *
 * Pure data types used by both the network API client (coding_plan_api) and
 * the UI page renderers. No functions, no implementation.
 */
#ifndef DATA_TYPES_CODING_PLAN_DTO_H
#define DATA_TYPES_CODING_PLAN_DTO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Shared data model (single source of truth — reused by the page)     */
/* ------------------------------------------------------------------ */

/** Max distinct models tracked in the breakdown. */
#define CODING_PLAN_MAX_MODELS 8
/** Model name buffer length. */
#define CODING_PLAN_NAME_LEN 16
/** Reset-time text buffer length. */
#define CODING_PLAN_RESET_TIME_LEN 32
/** Hourly buckets in a trailing 7-day window (7 × 24). */
#define CODING_PLAN_HOURS_7D 168

/** Per-model token consumption entry. */
typedef struct {
    char     name[CODING_PLAN_NAME_LEN];
    uint64_t tokens;
} coding_plan_model_usage_t;

/**
 * @brief Coding plan usage snapshot.
 *
 * @p reset_time is a pre-formatted display string (e.g. "08-06 22:00").
 * @p five_hour_tokens is the token count consumed inside the current 5-hour
 * window; @p week_tokens is the trailing 7-day total. @p hourly_tokens[]
 * holds up to 168 hourly counts (@p hourly_count is the real length).
 */
typedef struct {
    char                      five_hour_reset_time[CODING_PLAN_RESET_TIME_LEN];
    char                      week_reset_time[CODING_PLAN_RESET_TIME_LEN];
    uint64_t                  week_tokens;       /* trailing 7-day actual total */
    uint64_t                  five_hour_tokens;  /* 5h window actual total */
    int                       five_hour_pct;     /* 0-100 from quota/limit API */
    int                       week_pct;          /* 0-100 from quota/limit API */
    coding_plan_model_usage_t per_model[CODING_PLAN_MAX_MODELS];
    int                       per_model_count;
    uint64_t                  hourly_tokens[CODING_PLAN_HOURS_7D];
    int                       hourly_count;
} coding_plan_api_data_t;

#ifdef __cplusplus
}
#endif

#endif /* DATA_TYPES_CODING_PLAN_DTO_H */
