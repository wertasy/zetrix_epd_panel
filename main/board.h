#ifndef MAIN_BOARD_H_
#define MAIN_BOARD_H_

#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "charge_status.h"
#include "config.h"

typedef struct {
    i2c_master_bus_handle_t i2c_bus;
    i2c_master_dev_handle_t rtc_device;
    i2c_master_dev_handle_t nfc_device;
    i2c_master_dev_handle_t codec_device;
    SemaphoreHandle_t i2c_mutex;
    charge_status_t* charge_status;
    TaskHandle_t led_task;
    _Atomic bool led_override_enabled;
    _Atomic bool led_override_blink;
    _Atomic bool led_override_phase;
    _Atomic int led_activity_pulses;
} board_context_t;

extern board_context_t g_board;

esp_err_t board_i2c_write_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t value);
uint8_t board_i2c_read_reg(i2c_master_dev_handle_t dev, uint8_t reg);
esp_err_t board_i2c_read_regs(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t* buffer, size_t length);

void board_init(charge_status_t* charge_status);
void board_power_epd_on(void);
void board_power_epd_off(void);
void board_power_audio_on(void);
void board_power_audio_off(void);
void board_power_amp_on(void);
void board_power_amp_off(void);
void board_power_vbat_on(void);
void board_power_vbat_off(void);
void board_set_factory_led_override(bool enabled, bool blink);
void board_flash_activity_led(void);

#endif // MAIN_BOARD_H_
