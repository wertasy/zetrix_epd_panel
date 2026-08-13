/**
 * @file app_sleep.c
 * @brief Sleep management — RTC alarm, deep sleep entry, peripheral
 *        power-down, and sync-timer arming.
 *
 * Extracted from application.c (Phase 2.1 module split). All functions
 * access the shared s_app singleton via application_internal.h.
 */
#include "application_internal.h"

#include <string.h>
#include <time.h>

#include <esp_log.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include "driver/rtc_io.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "board.h"
#include "sleep_manager.h"
#include "rtc_pcf8563.h"
#include "config.h"
#include "settings.h"
#include "ui_manager.h"
#include "wifi_manager.h"
#include "page_registry.h"
#include "photo_gallery_page.h"

#define TAG "Application"

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

bool app_sleep_is_local_http_running(void)
{
    return ap_transfer_server_is_running(&s_transfer_server);
}

static void power_down_peripherals_for_sleep(void)
{
    board_power_epd_off();
    board_power_audio_off();
    board_power_amp_off();
}

static void on_sync_sleep_timer(void *arg)
{
    (void)arg;
    app_sleep_enter_scheduled();
}

/* ------------------------------------------------------------------ */
/* Scheduled (timer-driven) sleep                                      */
/* ------------------------------------------------------------------ */

void app_sleep_enter_scheduled(void)
{
    if (app_sleep_is_local_http_running()) {
        ESP_LOGI(TAG, "Scheduled sleep skipped: local HTTP transfer service is running");
        app_sleep_arm_sync_timer();
        return;
    }

    if (!sm_can_sleep_now()) {
        ESP_LOGI(TAG, "Scheduled sleep postponed: sleep manager is busy (retrying in 5s)");
        if (s_app.sleep_timer != NULL) {
            esp_timer_stop(s_app.sleep_timer);
            esp_timer_delete(s_app.sleep_timer);
            s_app.sleep_timer = NULL;
        }
        esp_timer_create_args_t args = {0};
        args.callback = on_sync_sleep_timer;
        args.arg = NULL;
        args.dispatch_method = ESP_TIMER_TASK;
        args.name = "app_sync_sleep";
        if (esp_timer_create(&args, &s_app.sleep_timer) == ESP_OK) {
            esp_timer_start_once(s_app.sleep_timer, 5ULL * 1000 * 1000); // 5 seconds
        }
        return;
    }

    ESP_LOGI(TAG, "Entering deep sleep; BOOT, UP & RTC wake device");
    s_app.wifi_connected = false;
    /* Use wifi_manager_disconnect() — it raises retry_count to MAX_RETRY so
     * the STA_DISCONNECTED handler does NOT arm the auto-reconnect timer,
     * which would otherwise race with esp_wifi_stop()/deep sleep. */
    wifi_manager_disconnect();
    esp_wifi_stop();

    /* Persist the slideshow position so the next RTC wakeup advances past it
     * instead of always showing photo 2/3 (deep sleep wipes RAM). */
    page_renderer_t *gallery = page_registry_get_instance(UI_PAGE_GALLERY);
    if (gallery) {
        int idx = photo_gallery_get_selected_index(gallery);
        settings_handle_t gnvs = settings_open(APP_GALLERY_NS, true);
        if (gnvs) {
            settings_set_int(gnvs, "current_idx", idx);
            settings_close(gnvs);
        }
    }

    /* Wake-up period: use the slideshow interval only when the gallery
     * slideshow is enabled AND the current page is the gallery (RTC alarm
     * advances the slide). On non-gallery pages use the sync interval so
     * wakeups refresh the displayed page's network data. */
    int interval_minutes = 0;
    if (ui_manager_get_current_page(s_app.ui_mgr) == UI_PAGE_GALLERY) {
        interval_minutes = ui_manager_get_gallery_slideshow_interval_minutes(s_app.ui_mgr);
    }
    if (interval_minutes <= 0) {
        settings_handle_t nvs = settings_open(APP_SYNC_NS, false);
        interval_minutes = APP_DEFAULT_SYNC_INTERVAL_MIN;
        if (nvs) {
            interval_minutes = (int)settings_get_int(nvs, APP_SYNC_INTERVAL_KEY, APP_DEFAULT_SYNC_INTERVAL_MIN);
            settings_close(nvs);
        }
    }
    if (interval_minutes > 0) {
        struct tm now_tm;
        memset(&now_tm, 0, sizeof(now_tm));
        if (pcf8563_get_time(&now_tm)) {
            ESP_LOGI(TAG, "RTC now=%04d-%02d-%02d %02d:%02d:%02d", now_tm.tm_year + 1900, now_tm.tm_mon + 1,
                     now_tm.tm_mday, now_tm.tm_hour, now_tm.tm_min, now_tm.tm_sec);
            /* Round up to the next minute boundary + interval: a stale alarm
             * whose minute still matches the current time would otherwise
             * hold RTC_INT (GPIO5) low and wake the chip instantly. */
            if (now_tm.tm_sec > 0) {
                now_tm.tm_min += 1;
            }
            now_tm.tm_sec = 0;
            now_tm.tm_min += interval_minutes;
            time_t t = mktime(&now_tm);
            if (t != (time_t)-1) {
                struct tm *target_tm = localtime(&t);
                if (target_tm && pcf8563_set_alarm(target_tm)) {
                    ESP_LOGI(TAG, "Alarm armed %02d:%02d:%02d fired=%d int_level=%d", target_tm->tm_hour,
                             target_tm->tm_min, target_tm->tm_sec, pcf8563_is_alarm_fired() ? 1 : 0,
                             gpio_get_level(RTC_INT_GPIO));
                    /* Settle check: watch the alarm for 500ms. If AF or the INT
                     * line asserts right after arming, the RTC is mis-comparing
                     * and would instantly wake the chip. */
                    bool settled_fired = false;
                    for (int i = 0; i < 10; i++) {
                        if (pcf8563_is_alarm_fired() || gpio_get_level(RTC_INT_GPIO) == 0) {
                            settled_fired = true;
                            break;
                        }
                        vTaskDelay(pdMS_TO_TICKS(50));
                    }
                    ESP_LOGI(TAG, "Alarm settle: fired=%d int_level=%d", settled_fired ? 1 : 0,
                             gpio_get_level(RTC_INT_GPIO));
                    if (settled_fired) {
                        ESP_LOGW(TAG, "RTC alarm asserted immediately; disabling interrupt");
                        pcf8563_enable_interrupt(false);
                        pcf8563_clear_alarm_flag();
                    }
                    /* If a stale match re-asserted the flag right after arming,
                     * clear it so the INT line releases before sleeping. */
                    if (pcf8563_is_alarm_fired()) {
                        pcf8563_clear_alarm_flag();
                    }
                } else {
                    /* Alarm not armed: disable the interrupt so a stale alarm
                     * cannot hold GPIO5 low and cause an instant wake. */
                    ESP_LOGW(TAG, "Alarm arming failed; disabling RTC interrupt");
                    pcf8563_enable_interrupt(false);
                    pcf8563_clear_alarm_flag();
                }
            }
        }
    }
    /* Switch the wakeup pins to RTC IO mode and keep their internal pull-ups
     * during deep sleep. A plain gpio_config() pull-up is NOT preserved in
     * deep sleep: RTC_INT (GPIO5) is an open-drain output from the PCF8563,
     * so without a held pull-up the line floats and ANY_LOW fires instantly
     * (observed: pin_mask=0x20 wake ~0.5s after sleep entry with the alarm
     * still 5+ minutes away). Same treatment for the BOOT button (GPIO0).
     *
     * NOTE: only GPIO0-21 are RTC GPIOs on ESP32-S3; the UP button (GPIO39)
     * and DOWN button (GPIO18) are NOT valid EXT1 wake sources. Adding an
     * invalid GPIO to the mask makes esp_sleep_enable_ext1_wakeup() return
     * ESP_ERR_INVALID_ARG, which leaves the device with NO wake source at
     * all — a bricked deep sleep. BOOT is the only button wake source. */
    rtc_gpio_init(RTC_INT_GPIO);
    rtc_gpio_set_direction(RTC_INT_GPIO, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en(RTC_INT_GPIO);
    rtc_gpio_pulldown_dis(RTC_INT_GPIO);
    rtc_gpio_init(BOOT_BUTTON_GPIO);
    rtc_gpio_set_direction(BOOT_BUTTON_GPIO, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en(BOOT_BUTTON_GPIO);
    rtc_gpio_pulldown_dis(BOOT_BUTTON_GPIO);
    power_down_peripherals_for_sleep();
    ESP_LOGI(TAG, "After power down: RTC_INT level=%d BOOT level=%d", gpio_get_level(RTC_INT_GPIO),
             gpio_get_level(BOOT_BUTTON_GPIO));
    const esp_err_t wake_err =
        esp_sleep_enable_ext1_wakeup((1ULL << BOOT_BUTTON_GPIO) | (1ULL << RTC_INT_GPIO), ESP_EXT1_WAKEUP_ANY_LOW);
    if (wake_err != ESP_OK) {
        ESP_LOGE(TAG, "EXT1 wakeup config failed: %s — deep sleep would be unwakeable!", esp_err_to_name(wake_err));
    }
    esp_deep_sleep_start();
}

void app_sleep_arm_sync_timer(void)
{
    if (app_sleep_is_local_http_running()) {
        if (s_app.sleep_timer != NULL) {
            esp_timer_stop(s_app.sleep_timer);
        }
        ESP_LOGI(TAG, "Sync sleep timer skipped while local HTTP transfer "
                      "service is running");
        return;
    }
    /* Only skip the sync-sleep timer when the gallery slideshow is enabled AND
     * the current page is the gallery. On non-gallery pages (weather/calendar/
     * chat/etc.) the slideshow is not advancing, so the normal sync interval
     * must still apply — otherwise the device would never auto-sleep while a
     * non-gallery page is displayed. */
    if (ui_manager_get_gallery_slideshow_interval_minutes(s_app.ui_mgr) > 0 &&
        ui_manager_get_current_page(s_app.ui_mgr) == UI_PAGE_GALLERY) {
        if (s_app.sleep_timer != NULL) {
            esp_timer_stop(s_app.sleep_timer);
        }
        ESP_LOGI(TAG, "Sync sleep timer skipped while gallery slideshow is enabled");
        return;
    }

    settings_handle_t nvs = settings_open(APP_SYNC_NS, false);
    int interval_minutes = APP_DEFAULT_SYNC_INTERVAL_MIN;
    if (nvs) {
        interval_minutes = (int)settings_get_int(nvs, APP_SYNC_INTERVAL_KEY, APP_DEFAULT_SYNC_INTERVAL_MIN);
        settings_close(nvs);
    }
    if (interval_minutes <= 0) {
        ESP_LOGI(TAG, "Sync sleep interval: 关闭");
        return;
    }
    /* (Re)create a one-shot timer each arm. */
    if (s_app.sleep_timer != NULL) {
        esp_timer_stop(s_app.sleep_timer);
        esp_timer_delete(s_app.sleep_timer);
        s_app.sleep_timer = NULL;
    }
    esp_timer_create_args_t args = {0};
    args.callback = on_sync_sleep_timer;
    args.arg = NULL;
    args.dispatch_method = ESP_TIMER_TASK;
    args.name = "app_sync_sleep";
    esp_err_t ret = esp_timer_create(&args, &s_app.sleep_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create sync sleep timer: %s", esp_err_to_name(ret));
        return;
    }
    const int64_t delay_us = (int64_t)interval_minutes * 60 * 1000 * 1000;
    ESP_LOGI(TAG, "Sync sleep interval: %d minutes", interval_minutes);
    ESP_LOGI(TAG, "Scheduling sleep after sync interval: %d minutes", interval_minutes);
    ESP_ERROR_CHECK(esp_timer_start_once(s_app.sleep_timer, delay_us));
}

/* ------------------------------------------------------------------ */
/* Manual sleep (called from settings menu + public API wrapper)       */
/* ------------------------------------------------------------------ */

void app_sleep_enter_manual(void)
{
    ESP_LOGI(TAG, "Entering manual deep sleep; stopping local services and WiFi");
    if (s_app.sleep_timer != NULL) {
        esp_timer_stop(s_app.sleep_timer);
    }
    if (ap_transfer_server_is_running(&s_transfer_server)) {
        ap_transfer_server_stop(&s_transfer_server);
    }
    s_app.wifi_connected = false;
    wifi_manager_disconnect();
    esp_wifi_stop();
    application_update_status_bar();

    int wait_cnt = 0;
    while (!sm_can_sleep_now() && wait_cnt < 70) {
        vTaskDelay(pdMS_TO_TICKS(500));
        wait_cnt++;
    }

    /* Manual sleep: no RTC alarm is armed, so disable the alarm interrupt
     * and clear any stale flag — otherwise RTC_INT (GPIO5) could stay low
     * and wake the chip instantly. */
    pcf8563_enable_interrupt(false);
    pcf8563_clear_alarm_flag();
    /* Switch the wakeup pins to RTC IO mode so their internal pull-ups survive
     * deep sleep (a plain gpio_config pull-up is not preserved). Only BOOT
     * (GPIO0) and RTC_INT (GPIO5) are valid EXT1 sources — the UP button
     * (GPIO39) is not an RTC GPIO on ESP32-S3 and must NOT be in the mask. */
    rtc_gpio_init(RTC_INT_GPIO);
    rtc_gpio_set_direction(RTC_INT_GPIO, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en(RTC_INT_GPIO);
    rtc_gpio_pulldown_dis(RTC_INT_GPIO);
    rtc_gpio_init(BOOT_BUTTON_GPIO);
    rtc_gpio_set_direction(BOOT_BUTTON_GPIO, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en(BOOT_BUTTON_GPIO);
    rtc_gpio_pulldown_dis(BOOT_BUTTON_GPIO);
    power_down_peripherals_for_sleep();
    const esp_err_t wake_err =
        esp_sleep_enable_ext1_wakeup((1ULL << BOOT_BUTTON_GPIO) | (1ULL << RTC_INT_GPIO), ESP_EXT1_WAKEUP_ANY_LOW);
    if (wake_err != ESP_OK) {
        ESP_LOGE(TAG, "EXT1 wakeup config failed: %s — deep sleep would be unwakeable!", esp_err_to_name(wake_err));
    }
    esp_deep_sleep_start();
}
