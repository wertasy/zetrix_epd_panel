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
#include <esp_bit_defs.h>
#include <freertos/timers.h>
#include <iot_button.h>
#include <button_gpio.h>
#include "nvs_state.h"
#include "holiday_fetcher.h"
#include "config.h"
#include "board.h"
#include "charge_status.h"
#include "epd_driver.h"
#include "rawdraw.h"
#include "rtc_pcf8563.h"
#include "rtc_time_valid.h"
#include "zectrix_nfc.h"
#include "settings.h"
#include "audio_player.h"
#include "wifi_manager.h"
#include "bluetooth_manager.h"
#include "ble_gatt_service.h"
#include "ble_image_receiver.h"
#include "application.h"
#include "page_runtime.h"
#include "calendar_page.h"
#include "application_internal.h"
static const char *TAG = "main_app";

static charge_status_t s_charge_status;
static TimerHandle_t s_clock_timer = NULL;

/* ------------------------------------------------------------------ */
/* Button callbacks (routed to the Application singleton)              */
static void application_main_task(void *arg);

/* ------------------------------------------------------------------ */

static bool s_suppress_next_up_click = false;
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
    ESP_LOGI(TAG, "render_ui_and_refresh: force_full=%d", force_full ? 1 : 0);
    SemaphoreHandle_t mutex = get_display_mutex();
    if (!mutex)
        return;

    xSemaphoreTake(mutex, portMAX_DELAY);
    epd_clear();
    uint8_t *fb = get_framebuffer();
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
    ui_manager_t *mgr = (ui_manager_t *)application_get_ui_manager();
    ESP_LOGI(TAG, "refresh_cb: urgent=%d page=%d", urgent ? 1 : 0, mgr ? (int)ui_manager_get_current_page(mgr) : -1);
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

    /* ---- Time trust chain (rtc-time-validity plan §3.2) ----
     * The RTC is the time authority on every boot (deep sleep wake =
     * reboot), but its registers can hold the power-on reset value
     * (2000-01-01, VL flag set) or I2C garbage. Boot sequence:
     *   1. RTC valid            -> settimeofday (unchanged behavior)
     *   2. RTC invalid, system continuation time plausible (maintained
     *      across deep sleep) -> SELF-HEAL: write it back to the RTC
     *      (clears VL), then adopt it. The alarm day field matches the
     *      RTC's own counter, so healing must precede any alarm arming.
     *   3. Both invalid         -> keep epoch; a calendar wake skips
     *      rendering and retries via timer (max 3). */
    const uint32_t wake_causes_early = esp_sleep_get_wakeup_causes();
    bool time_ok = false;
    struct tm rtc_tm = {0};
    {
        uint8_t raw[7] = {0};
        const bool raw_ok = pcf8563_get_raw(raw);
        const bool vl = raw_ok && (raw[0] & 0x80);
        ESP_LOGI(TAG, "RTC raw: %s VL=%d regs=%02X %02X %02X %02X %02X %02X %02X wake_timer=%d wake_ext1=%d",
                 raw_ok ? "ok" : "I2C_FAIL", vl, raw[0], raw[1], raw[2], raw[3], raw[4], raw[5], raw[6],
                 (int)(wake_causes_early & BIT(ESP_SLEEP_WAKEUP_TIMER)),
                 (int)(wake_causes_early & BIT(ESP_SLEEP_WAKEUP_EXT1)));
        if (pcf8563_get_time(&rtc_tm)) {
            time_t t = mktime(&rtc_tm);
            if (t != (time_t)-1) {
                struct timeval tv = {.tv_sec = t, .tv_usec = 0};
                settimeofday(&tv, NULL);
                time_ok = true;
                page_runtime_time_retry_clear();
                ESP_LOGI(TAG, "System time synchronized with RTC: %02d:%02d:%02d", rtc_tm.tm_hour, rtc_tm.tm_min,
                         rtc_tm.tm_sec);
            }
        }
        if (!time_ok) {
            time_t now = time(NULL);
            struct tm cont_tm;
            localtime_r(&now, &cont_tm);
            if (time_year_is_plausible(cont_tm.tm_year + 1900)) {
                ESP_LOGW(TAG, "RTC invalid; self-healing from system continuation time %04d-%02d-%02d %02d:%02d:%02d",
                         cont_tm.tm_year + 1900, cont_tm.tm_mon + 1, cont_tm.tm_mday, cont_tm.tm_hour, cont_tm.tm_min,
                         cont_tm.tm_sec);
                if (pcf8563_set_time(&cont_tm)) {
                    /* Re-read so later consumers see one consistent source. */
                    if (pcf8563_get_time(&rtc_tm)) {
                        time_t t = mktime(&rtc_tm);
                        if (t != (time_t)-1) {
                            struct timeval tv = {.tv_sec = t, .tv_usec = 0};
                            settimeofday(&tv, NULL);
                            time_ok = true;
                            page_runtime_time_retry_clear();
                        }
                    }
                } else {
                    ESP_LOGE(TAG, "RTC self-heal write failed");
                }
                if (time_ok)
                    ESP_LOGI(TAG, "RTC self-healed; alarms will match the day field");
                else
                    ESP_LOGW(TAG, "RTC still invalid; continuing on system time");
            } else {
                ESP_LOGW(TAG, "No valid time source (RTC invalid, system epoch); calendar wake will retry via timer");
            }
        }
    }

    /* ---- Time-invalid early exit (plan §3.2/§3.4) ----
     * Unattended wake (RTC alarm or its timer retry) with no valid time
     * source: keep the old EPD image, stay offline, latch the C-1
     * explanation flag, and retry via deep-sleep timer (bounded). A BOOT
     * button or charger wake is interactive/powered — continue booting so
     * Wi-Fi + SNTP can heal the clock. */
    {
        const bool timer_wake = (wake_causes_early & BIT(ESP_SLEEP_WAKEUP_TIMER)) != 0;
        uint64_t ext1_pins = 0;
        if (wake_causes_early & BIT(ESP_SLEEP_WAKEUP_EXT1))
            ext1_pins = esp_sleep_get_ext1_wakeup_status();
        const bool boot_btn_wake = (ext1_pins & (1ULL << BOOT_BUTTON_GPIO)) != 0;
        const bool rtc_alarm_wake = (ext1_pins & (1ULL << RTC_INT_GPIO)) != 0;
        if (!time_ok && (timer_wake || rtc_alarm_wake) && !boot_btn_wake &&
            !charge_status_is_charging() && !charge_status_power_present()) {
            /* Charger present: suppress the early exit — sleeping here
             * would be re-woken instantly by the charge pin (it is in the
             * ext1 mask and held low while plugged in). Continuing boot
             * lets Wi-Fi/SNTP heal the clock, strictly better. */
            if (page_runtime_time_retry_count() >= PAGE_RUNTIME_TIME_RETRY_MAX) {
                ESP_LOGW(TAG, "Time retry budget exhausted; sleeping until button/charger wake");
                app_sleep_deep_sleep_now(false, 0);
            }
            page_runtime_time_retry_increment();
            calendar_page_note_time_invalid();
            const int64_t retry_us = (int64_t)TIME_INVALID_RETRY_MINUTES * 60 * 1000 * 1000;
            ESP_LOGW(TAG, "No valid time on unattended wake; retry %lu/%d in %d min (offline, no refresh)",
                     (unsigned long)page_runtime_time_retry_count(), PAGE_RUNTIME_TIME_RETRY_MAX,
                     TIME_INVALID_RETRY_MINUTES);
            app_sleep_deep_sleep_now(false, retry_us);
        }
    }

    nfc_init(NFC_PWR_GPIO, NFC_FD_GPIO, NFC_FD_ACTIVE_LEVEL);
    bluetooth_manager_init();
    ble_gatt_service_set_image_ready_callback(ble_image_ready_cb, NULL);
    bluetooth_manager_set_nfc_writer(nfc_raw_ndef_writer_cb, NULL);
    bluetooth_manager_enable();
    bluetooth_manager_publish_touch_and_go();
    audio_player_init();
    wifi_manager_init();
    /* Load the holiday NVS cache before the wake-path network decision so a
     * calendar wake can skip Wi-Fi when the year cache already hits. */
    holiday_fetcher_init();

    audio_player_play_tone(1000, 100);

    settings_handle_t wifi_handle = settings_open("wifi", false);
    char ssid[32] = {0};
    char password[64] = {0};
    if (wifi_handle) {
        settings_get_string(wifi_handle, "ssid", ssid, sizeof(ssid), "");
        settings_get_string(wifi_handle, "password", password, sizeof(password), "");
        settings_close(wifi_handle);
    }

    const char *kconfig_ssid = CONFIG_DEFAULT_WIFI_SSID;
    const char *kconfig_password = CONFIG_DEFAULT_WIFI_PASSWORD;
    if (strlen(kconfig_ssid) == 0) {
        kconfig_ssid = "ZecTrix-AP";
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

    /* RTC wakeup: decide via the restored page's runtime policy whether Wi-Fi
     * is needed at all. Gallery-with-slideshow (policy: needs_network_on_wake
     * = false, slideshow override active) skips Wi-Fi entirely to save
     * battery; every other page connects normally. Cold boot always
     * connects. */
    bool skip_wifi = false;
    const uint32_t wakeup_causes = wake_causes_early;
    /* Unattended wake = RTC alarm (EXT1 pin) or its timer retry. Both
     * restore the saved page and run the offline network policy; a BOOT
     * button wake boots interactively like a cold boot. */
    const bool timer_wake2 = (wakeup_causes & BIT(ESP_SLEEP_WAKEUP_TIMER)) != 0;
    uint64_t pin_mask = 0;
    if (wakeup_causes & BIT(ESP_SLEEP_WAKEUP_EXT1)) {
        /* Fast multi-pulse activity blink: signals that the device is waking up. */
        board_flash_activity_led_blink(3);
        pin_mask = esp_sleep_get_ext1_wakeup_status();
    }
    const bool rtc_alarm_wake2 = (pin_mask & (1ULL << RTC_INT_GPIO)) != 0;
    if (rtc_alarm_wake2 || timer_wake2) {
        const ui_page_id_t restore = ui_manager_get_rtc_saved_page();
        /* Replay the gallery slideshow override from NVS into the runtime
         * override array (deep sleep wipes RAM). */
        if (restore == UI_PAGE_GALLERY) {
            settings_handle_t gallery_nvs = settings_open("gallery", false);
            int32_t slideshow_interval = 5;
            if (gallery_nvs) {
                slideshow_interval = (int32_t)settings_get_int(gallery_nvs, "slide_min", 5);
                settings_close(gallery_nvs);
            }
            page_runtime_set_wake_interval_override(UI_PAGE_GALLERY, (int)slideshow_interval);
        }
        if (!page_runtime_effective_network_on_wake(restore)) {
            skip_wifi = true;
            ESP_LOGI(TAG, "RTC wakeup: policy says no network needed; skipping Wi-Fi.");
        } else if (restore == UI_PAGE_CALENDAR) {
            /* Dynamic calendar probe: holiday cache hit (and not year
             * end) -> the wake refresh needs no network either. */
            time_t now = time(NULL);
            struct tm tmr;
            localtime_r(&now, &tmr);
            const int year = tmr.tm_year + 1900;
            const bool year_end = (tmr.tm_mon == 11) && (tmr.tm_mday >= 30);
            if (holiday_fetcher_is_year_cached(year) && !year_end) {
                skip_wifi = true;
                ESP_LOGI(TAG, "RTC wakeup: calendar holiday cache hit; skipping Wi-Fi.");
            }
        }
    }

    if (strlen(ssid) > 0 && !skip_wifi) {
        wifi_manager_connect(ssid, password);
    }

    epd_spi_t spi_data = {
        .cs = EPD_CS_PIN,
        .dc = EPD_DC_PIN,
        .rst = EPD_RST_PIN,
        .busy = EPD_BUSY_PIN,
        .mosi = EPD_MOSI_PIN,
        .scl = EPD_SCK_PIN,
        .power = EPD_PWR_PIN,
        .spi_host = EPD_SPI_NUM,
        .buffer_len = ((EXAMPLE_LCD_WIDTH * 2 + 7) / 8) * EXAMPLE_LCD_HEIGHT,
        .panel_type = EPD_PANEL_4COLOR_SSD2683,
    };
    epd_driver_init(&spi_data);
    ESP_LOGI(TAG, "SSD2683 EPD display initialized");
    /* When WiFi will connect, boot data (SNTP, weather, holidays) arrives
     * 3–11 s after boot — during the ~23 s first physical refresh. Extend
     * the first-merge window so all that data lands in the framebuffer
     * before the single first refresh, avoiding a second flash. */
    if (strlen(ssid) > 0 && !skip_wifi) {
        epd_driver_set_boot_merge_ms(10000);
    }

    button_config_t btn_cfg = {
        .long_press_time = 1000,
    };

    button_gpio_config_t up_gpio_cfg = {
        .gpio_num = TODO_UP_BUTTON_GPIO,
        .active_level = 0,
    };
    button_handle_t up_btn;
    ESP_ERROR_CHECK(iot_button_new_gpio_device(&btn_cfg, &up_gpio_cfg, &up_btn));

    button_gpio_config_t down_gpio_cfg = {
        .gpio_num = TODO_DOWN_BUTTON_GPIO,
        .active_level = 0,
    };
    button_handle_t down_btn;
    ESP_ERROR_CHECK(iot_button_new_gpio_device(&btn_cfg, &down_gpio_cfg, &down_btn));

    button_gpio_config_t confirm_gpio_cfg = {
        .gpio_num = TODO_CONFIRM_BUTTON_GPIO,
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

    /* Initial render + first EPD refresh. The refresh scheduler merges
     * requests that arrive within its first-merge window, so the wifi-icon
     * status-bar change right after connect is coalesced into this one
     * refresh instead of flashing the page twice. */
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
