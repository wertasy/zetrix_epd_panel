#ifndef MAIN_SLEEP_MANAGER_H_
#define MAIN_SLEEP_MANAGER_H_

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    SLEEP_BUSY_SRC_NET      = 1u << 0,
    SLEEP_BUSY_SRC_AUDIO    = 1u << 1,
    SLEEP_BUSY_SRC_DISPLAY  = 1u << 2,
    SLEEP_BUSY_SRC_UI       = 1u << 3,
    SLEEP_BUSY_SRC_NVS      = 1u << 4,
    SLEEP_BUSY_SRC_TODO     = 1u << 5,
    SLEEP_BUSY_SRC_PROTOCOL = 1u << 6,
} sleep_busy_src_t;

// Standard C API for Sleep Manager
void sm_set_busy(sleep_busy_src_t src, bool busy);
void sm_kick(uint32_t delay_ms, const char *reason);
void sm_hold(const char *reason);
void sm_release(const char *reason);
void sg_release(const char *reason); // Alias
bool sm_can_sleep_now(void);

#endif // MAIN_SLEEP_MANAGER_H_
