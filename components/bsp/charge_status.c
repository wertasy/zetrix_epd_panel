#include "charge_status.h"
#include "board.h"
#include <stdatomic.h>
#include <driver/gpio.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali_scheme.h>
#include "config.h"

static const char *BATT_TAG = "battery";

#define POWER_PRESENT_HOLD_MS 1000
#define STABLE_HIGH_MS 400
#define ALT_WINDOW_MS 1500
/* ---- Battery ADC (Li-ion voltage → percent) ----
 *
 * Hardware note: VBAT_PWR_PIN (GPIO17) is the power-enable OUTPUT for the
 * battery measurement circuit (a 2:1 voltage divider). The actual ADC
 * measurement channel is ADC1_CH3, matching the original reference firmware.
 * The divider halves the battery voltage (up to 4.2V → ~2.1V at the ADC),
 * so calibrated readings are multiplied by 2 to recover true battery voltage.
 *
 * Nonlinear Li-ion discharge curve LUT (voltage_mV → percent):
 *   4.20V→100%  4.00V→80%  3.80V→60%  3.70V→50%
 *   3.60V→40%   3.50V→20%  3.40V→5%   3.30V→0%
 */
#define BATTERY_ADC_UNIT ADC_UNIT_1
#define BATTERY_ADC_CHANNEL ADC_CHANNEL_3
#define BATTERY_ADC_ATTEN ADC_ATTEN_DB_12
#define BATTERY_ADC_BITWIDTH ADC_BITWIDTH_12
#define BATTERY_DIVIDER_MULT 2 /* recovered voltage = adc_mV * 2 */
#define BATTERY_SAMPLE_COUNT 10

typedef struct {
    int mv;
    int pct;
} battery_point_t;

static const battery_point_t battery_lut[] = {
    {4200, 100}, {4000, 80}, {3800, 60}, {3700, 50}, {3600, 40}, {3500, 20}, {3400, 5}, {3300, 0},
};
#define BATTERY_LUT_LEN (sizeof(battery_lut) / sizeof(battery_lut[0]))

static bool                      s_batt_initialized = false;
static adc_oneshot_unit_handle_t s_batt_adc_handle  = NULL;
static adc_cali_handle_t         s_batt_cali_handle = NULL;

static bool battery_adc_init(void)
{
    if (s_batt_initialized)
        return true;

    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id  = BATTERY_ADC_UNIT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    if (adc_oneshot_new_unit(&init_cfg, &s_batt_adc_handle) != ESP_OK) {
        ESP_LOGE(BATT_TAG, "failed to init ADC unit");
        return false;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = BATTERY_ADC_ATTEN,
        .bitwidth = BATTERY_ADC_BITWIDTH,
    };
    if (adc_oneshot_config_channel(s_batt_adc_handle, BATTERY_ADC_CHANNEL, &chan_cfg) != ESP_OK) {
        ESP_LOGE(BATT_TAG, "failed to config ADC channel");
        adc_oneshot_del_unit(s_batt_adc_handle);
        s_batt_adc_handle = NULL;
        return false;
    }

    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id  = BATTERY_ADC_UNIT,
        .chan     = BATTERY_ADC_CHANNEL,
        .atten    = BATTERY_ADC_ATTEN,
        .bitwidth = BATTERY_ADC_BITWIDTH,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_batt_cali_handle) != ESP_OK) {
        ESP_LOGW(BATT_TAG, "ADC calibration unavailable; using raw values");
        s_batt_cali_handle = NULL;
    }

    s_batt_initialized = true;
    ESP_LOGI(BATT_TAG, "ADC initialized (ADC1_CH%d, 2x divider)", BATTERY_ADC_CHANNEL);
    return true;
}

/* Linear-interpolate the Li-ion LUT. Voltage in mV, returns 0–100. */
static int battery_voltage_to_percent(int voltage_mv)
{
    if (voltage_mv >= battery_lut[0].mv)
        return battery_lut[0].pct;
    if (voltage_mv <= battery_lut[BATTERY_LUT_LEN - 1].mv)
        return battery_lut[BATTERY_LUT_LEN - 1].pct;

    for (size_t i = 0; i < BATTERY_LUT_LEN - 1; i++) {
        if (voltage_mv <= battery_lut[i].mv && voltage_mv >= battery_lut[i + 1].mv) {
            const int dv   = battery_lut[i].mv - battery_lut[i + 1].mv;
            const int dp   = battery_lut[i].pct - battery_lut[i + 1].pct;
            const int frac = voltage_mv - battery_lut[i + 1].mv;
            return battery_lut[i + 1].pct + (frac * dp) / dv;
        }
    }
    return 0;
}

static uint32_t pack_snapshot(charge_state_t state, bool power_present, bool charging, bool full, bool no_battery)
{
    return (uint32_t)state | ((uint32_t)power_present << 8) | ((uint32_t)charging << 9) | ((uint32_t)full << 10) |
           ((uint32_t)no_battery << 11);
}

static charge_snapshot_t unpack_snapshot(uint32_t v)
{
    charge_snapshot_t s = {0};
    s.state             = (charge_state_t)(v & 0xFF);
    s.power_present     = (v >> 8) & 0x1;
    s.charging          = (v >> 9) & 0x1;
    s.full              = (v >> 10) & 0x1;
    s.no_battery        = (v >> 11) & 0x1;
    return s;
}

static void update_snapshot(charge_status_t *self, charge_state_t state, bool power_present, bool full, bool no_battery)
{
    bool     charging = (state == CHARGE_STATE_CHARGING || state == CHARGE_STATE_NO_BATTERY);
    uint32_t packed   = pack_snapshot(state, power_present, charging, full, no_battery);
    uint32_t old      = atomic_exchange(&self->snapshot, packed);
    if (old != packed && self->on_state_changed) {
        charge_snapshot_t snap = unpack_snapshot(packed);
        self->on_state_changed(&snap, self->callback_user_data);
    }
}

void charge_status_init(charge_status_t *self, gpio_num_t detect, gpio_num_t full, int64_t now_ms)
{
    self->detect_gpio           = detect;
    self->full_gpio             = full;
    self->detect_high_start_ms  = -1;
    self->full_high_start_ms    = -1;
    self->last_detect_seen_ms   = -1;
    self->last_full_seen_ms     = -1;
    self->last_power_present_ms = -1;
    self->on_state_changed      = NULL;
    self->callback_user_data    = NULL;

    gpio_config_t cfg = {
        .intr_type    = GPIO_INTR_DISABLE,
        .mode         = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << self->detect_gpio) | (1ULL << self->full_gpio),
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&cfg));

    atomic_store(&self->snapshot, pack_snapshot(CHARGE_STATE_NO_POWER, false, false, false, false));
    charge_status_tick(self, now_ms);
}

void charge_status_on_state_changed(charge_status_t *self, charge_state_changed_cb_t cb, void *user_data)
{
    self->on_state_changed   = cb;
    self->callback_user_data = user_data;
}

void charge_status_tick(charge_status_t *self, int64_t now_ms)
{
    const bool detect_high = gpio_get_level(self->detect_gpio) == CHARGE_DETECT_CHARGING_LEVEL;
    const bool full_high   = gpio_get_level(self->full_gpio) == 1;

    if (detect_high) {
        self->last_power_present_ms = now_ms;
        self->last_detect_seen_ms   = now_ms;
        if (self->detect_high_start_ms < 0) {
            self->detect_high_start_ms = now_ms;
        }
    } else {
        self->detect_high_start_ms = -1;
    }

    if (full_high) {
        self->last_power_present_ms = now_ms;
        self->last_full_seen_ms     = now_ms;
        if (self->full_high_start_ms < 0) {
            self->full_high_start_ms = now_ms;
        }
    } else {
        self->full_high_start_ms = -1;
    }

    const bool power_present =
        (self->last_power_present_ms >= 0) && ((now_ms - self->last_power_present_ms) <= POWER_PRESENT_HOLD_MS);

    const bool detect_stable =
        (self->detect_high_start_ms >= 0) && ((now_ms - self->detect_high_start_ms) >= STABLE_HIGH_MS);
    const bool full_stable = (self->full_high_start_ms >= 0) && ((now_ms - self->full_high_start_ms) >= STABLE_HIGH_MS);

    const bool alt_seen = power_present && (self->last_detect_seen_ms >= 0) && (self->last_full_seen_ms >= 0) &&
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

charge_snapshot_t charge_status_get(const charge_status_t *self)
{
    uint32_t val = atomic_load(&((charge_status_t *)self)->snapshot);
    return unpack_snapshot(val);
}

int charge_status_get_battery_percent(void)
{
    if (!battery_adc_init())
        return -1;

    int sum_mv = 0;
    int valid  = 0;
    for (int i = 0; i < BATTERY_SAMPLE_COUNT; i++) {
        int raw = 0;
        int mv  = 0;
        if (adc_oneshot_read(s_batt_adc_handle, BATTERY_ADC_CHANNEL, &raw) != ESP_OK)
            continue;
        if (s_batt_cali_handle) {
            if (adc_cali_raw_to_voltage(s_batt_cali_handle, raw, &mv) != ESP_OK)
                continue;
        } else {
            mv = raw;
        }
        sum_mv += mv * BATTERY_DIVIDER_MULT;
        valid++;
    }

    if (valid == 0) {
        ESP_LOGW(BATT_TAG, "ADC read failed for all %d samples", BATTERY_SAMPLE_COUNT);
        return -1;
    }

    const int avg_mv  = sum_mv / valid;
    const int percent = battery_voltage_to_percent(avg_mv);
    ESP_LOGD(BATT_TAG, "battery: %d mV → %d%% (samples=%d)", avg_mv, percent, valid);
    return percent;
}

bool charge_status_is_charging(void)
{
    if (!g_board.charge_status)
        return false;
    return charge_status_get(g_board.charge_status).charging;
}
