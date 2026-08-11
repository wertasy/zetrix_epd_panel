#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_sleep.h>
#include <freertos/timers.h>
#include <iot_button.h>
#include <button_gpio.h>
#include "nvs_state.h"

#include "config.h"
#include "board.h"
#include "charge_status.h"
#include "custom_lcd_display.h"
#include "rawdraw.h"
#include "rtc_pcf8563.h"
#include "zectrix_nfc.h"
#include "settings.h"
#include "audio_player.h"
#include "wifi_manager.h"
#include "bluetooth_manager.h"
#include "ble_gatt_service.h"
#include "ble_image_receiver.h"
#include "application.h"
#include "ui_manager.h"
static const char *TAG = "main_app";

static charge_status_t s_charge_status;
static TimerHandle_t   s_clock_timer = NULL;

/* ------------------------------------------------------------------ */
/* Button callbacks (routed to the Application singleton)              */
static void application_main_task(void *arg);

/* ------------------------------------------------------------------ */

static bool s_suppress_next_up_click   = false;
static bool s_suppress_next_down_click = false;

static void button_up_click_cb(void *arg, void *usr_data)
{
    (void)arg;
    (void)usr_data;
    if (s_suppress_next_up_click) {
        s_suppress_next_up_click = false;
        return;
    }
    application_on_up_click();
}

static void button_down_click_cb(void *arg, void *usr_data)
{
    (void)arg;
    (void)usr_data;
    if (s_suppress_next_down_click) {
        s_suppress_next_down_click = false;
        return;
    }
    application_on_down_click();
}
static void button_confirm_click_cb(void *arg, void *usr_data)
{
    (void)arg;
    (void)usr_data;
    application_on_boot_click();
}

static void button_up_long_press_cb(void *arg, void *usr_data)
{
    (void)arg;
    (void)usr_data;
    s_suppress_next_up_click = true;
    application_on_up_long_press();
}

static void button_down_long_press_cb(void *arg, void *usr_data)
{
    (void)arg;
    (void)usr_data;
    s_suppress_next_down_click = true;
    application_on_down_long_press();
}

static void button_boot_long_press_cb(void *arg, void *usr_data)
{
    (void)arg;
    (void)usr_data;
    application_on_boot_long_press();
}

static void button_boot_double_click_cb(void *arg, void *usr_data)
{
    (void)arg;
    (void)usr_data;
    application_on_boot_double_click();
}
static void button_up_double_click_cb(void *arg, void *usr_data)
{
    (void)arg;
    (void)usr_data;
    application_on_up_double_click();
}
static void button_down_double_click_cb(void *arg, void *usr_data)
{
    (void)arg;
    (void)usr_data;
    application_on_down_double_click();
}

static void clock_timer_callback(TimerHandle_t xTimer)
{
    (void)xTimer;
    /* No-op: the 4-color EPD avoids periodic refreshes; the UI manager's
     * PumpClockRefresh handles deferred page re-renders. */
}

/**
 * @brief Bridge BLE-completed images to photo_storage (Touch & Go push).
 *
 * Called by the GATT service when a phone finishes pushing a background image.
 */
static void ble_image_ready_cb(const uint8_t *data, uint16_t size, void *user_data)
{
    (void)user_data;
    ESP_LOGI(TAG, "BLE image ready: %u bytes", (unsigned)size);
    if (ble_image_receiver_save_to_storage() != 0) {
        ESP_LOGE(TAG, "Failed to save BLE image to storage");
    }
}

/**
 * @brief Adapter matching bluetooth_nfc_ndef_writer_t; forwards to the NFC
 *        driver's raw NDEF writer (Touch & Go tag programming).
 */
static int nfc_raw_ndef_writer_cb(const uint8_t *data, size_t len, void *user_data)
{
    (void)user_data;
    if (!nfc_power_on()) {
        ESP_LOGE(TAG, "NFC power-on failed; cannot publish Touch & Go NDEF");
        return -1;
    }
    esp_err_t ret = nfc_write_raw_ndef(data, len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NDEF write failed: %s", esp_err_to_name(ret));
        return (int)ret;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Render + refresh integration                                        */
/*                                                                     */
/* The UI manager renders all 19 pages into the shared 2bpp framebuffer;
 * the 4-color SSD2683 panel requires a full refresh for every update.  */
/* ------------------------------------------------------------------ */

static void render_ui_and_refresh(bool force_full)
{
    SemaphoreHandle_t mutex = get_display_mutex();
    if (!mutex)
        return;

    xSemaphoreTake(mutex, portMAX_DELAY);
    epd_clear();
    uint8_t      *fb  = get_framebuffer();
    ui_manager_t *mgr = (ui_manager_t *)application_get_ui_manager();
    if (mgr && fb) {
        ui_manager_render_all(mgr, fb, EXAMPLE_LCD_WIDTH, EXAMPLE_LCD_HEIGHT);
    }
    xSemaphoreGive(mutex);
    if (force_full) {
        request_urgent_full_refresh();
    } else {
        request_urgent_refresh();
    }
}

/* Refresh callback wired into the UI manager (called after HandleInput /
 * page switches / data updates). Renders the framebuffer and triggers the
 * EPD update. */
static void ui_refresh_cb(rawdraw_rect_t rect, bool urgent, void *user_data)
{
    (void)rect;
    (void)user_data;
    render_ui_and_refresh(urgent);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting ZecTrix EPD Panel C Firmware");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Initialize the nvs_state module's shared handle so all
     * nvs_state_get/set_* calls become functional. Without this,
     * theme persistence, last-page restore, and calendar navigation
     * persistence are all silent no-ops. */
    nvs_state_init();

    charge_status_init(&s_charge_status, CHARGE_DETECT_GPIO, CHARGE_FULL_GPIO, esp_timer_get_time() / 1000);

    board_init(&s_charge_status);

    board_power_vbat_on();
    board_power_audio_on();
    board_power_epd_on();

    while (!gpio_get_level(VBAT_PWR_GPIO)) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_LOGI(TAG, "Board Power Rails and I2C initialized");

    pcf8563_init(RTC_INT_GPIO);

    struct tm rtc_tm = {0};
    if (pcf8563_get_time(&rtc_tm)) {
        time_t t = mktime(&rtc_tm);
        if (t != -1) {
            struct timeval tv = {.tv_sec = t, .tv_usec = 0};
            settimeofday(&tv, NULL);
            ESP_LOGI(TAG, "System time synchronized with RTC: %02d:%02d:%02d", rtc_tm.tm_hour, rtc_tm.tm_min,
                     rtc_tm.tm_sec);
        }
    } else {
        ESP_LOGW(TAG, "RTC read failed; using system time");
    }

    nfc_init(NFC_PWR_GPIO, NFC_FD_GPIO, NFC_FD_ACTIVE_LEVEL);
    bluetooth_manager_init();
    ble_gatt_service_set_image_ready_callback(ble_image_ready_cb, NULL);
    bluetooth_manager_set_nfc_writer(nfc_raw_ndef_writer_cb, NULL);
    bluetooth_manager_enable();
    bluetooth_manager_publish_touch_and_go();
    audio_player_init();
    wifi_manager_init();

    audio_player_play_tone(1000, 100);

    settings_handle_t wifi_handle  = settings_open("wifi", false);
    char              ssid[32]     = {0};
    char              password[64] = {0};
    if (wifi_handle) {
        settings_get_string(wifi_handle, "ssid", ssid, sizeof(ssid), "");
        settings_get_string(wifi_handle, "password", password, sizeof(password), "");
        settings_close(wifi_handle);
    }

    const char *kconfig_ssid     = CONFIG_DEFAULT_WIFI_SSID;
    const char *kconfig_password = CONFIG_DEFAULT_WIFI_PASSWORD;
    if (strlen(kconfig_ssid) == 0) {
        kconfig_ssid     = "ZecTrix-AP";
        kconfig_password = "12345678";
    }

    if (strlen(ssid) == 0 || (strcmp(ssid, "ZecTrix-AP") == 0 && strcmp(kconfig_ssid, "ZecTrix-AP") != 0)) {
        settings_handle_t wifi_wr = settings_open("wifi", true);
        if (wifi_wr) {
            settings_set_string(wifi_wr, "ssid", kconfig_ssid);
            settings_set_string(wifi_wr, "password", kconfig_password);
            settings_close(wifi_wr);
            ESP_LOGI(TAG, "Saved default Wi-Fi '%s' to NVS settings.", kconfig_ssid);
            strncpy(ssid, kconfig_ssid, sizeof(ssid) - 1);
            strncpy(password, kconfig_password, sizeof(password) - 1);
        }
    }

    /* Check if this is an RTC slideshow wakeup (to save massive battery by skipping WiFi). */
    bool is_rtc_slideshow_wakeup = false;
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    if (cause == ESP_SLEEP_WAKEUP_EXT1) {
        uint64_t pin_mask = esp_sleep_get_ext1_wakeup_status();
        if (pin_mask & (1ULL << RTC_INT_GPIO)) {
            settings_handle_t gallery_nvs = settings_open("gallery", false);
            int32_t slideshow_interval = 5;
            if (gallery_nvs) {
                slideshow_interval = (int32_t)settings_get_int(gallery_nvs, "slide_min", 5);
                settings_close(gallery_nvs);
            }
            if (slideshow_interval > 0) {
                is_rtc_slideshow_wakeup = true;
                ESP_LOGI(TAG, "RTC slideshow wakeup: skipping Wi-Fi connection to save battery.");
            }
        }
    }

    if (strlen(ssid) > 0 && !is_rtc_slideshow_wakeup) {
        wifi_manager_connect(ssid, password);
    }

    custom_lcd_spi_t spi_data = {
        .cs         = EPD_CS_PIN,
        .dc         = EPD_DC_PIN,
        .rst        = EPD_RST_PIN,
        .busy       = EPD_BUSY_PIN,
        .mosi       = EPD_MOSI_PIN,
        .scl        = EPD_SCK_PIN,
        .power      = EPD_PWR_PIN,
        .spi_host   = EPD_SPI_NUM,
        .buffer_len = ((EXAMPLE_LCD_WIDTH * 2 + 7) / 8) * EXAMPLE_LCD_HEIGHT,
        .panel_type = EPD_PANEL_4COLOR_SSD2683,
    };
    custom_lcd_display_init(&spi_data);
    ESP_LOGI(TAG, "SSD2683 EPD display initialized");

    button_config_t btn_cfg = {
        .long_press_time = 1000,
    };

    button_gpio_config_t up_gpio_cfg = {
        .gpio_num     = TODO_UP_BUTTON_GPIO,
        .active_level = 0,
    };
    button_handle_t up_btn;
    ESP_ERROR_CHECK(iot_button_new_gpio_device(&btn_cfg, &up_gpio_cfg, &up_btn));

    button_gpio_config_t down_gpio_cfg = {
        .gpio_num     = TODO_DOWN_BUTTON_GPIO,
        .active_level = 0,
    };
    button_handle_t down_btn;
    ESP_ERROR_CHECK(iot_button_new_gpio_device(&btn_cfg, &down_gpio_cfg, &down_btn));

    button_gpio_config_t confirm_gpio_cfg = {
        .gpio_num     = TODO_CONFIRM_BUTTON_GPIO,
        .active_level = 0,
    };
    button_handle_t confirm_btn;
    ESP_ERROR_CHECK(iot_button_new_gpio_device(&btn_cfg, &confirm_gpio_cfg, &confirm_btn));

    iot_button_register_cb(up_btn, BUTTON_SINGLE_CLICK, NULL, button_up_click_cb, NULL);
    iot_button_register_cb(down_btn, BUTTON_SINGLE_CLICK, NULL, button_down_click_cb, NULL);
    iot_button_register_cb(confirm_btn, BUTTON_SINGLE_CLICK, NULL, button_confirm_click_cb, NULL);
    iot_button_register_cb(up_btn, BUTTON_LONG_PRESS_START, NULL, button_up_long_press_cb, NULL);
    iot_button_register_cb(down_btn, BUTTON_LONG_PRESS_START, NULL, button_down_long_press_cb, NULL);
    iot_button_register_cb(confirm_btn, BUTTON_LONG_PRESS_START, NULL, button_boot_long_press_cb, NULL);
    iot_button_register_cb(confirm_btn, BUTTON_DOUBLE_CLICK, NULL, button_boot_double_click_cb, NULL);
    iot_button_register_cb(up_btn, BUTTON_DOUBLE_CLICK, NULL, button_up_double_click_cb, NULL);
    iot_button_register_cb(down_btn, BUTTON_DOUBLE_CLICK, NULL, button_down_double_click_cb, NULL);

    /* Initialize the Application singleton (UI manager + settings menu). */
    application_init();
    /* WiFi may have connected before the app callback registered; resync. */
    application_notify_wifi_if_connected();

    /* Wire the EPD refresh callback into the UI manager. */
    ui_manager_t *mgr = (ui_manager_t *)application_get_ui_manager();
    if (mgr) {
        ui_manager_set_refresh_callback(mgr, ui_refresh_cb, NULL);
    }

    /* Initial render. */
    application_update_status_bar();
    render_ui_and_refresh(true);

    s_clock_timer = xTimerCreate("clock_timer", pdMS_TO_TICKS(1000), pdTRUE, NULL, clock_timer_callback);
    if (s_clock_timer) {
        xTimerStart(s_clock_timer, 0);
    }

    /* Main UI loop (deferred page refreshes, slideshow, clock pump). */
    xTaskCreatePinnedToCore(application_main_task, "app_main_loop", 8192, NULL, 5, NULL, 0);

    ESP_LOGI(TAG, "Main Application running");
}

/* Main UI loop task: pumps deferred refreshes and periodic timers. */
static void application_main_task(void *arg)
{
    (void)arg;
    application_run();
}
