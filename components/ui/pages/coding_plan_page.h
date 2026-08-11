/**
 * @file coding_plan_page.h
 * @brief Coding Plan usage page renderer — local pure-C implementation
 *        (计划 Task 6.10, 本地全新).
 *
 * Shows the 5-hour and weekly token-quota progress bars, the quota reset
 * time, the trailing-7-day token total, a per-model consumption breakdown,
 * and a local bar chart drawn directly from the 7-day hourly usage series
 * (no LittleFS bitmap cache).
 */
#ifndef COMPONENTS_UI_PAGES_CODING_PLAN_PAGE_H_
#define COMPONENTS_UI_PAGES_CODING_PLAN_PAGE_H_

#include "page_renderer.h"
#include "coding_plan_api.h" /* shared quota types + hourly series */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Coding plan usage snapshot.
 *
 * @p five_hour_tokens is the token count consumed inside the current 5-hour
 * window (drives the 5-hour quota bar); @p week_tokens is the trailing
 * 7-day total (drives the weekly quota bar and the "近 7 天" line).
 * @p hourly_tokens holds up to CODING_PLAN_HOURS_7D hourly counts
 * (hourly_count is the real length); the bar chart aggregates these.
 */
typedef struct {
    char                      five_hour_reset_time[CODING_PLAN_RESET_TIME_LEN];
    char                      week_reset_time[CODING_PLAN_RESET_TIME_LEN];
    uint64_t                  week_tokens;
    uint64_t                  five_hour_tokens;
    int                       five_hour_pct;  /* 0-100 from quota/limit API */
    int                       week_pct;       /* 0-100 from quota/limit API */
    coding_plan_model_usage_t per_model[CODING_PLAN_MAX_MODELS];
    int                       per_model_count;
    uint64_t                  hourly_tokens[CODING_PLAN_HOURS_7D];
    int                       hourly_count;
} coding_plan_data_t;

typedef struct {
    page_renderer_t base;

    coding_plan_data_t data;
    bool               has_data;

    const lv_font_t *font;
    const lv_font_t *title_font;
    int                view_mode; /* 0 = 7-day total, 1 = per-model breakdown */
} coding_plan_page_t;

/* PageRenderer vtable entry points. */
void coding_plan_page_init(page_renderer_t *self, int width, int height);
void coding_plan_page_render(page_renderer_t *self, uint8_t *fb, int width, int height);
bool coding_plan_page_handle_input(page_renderer_t *self, const ui_button_event_t *event);

/* Data interface. */
void coding_plan_page_update(page_renderer_t *self, const coding_plan_data_t *data);

#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_UI_PAGES_CODING_PLAN_PAGE_H_ */
