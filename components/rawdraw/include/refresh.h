#ifndef COMPONENTS_RAWDRAW_INCLUDE_REFRESH_H_
#define COMPONENTS_RAWDRAW_INCLUDE_REFRESH_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef ESP_PLATFORM
#    include <esp_timer.h>
#else
#    include <sys/time.h>
#    include <stddef.h>
static inline int64_t esp_timer_get_time(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000LL + (int64_t)tv.tv_usec;
}
#endif

typedef struct {
    int64_t last_refresh_us; /* Last refresh timestamp (microseconds) */
    int     partial_count; /* Consecutive partial refreshes (0-100) */
    bool    dirty; /* Region needs refresh */
    bool    needs_full; /* Full refresh required (counter expired) */
} region_refresh_t;

static inline void refresh_tracker_init(region_refresh_t *tracker)
{
    if (!tracker)
        return;
    tracker->last_refresh_us = 0;
    tracker->partial_count   = 0;
    tracker->dirty           = false;
    tracker->needs_full      = false;
}

static inline bool refresh_should_refresh(const region_refresh_t *tracker, int64_t now_us, int64_t min_interval_ms)
{
    if (!tracker)
        return false;
    if (tracker->partial_count >= 100)
        return true;
    if (now_us - tracker->last_refresh_us < min_interval_ms * 1000) {
        return false;
    }
    return tracker->dirty;
}

static inline void refresh_mark_dirty(region_refresh_t *tracker)
{
    if (tracker)
        tracker->dirty = true;
}

static inline void refresh_mark_clean(region_refresh_t *tracker)
{
    if (tracker)
        tracker->dirty = false;
}

static inline void refresh_update_counter(region_refresh_t *tracker, int64_t now_us)
{
    if (!tracker)
        return;
    tracker->partial_count++;
    tracker->last_refresh_us = now_us;
    tracker->dirty           = false;
    if (tracker->partial_count >= 100) {
        tracker->needs_full = true;
    }
}

static inline void refresh_reset_counter(region_refresh_t *tracker)
{
    if (!tracker)
        return;
    tracker->partial_count = 0;
    tracker->needs_full    = false;
}

static inline int refresh_get_partial_count(const region_refresh_t *tracker)
{
    return tracker ? tracker->partial_count : 0;
}

static inline bool refresh_needs_full(const region_refresh_t *tracker)
{
    return tracker && tracker->needs_full;
}

#endif /* COMPONENTS_RAWDRAW_INCLUDE_REFRESH_H_ */
