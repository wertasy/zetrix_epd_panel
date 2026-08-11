#ifndef MAIN_CHARGE_STATUS_H_
#define MAIN_CHARGE_STATUS_H_

#include <driver/gpio.h>
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    CHARGE_STATE_NO_POWER   = 0,
    CHARGE_STATE_CHARGING   = 1,
    CHARGE_STATE_FULL       = 2,
    CHARGE_STATE_NO_BATTERY = 3,
} charge_state_t;

typedef struct {
    charge_state_t state;
    bool           power_present;
    bool           charging; // UI/LED charging indicator (includes no-battery)
    bool           full;
    bool           no_battery;
} charge_snapshot_t;

typedef void (*charge_state_changed_cb_t)(const charge_snapshot_t *snapshot, void *user_data);

typedef struct {
    gpio_num_t detect_gpio;
    gpio_num_t full_gpio;

    int64_t detect_high_start_ms;
    int64_t full_high_start_ms;
    int64_t last_detect_seen_ms;
    int64_t last_full_seen_ms;
    int64_t last_power_present_ms;

    _Atomic uint32_t snapshot;

    charge_state_changed_cb_t on_state_changed;
    void                     *callback_user_data;
} charge_status_t;

void              charge_status_init(charge_status_t *self, gpio_num_t detect, gpio_num_t full, int64_t now_ms);
void              charge_status_tick(charge_status_t *self, int64_t now_ms);
charge_snapshot_t charge_status_get(const charge_status_t *self);
int               charge_status_get_battery_percent(void);
bool              charge_status_is_charging(void);
void              charge_status_on_state_changed(charge_status_t *self, charge_state_changed_cb_t cb, void *user_data);

#endif // MAIN_CHARGE_STATUS_H_
