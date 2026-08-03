#include "charge_status.h"
#include <stdatomic.h>
#include <driver/gpio.h>
#include <esp_err.h>
#include "config.h"

#define POWER_PRESENT_HOLD_MS 1000
#define STABLE_HIGH_MS 400
#define ALT_WINDOW_MS 1500

static uint32_t pack_snapshot(charge_state_t state, bool power_present, bool charging, bool full, bool no_battery) {
    return (uint32_t)state |
        ((uint32_t)power_present << 8) |
        ((uint32_t)charging << 9) |
        ((uint32_t)full << 10) |
        ((uint32_t)no_battery << 11);
}

static charge_snapshot_t unpack_snapshot(uint32_t v) {
    charge_snapshot_t s = {0};
    s.state = (charge_state_t)(v & 0xFF);
    s.power_present = (v >> 8) & 0x1;
    s.charging = (v >> 9) & 0x1;
    s.full = (v >> 10) & 0x1;
    s.no_battery = (v >> 11) & 0x1;
    return s;
}

static void update_snapshot(charge_status_t* self, charge_state_t state, bool power_present, bool full, bool no_battery) {
    bool charging = (state == CHARGE_STATE_CHARGING || state == CHARGE_STATE_NO_BATTERY);
    uint32_t packed = pack_snapshot(state, power_present, charging, full, no_battery);
    uint32_t old = atomic_exchange(&self->snapshot, packed);
    if (old != packed && self->on_state_changed) {
        charge_snapshot_t snap = unpack_snapshot(packed);
        self->on_state_changed(&snap, self->callback_user_data);
    }
}

void charge_status_init(charge_status_t* self, gpio_num_t detect, gpio_num_t full, int64_t now_ms) {
    self->detect_gpio = detect;
    self->full_gpio = full;
    self->detect_high_start_ms = -1;
    self->full_high_start_ms = -1;
    self->last_detect_seen_ms = -1;
    self->last_full_seen_ms = -1;
    self->last_power_present_ms = -1;
    self->on_state_changed = NULL;
    self->callback_user_data = NULL;

    gpio_config_t cfg = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << self->detect_gpio) | (1ULL << self->full_gpio),
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&cfg));

    atomic_store(&self->snapshot, pack_snapshot(CHARGE_STATE_NO_POWER, false, false, false, false));
    charge_status_tick(self, now_ms);
}

void charge_status_on_state_changed(charge_status_t* self, charge_state_changed_cb_t cb, void* user_data) {
    self->on_state_changed = cb;
    self->callback_user_data = user_data;
}

void charge_status_tick(charge_status_t* self, int64_t now_ms) {
    const bool detect_high = gpio_get_level(self->detect_gpio) == CHARGE_DETECT_CHARGING_LEVEL;
    const bool full_high = gpio_get_level(self->full_gpio) == 1;

    if (detect_high) {
        self->last_power_present_ms = now_ms;
        self->last_detect_seen_ms = now_ms;
        if (self->detect_high_start_ms < 0) {
            self->detect_high_start_ms = now_ms;
        }
    } else {
        self->detect_high_start_ms = -1;
    }

    if (full_high) {
        self->last_power_present_ms = now_ms;
        self->last_full_seen_ms = now_ms;
        if (self->full_high_start_ms < 0) {
            self->full_high_start_ms = now_ms;
        }
    } else {
        self->full_high_start_ms = -1;
    }

    const bool power_present = (self->last_power_present_ms >= 0) &&
        ((now_ms - self->last_power_present_ms) <= POWER_PRESENT_HOLD_MS);

    const bool detect_stable = (self->detect_high_start_ms >= 0) &&
        ((now_ms - self->detect_high_start_ms) >= STABLE_HIGH_MS);
    const bool full_stable = (self->full_high_start_ms >= 0) &&
        ((now_ms - self->full_high_start_ms) >= STABLE_HIGH_MS);

    const bool alt_seen = power_present &&
        (self->last_detect_seen_ms >= 0) && (self->last_full_seen_ms >= 0) &&
        ((now_ms - self->last_detect_seen_ms) <= ALT_WINDOW_MS) &&
        ((now_ms - self->last_full_seen_ms) <= ALT_WINDOW_MS);

    const bool no_battery = alt_seen && !detect_stable && !full_stable;

    charge_state_t state = CHARGE_STATE_NO_POWER;
    if (!power_present) {
        state = CHARGE_STATE_NO_POWER;
    } else if (full_stable && !no_battery) {
        state = CHARGE_STATE_FULL;
    } else if (detect_stable || no_battery) {
        state = no_battery ? CHARGE_STATE_NO_BATTERY : CHARGE_STATE_CHARGING;
    } else {
        state = CHARGE_STATE_CHARGING;
    }

    update_snapshot(self, state, power_present, state == CHARGE_STATE_FULL, no_battery);
}

charge_snapshot_t charge_status_get(const charge_status_t* self) {
    uint32_t val = atomic_load(&((charge_status_t*)self)->snapshot);
    return unpack_snapshot(val);
}
