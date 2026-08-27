/**
 * @file app_sleep.c
 * @brief Sleep management — RTC alarm, deep sleep entry, peripheral
 *        power-down, and sync-timer arming.
 *
 * Extracted from application.c (Phase 2.1 module split). All functions
 * access the shared s_app singleton via application_internal.h.
 */
#include "application_internal.h"
#include "app_page_runtime.h"
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
#include "rtc_time_valid.h"
#include "config.h"
#include "settings.h"
#include "ui_manager.h"
#include "wifi_manager.h"
#include "page_registry.h"
#include "page_runtime.h"
#include "photo_gallery_page.h"

#define TAG "Application"

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

bool app_sleep_is_local_http_running(void)
{
    /* Single busy source for sleep decisions: any registry-held service
     * (LAN HTTP or AP transfer) defers sleep (plan §3.7 #9). */
    return app_page_runtime_service_any_running();
}

static void power_down_peripherals_for_sleep(void)
{
    board_power_epd_off();
    board_power_audio_off();
    board_power_amp_off();
}

/* Configure the wake pins (RTC IO pull-ups survive deep sleep), enable
 * EXT1 (BOOT / RTC_INT / charger) plus an optional timer, and enter deep
 * sleep. Shared by the scheduled path, manual sleep, and main.c's
 * time-invalid early exit (which sleeps before the app is wired). */
void app_sleep_deep_sleep_now(bool keep_rtc_alarm, int64_t timer_us)
{
    if (!keep_rtc_alarm) {
        /* No RTC alarm is armed by this path: release the interrupt line
         * so a stale alarm flag cannot hold RTC_INT low and wake the chip
         * instantly. */
        pcf8563_enable_interrupt(false);
        pcf8563_clear_alarm_flag();
    }
    /* RTC_INT (GPIO5) is an open-drain output from the PCF8563; without a
     * held pull-up the line floats and ANY_LOW fires instantly. Only
     * GPIO0-21 are RTC GPIOs on ESP32-S3 (UP=39 and DOWN=18 are NOT valid
     * EXT1 sources; an invalid pin in the mask makes
     * esp_sleep_enable_ext1_wakeup() fail and leave NO wake source). */
    rtc_gpio_init(RTC_INT_GPIO);
    rtc_gpio_set_direction(RTC_INT_GPIO, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en(RTC_INT_GPIO);
    rtc_gpio_pulldown_dis(RTC_INT_GPIO);
    rtc_gpio_init(BOOT_BUTTON_GPIO);
    rtc_gpio_set_direction(BOOT_BUTTON_GPIO, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en(BOOT_BUTTON_GPIO);
    rtc_gpio_pulldown_dis(BOOT_BUTTON_GPIO);
#if CHARGE_GPIO_AFFECT_SLEEP
    rtc_gpio_init(CHARGE_DETECT_GPIO);
    rtc_gpio_set_direction(CHARGE_DETECT_GPIO, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en(CHARGE_DETECT_GPIO);
    rtc_gpio_pulldown_dis(CHARGE_DETECT_GPIO);
#endif
    power_down_peripherals_for_sleep();
    uint64_t ext1_mask = (1ULL << BOOT_BUTTON_GPIO) | (1ULL << RTC_INT_GPIO);
#if CHARGE_GPIO_AFFECT_SLEEP
    ext1_mask |= (1ULL << CHARGE_DETECT_GPIO); /* D12: plug-in wake */
#endif
    const esp_err_t wake_err = esp_sleep_enable_ext1_wakeup(ext1_mask, ESP_EXT1_WAKEUP_ANY_LOW);
    if (wake_err != ESP_OK) {
        ESP_LOGE(TAG, "EXT1 wakeup config failed: %s — deep sleep would be unwakeable!", esp_err_to_name(wake_err));
    }
    if (timer_us > 0) {
        esp_sleep_enable_timer_wakeup(timer_us);
    }
    esp_deep_sleep_start();
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
    /* Charger inserted (charging in progress or termination pin active):
     * suppress scheduled sleep. The former 60 s application-level hysteresis
     * raced the ~30 s post-boot sleep grace (deep sleep wipes the debounce
     * statics), so a plug-in wake looped forever: sleep at ~37 s, charge-pin
     * ext1 wake, full re-render. The charge snapshot is already debounce-held
     * (STABLE_HIGH_MS / POWER_PRESENT_HOLD_MS in charge_status_tick), so an
     * instantaneous check here is sufficient. Manual sleep and the low-battery
     * shutdown stay exempt (explicit user/power intents). */
    static bool s_charger_suppressed = false;
    const bool charger_present = charge_status_is_charging() || charge_status_power_present();
    if (charger_present) {
        /* 1s idle loop re-enters every second while plugged in: log on the
         * level change only, not once per call. */
        if (!s_charger_suppressed) {
            s_charger_suppressed = true;
            ESP_LOGI(TAG, "Charger present: scheduled sleep suppressed");
        }
        return;
    }
    s_charger_suppressed = false;
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
    /* Wake interval arbitration: foreground page policy first (runtime
     * override array > policy > system default). The gallery policy's
     * override slot carries the slideshow interval; other pages fall back
     * to the NVS sync interval. */
    const ui_page_id_t fg = ui_manager_get_current_page(s_app.ui_mgr);
    int interval_minutes = page_runtime_effective_wake_interval_override_min(fg);
    if (interval_minutes <= 0) {
        interval_minutes = page_runtime_effective_wake_interval_min(fg);
    }
    if (interval_minutes <= 0) {
        settings_handle_t nvs = settings_open(APP_SYNC_NS, false);
        interval_minutes = APP_DEFAULT_SYNC_INTERVAL_MIN;
        if (nvs) {
            interval_minutes = (int)settings_get_int(nvs, APP_SYNC_INTERVAL_KEY, APP_DEFAULT_SYNC_INTERVAL_MIN);
            settings_close(nvs);
        }
    }
    /* MIDNIGHT alignment dominates the plain interval: calendar-style pages
     * want to wake exactly at the next day boundary (00:01), not now+30min. */
    const bool align_midnight = page_runtime_policy(fg)->wake_align == PAGE_WAKE_ALIGN_MIDNIGHT;
    if (align_midnight) {
        interval_minutes = -1; /* sentinel: align to next 00:01 below */
    }
    int64_t fallback_timer_us = 0;
    if (interval_minutes > 0 || interval_minutes == -1) {
        struct tm now_tm;
        memset(&now_tm, 0, sizeof(now_tm));
        bool have_now = false;
        if (pcf8563_get_time(&now_tm)) {
            have_now = true;
        } else {
            /* RTC invalid (VL/implausible/I2C): fall back to the system
             * clock. Boot's self-heal normally keeps the RTC in sync, so
             * this covers mid-session RTC corruption. The alarm day field
             * matches the RTC's own counter — when the system time is
             * plausible we still arm the alarm AND rewrite the RTC base so
             * the day field can actually match. */
            time_t sys_now = time(NULL);
            struct tm sys_tm;
            localtime_r(&sys_now, &sys_tm);
            if (time_year_is_plausible(sys_tm.tm_year + 1900)) {
                now_tm = sys_tm;
                have_now = true;
                ESP_LOGW(TAG, "RTC invalid at sleep time; using system time and rewriting RTC base");
                pcf8563_set_time(&sys_tm);
            } else {
                ESP_LOGW(TAG, "No valid time source at sleep time; timer fallback %d min",
                         TIME_INVALID_RETRY_MINUTES);
                fallback_timer_us = (int64_t)TIME_INVALID_RETRY_MINUTES * 60 * 1000 * 1000;
            }
        }
        if (have_now) {
            ESP_LOGI(TAG, "RTC now=%04d-%02d-%02d %02d:%02d:%02d", now_tm.tm_year + 1900, now_tm.tm_mon + 1,
                     now_tm.tm_mday, now_tm.tm_hour, now_tm.tm_min, now_tm.tm_sec);
            bool have_target = false;
            struct tm target_tm_buf;
            if (interval_minutes == -1) {
                /* MIDNIGHT alignment: target comes from the pure helper
                 * (next 00:01, or a +5min catch-up when today's boundary
                 * passed but the content has not been served yet). */
                bool catch_up = false;
                if (page_runtime_midnight_alarm_target(&now_tm, &target_tm_buf, &catch_up) == 0) {
                    if (catch_up) {
                        interval_minutes = 5;
                        ESP_LOGI(TAG, "MIDNIGHT align: today's content unserved; arming catch-up alarm (+5min)");
                    } else {
                        have_target = true;
                        ESP_LOGI(TAG, "MIDNIGHT alarm target %02d:%02d", target_tm_buf.tm_hour, target_tm_buf.tm_min);
                    }
                }
            }
            if (interval_minutes > 0 && !have_target) {
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
                    struct tm *norm = localtime(&t);
                    if (norm) {
                        target_tm_buf = *norm;
                        have_target = true;
                    }
                }
            }
            if (have_target) {
                const struct tm *target_tm = &target_tm_buf;
                if (pcf8563_set_alarm(target_tm)) {
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
                    /* Alarm not armed (I2C write failure): disable the
                     * interrupt so a stale alarm cannot hold GPIO5 low and
                     * cause an instant wake, then fall back to a timer
                     * wake so the device never sleeps unwakeable (plan
                     * O-2). The retry chain is bounded by main.c's
                     * early-exit counter. */
                    ESP_LOGW(TAG, "Alarm arming failed; disabling RTC interrupt, timer fallback %d min",
                             TIME_INVALID_RETRY_MINUTES);
                    pcf8563_enable_interrupt(false);
                    pcf8563_clear_alarm_flag();
                    fallback_timer_us = (int64_t)TIME_INVALID_RETRY_MINUTES * 60 * 1000 * 1000;
                }
            }
        }
    }
    ESP_LOGI(TAG, "After arming: RTC_INT level=%d BOOT level=%d", gpio_get_level(RTC_INT_GPIO),
             gpio_get_level(BOOT_BUTTON_GPIO));
    /* keep_rtc_alarm=true: when arming succeeded the alarm must survive;
     * on the timer-fallback branches the interrupt was already disabled
     * there, and keeping the (cleared) flag state is harmless. */
    app_sleep_deep_sleep_now(true, fallback_timer_us);
}

/* NVS sync-interval RAM cache (read once, invalidated on settings write). */
static int s_sync_interval_cache = -1; /* -1 = not read yet */

static int app_sleep_get_cached_sync_interval(void)
{
    if (s_sync_interval_cache < 0) {
        settings_handle_t nvs = settings_open(APP_SYNC_NS, false);
        s_sync_interval_cache = APP_DEFAULT_SYNC_INTERVAL_MIN;
        if (nvs) {
            s_sync_interval_cache =
                (int)settings_get_int(nvs, APP_SYNC_INTERVAL_KEY, APP_DEFAULT_SYNC_INTERVAL_MIN);
            settings_close(nvs);
        }
    }
    return s_sync_interval_cache;
}

void app_sleep_invalidate_sync_interval_cache(void)
{
    s_sync_interval_cache = -1;
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

    /* Interval arbitration: runtime override (gallery slideshow) > page
     * policy > system default (NVS, cached in RAM after first read). A page
     * policy of 0 means "inherit system default". */
    const ui_page_id_t fg = ui_manager_get_current_page(s_app.ui_mgr);
    int interval_minutes = page_runtime_effective_wake_interval_override_min(fg);
    if (interval_minutes <= 0) {
        interval_minutes = page_runtime_effective_wake_interval_min(fg);
    }
    if (interval_minutes <= 0) {
        interval_minutes = app_sleep_get_cached_sync_interval();
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
    /* Registry-aware teardown: stops whatever runs and resets ownership
     * state so wake does not inherit stale "service held" flags. */
    app_page_runtime_service_release_all();
    s_app.wifi_connected = false;
    wifi_manager_disconnect();
    esp_wifi_stop();
    application_update_status_bar();

    int wait_cnt = 0;
    while (!sm_can_sleep_now() && wait_cnt < 70) {
        vTaskDelay(pdMS_TO_TICKS(500));
        wait_cnt++;
    }

    app_sleep_deep_sleep_now(false, 0);
}
