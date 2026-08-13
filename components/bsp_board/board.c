#include "board.h"
#include <stdatomic.h>
#include <assert.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_err.h>
#include <driver/gpio.h>
#include <driver/i2c_master.h>

board_context_t g_board = {0};
/* Periodic charge-status sampler. Runs at a fixed 100ms interval independent
 * of the LED task's variable sleep schedule, ensuring precise debounce timing
 * for charge-detect / charge-full state transitions. */
static esp_timer_handle_t s_charge_tick_timer = NULL;

static void charge_tick_callback(void *arg)
{
    (void)arg;
    if (g_board.charge_status) {
        charge_status_tick(g_board.charge_status, esp_timer_get_time() / 1000);
    }
}

static void power_led_task(void *arg)
{
    gpio_config_t led_conf = {
        .intr_type    = GPIO_INTR_DISABLE,
        .mode         = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << GPIO_NUM_3),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&led_conf));

    for (;;) {
        if (atomic_load(&g_board.led_override_enabled)) {
            const bool blink = atomic_load(&g_board.led_override_blink);
            if (blink) {
                const bool phase = !atomic_load(&g_board.led_override_phase);
                atomic_store(&g_board.led_override_phase, phase);
                gpio_hold_dis(GPIO_NUM_3);
                gpio_set_level(GPIO_NUM_3, phase ? 0 : 1);
                gpio_hold_en(GPIO_NUM_3);
                vTaskDelay(pdMS_TO_TICKS(500));
                continue;
            }
            gpio_hold_dis(GPIO_NUM_3);
            gpio_set_level(GPIO_NUM_3, 1);
            gpio_hold_en(GPIO_NUM_3);
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        charge_snapshot_t snap       = {0};
        const bool        has_status = (g_board.charge_status != NULL);
        if (has_status) {
            snap = charge_status_get(g_board.charge_status);
        }
        gpio_hold_dis(GPIO_NUM_3);
        int pulses = atomic_load(&g_board.led_activity_pulses);
        if ((!has_status || (!snap.charging && !snap.full)) && pulses > 0) {
            atomic_store(&g_board.led_activity_pulses, pulses - 1);
            gpio_set_level(GPIO_NUM_3, 0);
            vTaskDelay(pdMS_TO_TICKS(120));
            gpio_set_level(GPIO_NUM_3, 1);
            gpio_hold_en(GPIO_NUM_3);
            vTaskDelay(pdMS_TO_TICKS(180));
        } else if (has_status && snap.full) {
            gpio_set_level(GPIO_NUM_3, 0);
            gpio_hold_en(GPIO_NUM_3);
            vTaskDelay(pdMS_TO_TICKS(500));
        } else if (has_status && snap.charging) {
            gpio_set_level(GPIO_NUM_3, 0);
            vTaskDelay(pdMS_TO_TICKS(200));
            gpio_set_level(GPIO_NUM_3, 1);
            gpio_hold_en(GPIO_NUM_3);
            vTaskDelay(pdMS_TO_TICKS(2800));
        } else {
            gpio_set_level(GPIO_NUM_3, 1);
            gpio_hold_en(GPIO_NUM_3);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
}

void board_init(charge_status_t *charge_status)
{
    g_board.charge_status = charge_status;
    atomic_store(&g_board.led_override_enabled, false);
    atomic_store(&g_board.led_override_blink, false);
    atomic_store(&g_board.led_override_phase, false);
    atomic_store(&g_board.led_activity_pulses, 0);

    // 1. Configure power rail GPIOs
    gpio_config_t gpio_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode      = GPIO_MODE_OUTPUT,
        .pin_bit_mask =
            (1ULL << EPD_PWR_PIN) | (1ULL << Audio_PWR_PIN) | (1ULL << Audio_AMP_PIN) | (1ULL << VBAT_PWR_PIN),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&gpio_conf));

    // 2. Initialize I2C Master Bus
    i2c_master_bus_config_t i2c_bus_cfg = {
        .i2c_port                     = (i2c_port_t)0,
        .sda_io_num                   = AUDIO_CODEC_I2C_SDA_PIN,
        .scl_io_num                   = AUDIO_CODEC_I2C_SCL_PIN,
        .clk_source                   = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt            = 7,
        .intr_priority                = 0,
        .trans_queue_depth            = 0,
        .flags.enable_internal_pullup = 1,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &g_board.i2c_bus));

    // Create I2C access mutex
    g_board.i2c_mutex = xSemaphoreCreateMutex();
    assert(g_board.i2c_mutex != NULL);

    // Add PCF8563 RTC device
    i2c_device_config_t rtc_device_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = RTC_I2C_ADDR,
        .scl_speed_hz    = 400 * 1000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(g_board.i2c_bus, &rtc_device_cfg, &g_board.rtc_device));

    // Add GT23SC6699 NFC device
    i2c_device_config_t nfc_device_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = NFC_I2C_ADDR,
        .scl_speed_hz    = 400 * 1000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(g_board.i2c_bus, &nfc_device_cfg, &g_board.nfc_device));

    // Add ES8311 Audio Codec device
    i2c_device_config_t codec_device_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = AUDIO_CODEC_ES8311_ADDR,
        .scl_speed_hz    = 400 * 1000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(g_board.i2c_bus, &codec_device_cfg, &g_board.codec_device));

    // 3. Create LED status background task
    xTaskCreatePinnedToCore(power_led_task, "PowerLedTask", 3 * 1024, NULL, 2, &g_board.led_task, 0);

    // 4. Create fixed 100ms periodic charge-status sampler (independent of the
    //    variable-sleep LED task, so debounce timing stays accurate).
    const esp_timer_create_args_t charge_timer_args = {
        .callback              = charge_tick_callback,
        .arg                   = NULL,
        .dispatch_method       = ESP_TIMER_TASK,
        .name                  = "charge_tick",
        .skip_unhandled_events = true,
    };
    esp_err_t err = esp_timer_create(&charge_timer_args, &s_charge_tick_timer);
    if (err != ESP_OK) {
        ESP_LOGE("board", "Failed to create charge_tick timer: %s", esp_err_to_name(err));
    } else {
        err = esp_timer_start_periodic(s_charge_tick_timer, 100 * 1000ULL);
        if (err != ESP_OK) {
            ESP_LOGE("board", "Failed to start charge_tick timer: %s", esp_err_to_name(err));
        }
    }
}

void board_power_epd_on(void)
{
    gpio_hold_dis(EPD_PWR_PIN);
    gpio_set_level(EPD_PWR_PIN, 1);
    gpio_hold_en(EPD_PWR_PIN);
}

void board_power_epd_off(void)
{
    gpio_hold_dis(EPD_PWR_PIN);
    gpio_set_level(EPD_PWR_PIN, 0);
    gpio_hold_en(EPD_PWR_PIN);
}

void board_power_audio_on(void)
{
    gpio_hold_dis(Audio_PWR_PIN);
    gpio_set_level(Audio_PWR_PIN, 1);
    gpio_hold_en(Audio_PWR_PIN);
}

void board_power_audio_off(void)
{
    gpio_hold_dis(Audio_PWR_PIN);
    gpio_set_level(Audio_PWR_PIN, 0);
    gpio_hold_en(Audio_PWR_PIN);
}

void board_power_amp_on(void)
{
    gpio_hold_dis(Audio_AMP_PIN);
    gpio_set_level(Audio_AMP_PIN, 1);
    gpio_hold_en(Audio_AMP_PIN);
}

void board_power_amp_off(void)
{
    gpio_hold_dis(Audio_AMP_PIN);
    gpio_set_level(Audio_AMP_PIN, 0);
    gpio_hold_en(Audio_AMP_PIN);
}

void board_power_vbat_on(void)
{
    gpio_hold_dis(VBAT_PWR_PIN);
    gpio_set_level(VBAT_PWR_PIN, 1);
    gpio_hold_en(VBAT_PWR_PIN);
}

void board_power_vbat_off(void)
{
    gpio_hold_dis(VBAT_PWR_PIN);
    gpio_set_level(VBAT_PWR_PIN, 0);
    gpio_hold_en(VBAT_PWR_PIN);
}

void board_set_factory_led_override(bool enabled, bool blink)
{
    atomic_store(&g_board.led_override_enabled, enabled);
    atomic_store(&g_board.led_override_blink, blink);
    atomic_store(&g_board.led_override_phase, false);
}

void board_flash_activity_led(void)
{
    board_flash_activity_led_blink(1);
}

/* Pulse the activity LED `pulses` times (each pulse: 120ms on / 180ms off,
 * driven by power_led_task). Clamped to >= 1 so the LED always blinks. */
void board_flash_activity_led_blink(int pulses)
{
    if (pulses < 1) {
        pulses = 1;
    }
    atomic_store(&g_board.led_activity_pulses, pulses);
}

esp_err_t board_i2c_write_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t value)
{
    if (!dev)
        return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(g_board.i2c_mutex, portMAX_DELAY);
    uint8_t   buffer[2] = {reg, value};
    esp_err_t ret       = i2c_master_transmit(dev, buffer, sizeof(buffer), 100);
    if (ret == ESP_ERR_INVALID_STATE || ret == ESP_ERR_TIMEOUT) {
        ESP_LOGW("I2C", "I2C write failed, resetting bus and retrying...");
        if (i2c_master_bus_reset(g_board.i2c_bus) == ESP_OK) {
            ret = i2c_master_transmit(dev, buffer, sizeof(buffer), 100);
        }
    }
    xSemaphoreGive(g_board.i2c_mutex);
    return ret;
}

uint8_t board_i2c_read_reg(i2c_master_dev_handle_t dev, uint8_t reg)
{
    uint8_t value = 0;
    board_i2c_read_regs(dev, reg, &value, 1);
    return value;
}

esp_err_t board_i2c_read_regs(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *buffer, size_t length)
{
    if (!dev || !buffer)
        return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(g_board.i2c_mutex, portMAX_DELAY);
    esp_err_t ret = i2c_master_transmit_receive(dev, &reg, 1, buffer, length, 100);
    if (ret == ESP_ERR_INVALID_STATE || ret == ESP_ERR_TIMEOUT) {
        ESP_LOGW("I2C", "I2C read failed, resetting bus and retrying...");
        if (i2c_master_bus_reset(g_board.i2c_bus) == ESP_OK) {
            ret = i2c_master_transmit_receive(dev, &reg, 1, buffer, length, 100);
        }
    }
    xSemaphoreGive(g_board.i2c_mutex);
    return ret;
}
