#ifndef COMPONENTS_UI_INCLUDE_PAGE_RUNTIME_H_
#define COMPONENTS_UI_INCLUDE_PAGE_RUNTIME_H_

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "ui_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PAGE_WAKE_ALIGN_NONE = 0,     /* Sleep time + interval */
    PAGE_WAKE_ALIGN_MIDNIGHT,     /* Align to next 00:01 (e.g. calendar) */
} page_wake_align_t;

typedef enum {
    PAGE_DATA_NONE        = 0,
    PAGE_DATA_WEATHER     = 1u << 0,
    PAGE_DATA_CODING_PLAN = 1u << 1,
    PAGE_DATA_HOLIDAY     = 1u << 2,
    PAGE_DATA_SNTP        = 1u << 3,
} page_data_interest_t;

/* Page-owned background services (mask for policy.services). The canonical
 * bit values are defined here so the ui component stays independent of
 * main/; main's app_service_id_t aliases these bits. */
typedef enum {
    PAGE_SVC_NONE        = 0,
    PAGE_SVC_AP_TRANSFER = 1u << 0, /* ap_transfer_server AP mode  */
    PAGE_SVC_LAN_HTTP    = 1u << 1, /* ap_transfer_server LAN mode */
} page_service_id_t;

typedef struct page_runtime_policy_t {
    uint16_t wake_interval_min;   /* 0 = fallback to default sync_interval */
    uint8_t  wake_align;          /* page_wake_align_t; if set, dominates interval */
    uint32_t data_interests;      /* PAGE_DATA_* mask */
    bool     needs_network_on_wake;/* false = skip WiFi on wakeup (like gallery slideshow) */
    uint32_t services;            /* PAGE_SVC_* mask, owned by page */
    uint16_t periodic_refresh_s;  /* 0 = no periodic refresh (replaces 30min counter) */
    void (*on_rtc_wake)(ui_page_id_t page); /* RTC wakeup hook */
} page_runtime_policy_t;

/* Policy queries with default fallback */
const page_runtime_policy_t *page_runtime_policy(ui_page_id_t page);
uint32_t page_runtime_effective_interests(ui_page_id_t page);
int      page_runtime_effective_wake_interval_min(ui_page_id_t page);
bool     page_runtime_effective_network_on_wake(ui_page_id_t page);
int      page_runtime_effective_periodic_refresh_s(ui_page_id_t page);
int      page_runtime_effective_wake_interval_override_min(ui_page_id_t page);
void     page_runtime_set_wake_interval_override(ui_page_id_t page, int minutes);
void     page_runtime_mark_day_served(uint32_t ymd);
uint32_t page_runtime_last_served_day(void);
int      page_runtime_midnight_alarm_target(const struct tm *now, struct tm *out, bool *catch_up);

/* Time-invalid retry bookkeeping (rtc-time-validity plan §3.4). Bounded
 * timer retries after an unattended wake that finds no valid time source;
 * any boot with a valid time clears the counter. Persisted in RTC_NOINIT
 * on target (survives deep sleep), plain static on host. */
#define PAGE_RUNTIME_TIME_RETRY_MAX 3
#define TIME_INVALID_RETRY_MINUTES 5
uint32_t page_runtime_time_retry_count(void);
void     page_runtime_time_retry_increment(void);
void     page_runtime_time_retry_clear(void);

/* Page active / freeze status */
bool     page_runtime_is_page_active(ui_page_id_t page);
void     page_runtime_on_page_entered(ui_page_id_t page);
void     page_runtime_on_page_exited(ui_page_id_t page);
void     page_runtime_init(void);
uint32_t page_runtime_pending_interests(void);
void     page_runtime_set_pending(uint32_t bits);
void     page_runtime_clear_pending(uint32_t bits);

#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_UI_INCLUDE_PAGE_RUNTIME_H_ */
