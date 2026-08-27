/**
 * @file application_internal.h
 * @brief Private header exposing shared singleton state and cross-module
 *        function declarations for the application module split.
 *
 * Included only by application.c and its companion modules (app_sync.c,
 * app_sntp.c, app_sleep.c, app_settings_menu.c). The public API remains
 * in application.h.
 */
#ifndef MAIN_APPLICATION_INTERNAL_H_
#define MAIN_APPLICATION_INTERNAL_H_

#include "application.h"

#include <stdbool.h>
#include <stdint.h>
#include <sys/time.h>

#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include "ui_manager.h"
#include "protocol.h"
#include "stream_pipeline.h"
#include "wifi_manager.h"
#include "ap_transfer_server.h"
#include "coding_plan_api.h"
#include "weather_api.h"
#include "page_runtime.h"

/* ------------------------------------------------------------------ */
/* Event types (moved from application.c)                              */
/* ------------------------------------------------------------------ */

typedef enum {
    APP_EVENT_UP_CLICK,
    APP_EVENT_DOWN_CLICK,
    APP_EVENT_UP_LONG_PRESS,
    APP_EVENT_DOWN_LONG_PRESS,
    APP_EVENT_UP_DOUBLE_CLICK,
    APP_EVENT_BOOT_DOUBLE_CLICK,
    APP_EVENT_DOWN_DOUBLE_CLICK,
    APP_EVENT_WIFI_CONFIG_COMBO_LONG_PRESS,
    APP_EVENT_BOOT_CLICK,
    APP_EVENT_BOOT_LONG_PRESS,
    APP_EVENT_WIFI,
    APP_EVENT_TIME_SYNC,
    APP_EVENT_UI_IMAGE_RECEIVED,
    APP_EVENT_UI_SETTINGS_CHANGED,
    APP_EVENT_UI_PHOTOS_CHANGED,
    APP_EVENT_UI_SHOW_PHOTO,
} app_event_type_t;

typedef struct {
    const char *photo_id;
    bool *out_success;
    SemaphoreHandle_t done_sem;
} show_photo_event_data_t;

typedef struct {
    app_event_type_t type;
    union {
        wifi_manager_event_t wifi_event;
        struct {
            char photo_id[16];
        } image_received;
        struct {
            int slideshow_interval_minutes;
        } settings_changed;
        struct {
            void *show_photo_data;
        } show_photo;
    };
} app_event_t;

/* ------------------------------------------------------------------ */
/* Singleton state (moved from application.c)                          */
/* ------------------------------------------------------------------ */

typedef struct {
    device_state_t state;
    bool wifi_connected;
    ui_manager_t *ui_mgr;
    esp_timer_handle_t sleep_timer;
    protocol_t protocol;
    stream_pipeline_t pipeline;
    QueueHandle_t event_queue;
    bool rtc_wakeup;
} application_t;

extern application_t s_app;
extern ap_transfer_server_t s_transfer_server;

/* ------------------------------------------------------------------ */
/* Shared static variable (owned by app_sync.c)                       */
/* ------------------------------------------------------------------ */

extern int s_cp_refresh_counter;

/* ------------------------------------------------------------------ */
/* NVS keys and constants                                              */
/* ------------------------------------------------------------------ */

#define APP_SYNC_NS "sync"
#define APP_SYNC_INTERVAL_KEY "sync_interval"
#define APP_GALLERY_NS "gallery"
#define APP_SLIDESHOW_KEY "slide_min"

#define APP_SETTINGS_SLIDESHOW_INDEX 3
#define APP_SETTINGS_WIFI_INDEX 5
#define APP_SETTINGS_HTTP_SERVER_INDEX 6
#define APP_SETTINGS_LAN_IP_INDEX 7
#define APP_SETTINGS_SYNC_INTERVAL_INDEX 11
#define APP_DEFAULT_SYNC_INTERVAL_MIN 30

/* 30-minute coding-plan refresh cadence (application_run ticks 1/s). */
#define APP_CODING_PLAN_REFRESH_SECONDS (30 * 60)

/* ------------------------------------------------------------------ */
/* app_sync.c — data sync orchestration                               */
/* ------------------------------------------------------------------ */

void app_sync_ensure_coding_plan_initialised(void);
void app_sync_on_coding_plan_update(const coding_plan_api_data_t *data, void *user_data);
void app_sync_refresh_coding_plan(void);
void app_sync_ensure_fridge_memo_initialised(void);
void app_sync_refresh_fridge_memo(void);
void app_sync_on_weather_update(const weather_data_t *data, void *user_data);
void app_sync_on_data_refresh_request(int page, void *ctx);
void app_sync_on_sntp_sync(struct timeval *tv);

/* ------------------------------------------------------------------ */
/* app_sntp.c — SNTP initialization                                   */
/* ------------------------------------------------------------------ */

void app_sntp_start_once(void);

/* ------------------------------------------------------------------ */
/* app_sleep.c — sleep management                                     */
/* ------------------------------------------------------------------ */

void app_sleep_enter_scheduled(void);
void app_sleep_deep_sleep_now(bool keep_rtc_alarm, int64_t timer_us);
void app_sleep_arm_sync_timer(void);
void app_sleep_enter_manual(void);
bool app_sleep_is_local_http_running(void);
void app_sleep_invalidate_sync_interval_cache(void);
/* ------------------------------------------------------------------ */
/* app_settings_menu.c — settings menu construction + dispatch        */
/* ------------------------------------------------------------------ */

void app_settings_menu_build(void);
void app_settings_menu_cb(void *ctx);
void app_settings_update_wifi_item(bool connected, const char *value);
void app_settings_update_http_server_item(bool running, const char *ip);
void app_settings_update_lan_ip_item(const char *ip);

/* ------------------------------------------------------------------ */
/* Helpers exposed from application.c                                 */
/* ------------------------------------------------------------------ */

bool app_is_lan_http_running(void);

#endif /* MAIN_APPLICATION_INTERNAL_H_ */
