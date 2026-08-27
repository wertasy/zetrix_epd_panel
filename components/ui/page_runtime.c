#include "page_runtime.h"
#include "page_registry.h"
#include <stddef.h>
#include <stdatomic.h>
#include <time.h>

#ifdef ESP_PLATFORM
#    include <esp_log.h>
#else
#    include <stdio.h>
#    define ESP_LOGI(tag, fmt, ...) printf("[%s][I] " fmt "\n", tag, ##__VA_ARGS__)
#    define ESP_LOGW(tag, fmt, ...) printf("[%s][W] " fmt "\n", tag, ##__VA_ARGS__)
#    define ESP_LOGE(tag, fmt, ...) printf("[%s][E] " fmt "\n", tag, ##__VA_ARGS__)
#endif

#define TAG "PageRuntime"
static _Atomic ui_page_id_t s_active_page = UI_PAGE_GALLERY;
static _Atomic uint32_t s_pending_interests = 0;
/* Per-page runtime wake-interval override (minutes, 0 = none). The gallery
 * slideshow interval is written here by ui_manager; deep-sleep wake replays
 * it from NVS. Small enough to live as a plain array guarded by the
 * single-threaded UI/main-loop access pattern. */
static uint16_t s_wake_override_min[UI_PAGE_COUNT];
/* Day-boundary bookkeeping for MIDNIGHT-aligned pages (ymd-encoded). On
 * target this lives in RTC_NOINIT (survives deep sleep); on host it is a
 * plain static so the logic stays testable. */
#ifdef ESP_PLATFORM
static RTC_NOINIT_ATTR uint32_t s_served_day;
static RTC_NOINIT_ATTR uint32_t s_served_day_magic;
#else
static uint32_t s_served_day;
static uint32_t s_served_day_magic;
#endif
#define SERVED_DAY_MAGIC 0xD1A5EEDU

#ifdef ESP_PLATFORM
static RTC_NOINIT_ATTR uint32_t s_time_retry_count;
static RTC_NOINIT_ATTR uint32_t s_time_retry_magic;
#else
static uint32_t s_time_retry_count;
static uint32_t s_time_retry_magic;
#endif
#define TIME_RETRY_MAGIC 0x71E5EED1U

/* Time-invalid retry counter: incremented on each timer-retry wake that
 * still finds no valid time source; cleared by any valid-time boot.
 * Accessors validate the magic themselves so they are safe to call
 * before page_runtime_init() (main.c's early-exit path runs first). */
uint32_t page_runtime_time_retry_count(void)
{
    return s_time_retry_magic == TIME_RETRY_MAGIC ? s_time_retry_count : 0;
}

void page_runtime_time_retry_increment(void)
{
    if (s_time_retry_magic != TIME_RETRY_MAGIC) {
        s_time_retry_count = 0;
        s_time_retry_magic = TIME_RETRY_MAGIC;
    }
    if (s_time_retry_count < 0xFFFFFFFFu)
        s_time_retry_count++;
    ESP_LOGW(TAG, "Time retry count now %lu (max %d)", (unsigned long)s_time_retry_count,
             PAGE_RUNTIME_TIME_RETRY_MAX);
}

void page_runtime_time_retry_clear(void)
{
    const bool had = s_time_retry_magic == TIME_RETRY_MAGIC && s_time_retry_count != 0;
    s_time_retry_count = 0;
    s_time_retry_magic = TIME_RETRY_MAGIC;
    if (had)
        ESP_LOGI(TAG, "Time source valid; retry counter cleared");
}

static const page_runtime_policy_t s_default_policy = {
    .wake_interval_min = 0,
    .wake_align = PAGE_WAKE_ALIGN_NONE,
    .data_interests = PAGE_DATA_WEATHER | PAGE_DATA_CODING_PLAN | PAGE_DATA_HOLIDAY | PAGE_DATA_SNTP,
    .needs_network_on_wake = true,
    .services = 0,
    .periodic_refresh_s = 1800,
    .on_rtc_wake = NULL,
};

void page_runtime_init(void)
{
    atomic_store_explicit(&s_active_page, UI_PAGE_GALLERY, memory_order_relaxed);
    atomic_store_explicit(&s_pending_interests, 0, memory_order_relaxed);
    for (int i = 0; i < UI_PAGE_COUNT; i++) {
        s_wake_override_min[i] = 0;
    }
    if (s_served_day_magic != SERVED_DAY_MAGIC) {
        s_served_day = 0;
        s_served_day_magic = SERVED_DAY_MAGIC;
    }
    if (s_time_retry_magic != TIME_RETRY_MAGIC) {
        s_time_retry_count = 0;
        s_time_retry_magic = TIME_RETRY_MAGIC;
    }
    /* Log per-page policy resolution (default fallback vs. declared). */
    for (int i = 0; i < UI_PAGE_COUNT; i++) {
        const page_entry_t *e = page_registry_get_entry((ui_page_id_t)i);
        if (!e)
            continue;
        ESP_LOGI(TAG, "page %d (%s) policy=%s", i, e->name ? e->name : "?",
                 e->runtime_policy ? "declared" : "default");
    }
    ESP_LOGI(TAG, "Page runtime initialized.");
}

const page_runtime_policy_t *page_runtime_policy(ui_page_id_t page)
{
    if (page < 0 || page >= UI_PAGE_COUNT) {
        return &s_default_policy;
    }
    const page_entry_t *entry = page_registry_get_entry(page);
    if (entry && entry->runtime_policy) {
        return entry->runtime_policy;
    }
    return &s_default_policy;
}

uint32_t page_runtime_effective_interests(ui_page_id_t page)
{
    return page_runtime_policy(page)->data_interests;
}

int page_runtime_effective_wake_interval_min(ui_page_id_t page)
{
    return page_runtime_policy(page)->wake_interval_min;
}

bool page_runtime_effective_network_on_wake(ui_page_id_t page)
{
    return page_runtime_policy(page)->needs_network_on_wake;
}

int page_runtime_effective_periodic_refresh_s(ui_page_id_t page)
{
    return page_runtime_policy(page)->periodic_refresh_s;
}

int page_runtime_effective_wake_interval_override_min(ui_page_id_t page)
{
    if (page < 0 || page >= UI_PAGE_COUNT) {
        return 0;
    }
    return s_wake_override_min[page];
}

void page_runtime_set_wake_interval_override(ui_page_id_t page, int minutes)
{
    if (page < 0 || page >= UI_PAGE_COUNT || minutes < 0 || minutes > 0xFFFF) {
        return;
    }
    s_wake_override_min[page] = (uint16_t)minutes;
}

uint32_t page_runtime_last_served_day(void)
{
    return s_served_day_magic == SERVED_DAY_MAGIC ? s_served_day : 0;
}

void page_runtime_mark_day_served(uint32_t ymd)
{
    s_served_day = ymd;
    s_served_day_magic = SERVED_DAY_MAGIC;
}

static uint32_t ymd_of(const struct tm *t)
{
    return (uint32_t)(t->tm_year + 1900) * 10000u + (uint32_t)(t->tm_mon + 1) * 100u + (uint32_t)t->tm_mday;
}

int page_runtime_midnight_alarm_target(const struct tm *now, struct tm *out, bool *catch_up)
{
    if (!now || !out || !catch_up)
        return -1;
    /* Today's 00:01 boundary has passed once the clock is past 00:01. */
    const bool boundary_passed = (now->tm_hour > 0) || (now->tm_min >= 1);
    if (boundary_passed && page_runtime_last_served_day() != ymd_of(now)) {
        /* The day's content is stale (e.g. slept right after 00:00:30):
         * wake soon for a catch-up refresh instead of waiting until the
         * next midnight (up to ~24h staleness). */
        *catch_up = true;
        return 0;
    }
    /* Next 00:01, strictly future: today's when the clock is still before
     * 00:01, otherwise tomorrow's. */
    struct tm t = *now;
    t.tm_hour = 0;
    t.tm_min = 1;
    t.tm_sec = 0;
    if (boundary_passed) {
        t.tm_mday += 1; /* mktime normalises month/year overflow */
    }
    time_t tt = mktime(&t);
    if (tt == (time_t)-1)
        return -1;
    struct tm *norm = localtime(&tt);
    if (!norm)
        return -1;
    *out = *norm;
    *catch_up = false;
    return 0;
}

void page_runtime_on_page_entered(ui_page_id_t page)
{
    if (page >= 0 && page < UI_PAGE_COUNT) {
        atomic_store_explicit(&s_active_page, page, memory_order_relaxed);
        /* Set pending interests for the new page */
        page_runtime_set_pending(page_runtime_effective_interests(page));
    }
}

void page_runtime_on_page_exited(ui_page_id_t page)
{
    if (page >= 0 && page < UI_PAGE_COUNT) {
        /* Clear pending interests that were declared by the exited page */
        page_runtime_clear_pending(page_runtime_effective_interests(page));
    }
}

bool page_runtime_is_page_active(ui_page_id_t page)
{
    return atomic_load_explicit(&s_active_page, memory_order_relaxed) == page;
}

uint32_t page_runtime_pending_interests(void)
{
    ui_page_id_t active = atomic_load_explicit(&s_active_page, memory_order_relaxed);
    uint32_t pending = atomic_load_explicit(&s_pending_interests, memory_order_relaxed);
    return pending & page_runtime_effective_interests(active);
}

void page_runtime_set_pending(uint32_t bits)
{
    uint32_t expected = atomic_load_explicit(&s_pending_interests, memory_order_relaxed);
    uint32_t desired;
    do {
        desired = expected | bits;
    } while (!atomic_compare_exchange_weak_explicit(&s_pending_interests, &expected, desired,
                                                    memory_order_relaxed, memory_order_relaxed));
    ESP_LOGI(TAG, "Pending interests updated: +0x%lx (total: 0x%lx)", (unsigned long)bits, (unsigned long)desired);
}

void page_runtime_clear_pending(uint32_t bits)
{
    uint32_t expected = atomic_load_explicit(&s_pending_interests, memory_order_relaxed);
    uint32_t desired;
    do {
        desired = expected & ~bits;
    } while (!atomic_compare_exchange_weak_explicit(&s_pending_interests, &expected, desired,
                                                    memory_order_relaxed, memory_order_relaxed));
    ESP_LOGI(TAG, "Pending interests cleared: -0x%lx (total: 0x%lx)", (unsigned long)bits, (unsigned long)desired);
}
