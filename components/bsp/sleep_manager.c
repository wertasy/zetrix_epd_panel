#include "sleep_manager.h"
#include <stdatomic.h>
#include <esp_timer.h>

static _Atomic uint32_t g_busy_mask   = 0;
static _Atomic int      g_hold_count  = 0;
static _Atomic int64_t  g_deadline_ms = 0;

static inline int64_t get_now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

void sm_set_busy(sleep_busy_src_t src, bool busy)
{
    uint32_t bit = (uint32_t)src;
    if (busy) {
        atomic_fetch_or(&g_busy_mask, bit);
    } else {
        atomic_fetch_and(&g_busy_mask, ~bit);
    }
}

void sm_kick(uint32_t delay_ms, const char *reason)
{
    int64_t new_deadline = get_now_ms() + (int64_t)delay_ms;
    int64_t cur          = atomic_load(&g_deadline_ms);
    while (cur < new_deadline) {
        if (atomic_compare_exchange_weak(&g_deadline_ms, &cur, new_deadline)) {
            break;
        }
    }
}

void sm_hold(const char *reason)
{
    atomic_fetch_add(&g_hold_count, 1);
}

void sm_release(const char *reason)
{
    int cur = atomic_load(&g_hold_count);
    while (true) {
        if (cur <= 0) {
            atomic_store(&g_hold_count, 0);
            return;
        }
        if (atomic_compare_exchange_weak(&g_hold_count, &cur, cur - 1)) {
            return;
        }
    }
}

void sg_release(const char *reason)
{
    sm_release(reason);
}

bool sm_can_sleep_now(void)
{
    if (atomic_load(&g_busy_mask) != 0) {
        return false;
    }
    if (atomic_load(&g_hold_count) > 0) {
        return false;
    }
    if (get_now_ms() < atomic_load(&g_deadline_ms)) {
        return false;
    }
    return true;
}
