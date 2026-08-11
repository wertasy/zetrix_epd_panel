/**
 * @file application.c
 * @brief Application singleton — C port of C++ Application.
 *
 * Owns the RawDraw UI manager (via ui_manager.h), routes button events,
 * manages WiFi / sync-sleep state, and updates the status bar. Audio
 * service is omitted (Phase 4 audio pipeline parked); sound feedback
 * uses the existing audio_player module.
 */
#include "application.h"

#include <string.h>
#include <time.h>
#include <sys/time.h>

#include <esp_log.h>
#include <esp_mac.h>
#include <esp_sleep.h>
#include <esp_sntp.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include "driver/rtc_io.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include "board.h"
#include "sleep_manager.h"
#include "rtc_pcf8563.h"
#include "custom_lcd_display.h"
#include "config.h"
#include "settings.h"
#include "ui_manager.h"
#include "wifi_manager.h"
#include "settings_page.h"
#include "protocol.h"
#include "stream_pipeline.h"
#include "rawdraw.h"
#include "rawdraw_ext.h"
#include "style.h"
#include "coding_plan_api.h"
#include "coding_plan_page.h"
#include "weather_api.h"
#include "weather_page.h"
#include "calendar_page.h"
#include "holiday_fetcher.h"
#include "page_registry.h"
#include "photo_gallery_page.h"

#ifndef PROJECT_VER
#    define PROJECT_VER "3.8.0"
#endif

#define TAG "Application"

/* NVS keys (match C++). */
#define APP_SYNC_NS "sync"
#define APP_SYNC_INTERVAL_KEY "sync_interval"
#define APP_GALLERY_NS "gallery"
#define APP_SLIDESHOW_KEY "slide_min"

/* Settings item indices (match C++ Application). */
#define APP_SETTINGS_SLIDESHOW_INDEX 3
#define APP_SETTINGS_WIFI_INDEX 5
#define APP_SETTINGS_HTTP_SERVER_INDEX 6
#define APP_SETTINGS_LAN_IP_INDEX 7

/* Default sync-sleep interval (minutes). */
#define APP_DEFAULT_SYNC_INTERVAL_MIN 30

#ifdef ESP_PLATFORM
#    include "esp_attr.h"
#else
#    ifndef EXT_RAM_BSS_ATTR
#        define EXT_RAM_BSS_ATTR
#    endif
#endif

/* ------------------------------------------------------------------ */
/* Singleton state                                                     */
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

typedef struct {
    device_state_t     state;
    bool               wifi_connected;
    bool               need_weather_fetch;
    bool               need_coding_plan_refresh;
    bool               need_holiday_fetch;
    ui_manager_t      *ui_mgr;
    esp_timer_handle_t sleep_timer;
    protocol_t         protocol;
    stream_pipeline_t  pipeline;
    QueueHandle_t      event_queue;
    bool               rtc_wakeup;
} application_t;

static EXT_RAM_BSS_ATTR application_t s_app;
/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */

static void enter_scheduled_sleep(void);
static void arm_sync_sleep_timer(void);
static void on_sync_sleep_timer(void *arg);
static void update_wifi_settings_item(bool connected, const char *value);
static void update_http_server_settings_item(bool running, const char *ip);
static void update_lan_ip_settings_item(const char *ip);
static void start_sntp_clock_sync_once(void);
static void ensure_coding_plan_initialised(void);
static void refresh_coding_plan(void);
static void on_coding_plan_update(const coding_plan_api_data_t *data, void *user_data);
static void on_weather_update(const weather_data_t *data, void *user_data);
static void on_sntp_sync(struct timeval *tv);
static void handle_image_received_async(const char *photo_id, void *ctx);
static void handle_settings_changed_async(int minutes, void *ctx);
static void handle_photos_changed_async(void *ctx);
static bool handle_show_photo_sync(const char *photo_id, void *ctx);

/* ------------------------------------------------------------------ */
/* Static helpers                                                      */
/* ------------------------------------------------------------------ */

static void format_minutes_label(int minutes, char *out, size_t len)
{
    if (minutes <= 0) {
        snprintf(out, len, "%s", "关闭");
    } else {
        snprintf(out, len, "%dmin", minutes);
    }
}

static const char *format_minutes_log_label(int minutes)
{
    return minutes <= 0 ? "关闭" : "开启";
}

static bool is_local_http_service_running(void)
{
    return ui_manager_is_http_server_running(s_app.ui_mgr);
}

/* ------------------------------------------------------------------ */
/* Coding Plan usage refresh                                           */
/* ------------------------------------------------------------------ */

/* 30-minute refresh cadence (application_run ticks once per second). */
#define APP_CODING_PLAN_REFRESH_SECONDS (30 * 60)

static void ensure_coding_plan_initialised(void)
{
    static bool s_cp_inited = false;
    if (s_cp_inited)
        return;
    coding_plan_api_init(CONFIG_CODING_PLAN_API_TOKEN, CONFIG_CODING_PLAN_API_ORG, CONFIG_CODING_PLAN_API_PROJECT);
    coding_plan_api_set_callback(on_coding_plan_update, NULL);
    s_cp_inited = true;

    /* If NVS cache exists, immediately render it so the page shows data
     * before the network fetch completes. */
    if (coding_plan_api_has_cached_data()) {
        on_coding_plan_update(coding_plan_api_get_cached_data(), NULL);
    }
}

static void on_coding_plan_update(const coding_plan_api_data_t *data, void *user_data)
{
    (void)user_data;
    if (!data)
        return;

    page_renderer_t *page = ui_manager_get_renderer(s_app.ui_mgr, UI_PAGE_CODING_PLAN);
    if (!page)
        return;

    coding_plan_data_t *page_data = (coding_plan_data_t *)malloc(sizeof(coding_plan_data_t));
    if (!page_data)
        return;
    memset(page_data, 0, sizeof(*page_data));
    snprintf(page_data->five_hour_reset_time, sizeof(page_data->five_hour_reset_time), "%s", data->five_hour_reset_time);
    snprintf(page_data->week_reset_time, sizeof(page_data->week_reset_time), "%s", data->week_reset_time);
    page_data->five_hour_tokens = data->five_hour_tokens;
    page_data->week_tokens      = data->week_tokens;
    page_data->five_hour_pct    = data->five_hour_pct;
    page_data->week_pct         = data->week_pct;
    page_data->per_model_count  = data->per_model_count;
    for (int i = 0; i < data->per_model_count && i < CODING_PLAN_MAX_MODELS; i++) {
        page_data->per_model[i] = data->per_model[i];
    }
    page_data->hourly_count = data->hourly_count;
    memcpy(page_data->hourly_tokens, data->hourly_tokens, sizeof(page_data->hourly_tokens));

    coding_plan_page_update(page, page_data);
    free(page_data);
    ui_manager_request_active_page_refresh(s_app.ui_mgr);
}

static void refresh_coding_plan(void)
{
    ensure_coding_plan_initialised();
    coding_plan_api_fetch_async();
}

/* ------------------------------------------------------------------ */
/* SNTP clock sync                                                     */
/* ------------------------------------------------------------------ */

static void on_sntp_sync(struct timeval *tv)
{
    (void)tv;
    ESP_LOGI(TAG, "SNTP time sync complete; writing RTC and refreshing calendar");

    /* Write the corrected time back to the PCF8563 RTC so cold boots start
     * with the right date without needing WiFi. */
    time_t    now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    pcf8563_set_time(&tm_buf);

    /* Notify the main loop so it can refresh date-dependent pages. */
    app_event_t ev = { .type = APP_EVENT_TIME_SYNC };
    if (s_app.event_queue) {
        xQueueSend(s_app.event_queue, &ev, 0);
    }
}

static void start_sntp_clock_sync_once(void)
{
    static bool s_started = false;
    if (s_started)
        return;

    setenv("TZ", "CST-8", 1);
    tzset();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp.aliyun.com");
    esp_sntp_setservername(1, "cn.pool.ntp.org");
    esp_sntp_set_time_sync_notification_cb(on_sntp_sync);
    esp_sntp_init();
    s_started = true;
    ESP_LOGI(TAG, "SNTP started: tz=Asia/Shanghai "
                  "servers=ntp.aliyun.com,cn.pool.ntp.org,pool.ntp.org");
}

/* ------------------------------------------------------------------ */
/* Settings item updates                                               */
/* ------------------------------------------------------------------ */

static void update_wifi_settings_item(bool connected, const char *value)
{
    ui_manager_update_settings_checked(s_app.ui_mgr, APP_SETTINGS_WIFI_INDEX, connected);
    if (value) {
        ui_manager_update_settings_item(s_app.ui_mgr, APP_SETTINGS_WIFI_INDEX, value);
    } else {
        ui_manager_update_settings_item(s_app.ui_mgr, APP_SETTINGS_WIFI_INDEX, connected ? "已连接" : "未连接");
    }
}

static void update_http_server_settings_item(bool running, const char *ip)
{
    char value[64];
    if (running && ip && ip[0] != '\0') {
        snprintf(value, sizeof(value), "http://%s", ip);
    } else if (ip && ip[0] != '\0') {
        snprintf(value, sizeof(value), "%s", ip);
    } else {
        snprintf(value, sizeof(value), "%s", running ? "已开启" : "已关闭");
    }
    ui_manager_update_settings_checked(s_app.ui_mgr, APP_SETTINGS_HTTP_SERVER_INDEX, running);
    ui_manager_update_settings_item(s_app.ui_mgr, APP_SETTINGS_HTTP_SERVER_INDEX, value);
}

static void update_lan_ip_settings_item(const char *ip)
{
    ui_manager_update_settings_item(s_app.ui_mgr, APP_SETTINGS_LAN_IP_INDEX, (ip && ip[0] != '\0') ? ip : "未获取");
}

/* ------------------------------------------------------------------ */
/* Settings menu callback dispatcher                                   */
/*                                                                     */
/* The on_click ctx value carries an action tag:                       */
/*   1 = slideshow interval cycle, 2 = wifi toggle,                    */
/*   3 = LAN HTTP server toggle, 4 = manual sleep, 5 = reboot          */
/* ------------------------------------------------------------------ */

static void settings_menu_cb(void *ctx)
{
    const intptr_t action = (intptr_t)ctx;
    switch (action) {
    case 1: { /* Slideshow interval cycle. */
        settings_handle_t nvs     = settings_open(APP_GALLERY_NS, true);
        int               current = 5;
        if (nvs) {
            current = (int)settings_get_int(nvs, APP_SLIDESHOW_KEY, 5);
        }
        static const int kOptions[] = {0, 5, 10, 30};
        int              next       = 5;
        for (unsigned i = 0; i < sizeof(kOptions) / sizeof(kOptions[0]); ++i) {
            if (kOptions[i] == current) {
                next = kOptions[(i + 1) % (sizeof(kOptions) / sizeof(kOptions[0]))];
                break;
            }
        }
        if (nvs) {
            settings_set_int(nvs, APP_SLIDESHOW_KEY, next);
            settings_close(nvs);
        }
        ui_manager_set_gallery_slideshow_interval_minutes(s_app.ui_mgr, next);
        char label[16];
        format_minutes_label(next, label, sizeof(label));
        ui_manager_update_settings_item(s_app.ui_mgr, APP_SETTINGS_SLIDESHOW_INDEX, label);
        if (next > 0 && s_app.sleep_timer != NULL) {
            esp_timer_stop(s_app.sleep_timer);
            ESP_LOGI(TAG, "Sync sleep timer paused while gallery slideshow is enabled");
        } else if (next <= 0 && (s_app.wifi_connected || wifi_manager_is_connected())) {
            arm_sync_sleep_timer();
        }
        break;
    }
    case 2: { /* WiFi toggle. */
        if (s_app.wifi_connected || wifi_manager_is_connected()) {
            ESP_LOGI(TAG, "Wi-Fi setting toggled OFF");
            if (ui_manager_is_lan_http_server_running(s_app.ui_mgr)) {
                ui_manager_stop_lan_http_server(s_app.ui_mgr);
                update_http_server_settings_item(false, NULL);
            }
            wifi_manager_disconnect();
            s_app.wifi_connected = false;
            update_wifi_settings_item(false, NULL);
            update_lan_ip_settings_item("");
        } else {
            ESP_LOGI(TAG, "Wi-Fi setting toggled ON");
            update_wifi_settings_item(false, "连接中");
            wifi_manager_connect("", "");
        }
        application_update_status_bar();
        break;
    }
    case 3: { /* LAN HTTP server toggle. */
        if (ui_manager_is_lan_http_server_running(s_app.ui_mgr)) {
            ESP_LOGI(TAG, "LAN HTTP server toggled OFF");
            ui_manager_stop_lan_http_server(s_app.ui_mgr);
            update_http_server_settings_item(false, NULL);
            application_update_status_bar();
            if (s_app.wifi_connected || wifi_manager_is_connected()) {
                arm_sync_sleep_timer();
            }
            return;
        }
        if (!s_app.wifi_connected && !wifi_manager_is_connected()) {
            ESP_LOGW(TAG, "LAN HTTP server requires WiFi connection");
            update_http_server_settings_item(false, "需先连接WiFi");
            application_update_status_bar();
            return;
        }
        char ip[32] = {0};
        wifi_manager_get_ip(ip, sizeof(ip));
        if (ip[0] == '\0') {
            ESP_LOGW(TAG, "LAN HTTP server requires station IP");
            update_http_server_settings_item(false, "等待IP");
            application_update_status_bar();
            return;
        }
        const bool started = ui_manager_start_lan_http_server(s_app.ui_mgr, ip);
        ESP_LOGI(TAG, "LAN HTTP server toggled ON: started=%d url=http://%s/", started ? 1 : 0, ip);
        if (started && s_app.sleep_timer != NULL) {
            esp_timer_stop(s_app.sleep_timer);
            ESP_LOGI(TAG, "Sync sleep timer paused while LAN HTTP server is running");
        }
        update_http_server_settings_item(started, started ? ip : "");
        update_lan_ip_settings_item(started ? ip : "");
        application_update_status_bar();
        break;
    }
    case 4: { /* Manual sleep. */
        ESP_LOGI(TAG, "Manual sleep requested from settings");
        application_enter_manual_sleep();
        break;
    }
    case 5: /* Reboot. */
    default:
        ESP_LOGI(TAG, "Restart requested from settings");
        esp_restart();
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Settings menu construction                                          */
/* ------------------------------------------------------------------ */

static void build_settings_items(void)
{
    settings_handle_t gallery_nvs        = settings_open(APP_GALLERY_NS, false);
    int               slideshow_interval = 5;
    if (gallery_nvs) {
        slideshow_interval = (int)settings_get_int(gallery_nvs, APP_SLIDESHOW_KEY, 5);
        settings_close(gallery_nvs);
    }
    if (slideshow_interval != 0 && slideshow_interval != 5 && slideshow_interval != 10 && slideshow_interval != 30) {
        slideshow_interval = 5;
    }
    ESP_LOGI(TAG, "Startup gallery fullscreen slideshow: %s, interval=%s", format_minutes_log_label(slideshow_interval),
             slideshow_interval > 0 ? "开启" : "关闭");
    ui_manager_set_gallery_slideshow_interval_minutes(s_app.ui_mgr, slideshow_interval);

    /* Declarative settings menu (C port of the C++ items vector). */
    settings_page_item_t items[12];
    int                  n = 0;
    memset(items, 0, sizeof(items));

    /* Section: 系统 */
    strcpy(items[n].label, "系统");
    items[n].type = SETTINGS_ITEM_SECTION;
    ++n;

    /* 重启 (action) */
    strcpy(items[n].label, "重启");
    strcpy(items[n].value, "执行");
    items[n].type         = SETTINGS_ITEM_ACTION;
    items[n].on_click     = settings_menu_cb;
    items[n].on_click_ctx = (void *)(intptr_t)5;
    ++n;

    /* Section: 相册 */
    strcpy(items[n].label, "相册");
    items[n].type = SETTINGS_ITEM_SECTION;
    ++n;

    /* 轮播间隔 (action) */
    strcpy(items[n].label, "轮播间隔");
    format_minutes_label(slideshow_interval, items[n].value, sizeof(items[n].value));
    items[n].type         = SETTINGS_ITEM_ACTION;
    items[n].on_click     = settings_menu_cb;
    items[n].on_click_ctx = (void *)(intptr_t)1;
    ++n;

    /* Section: 网络 */
    strcpy(items[n].label, "网络");
    items[n].type = SETTINGS_ITEM_SECTION;
    ++n;

    /* Wi-Fi (checkbox) */
    strcpy(items[n].label, "Wi-Fi");
    strcpy(items[n].value, "未连接");
    items[n].type         = SETTINGS_ITEM_CHECKBOX;
    items[n].on_click     = settings_menu_cb;
    items[n].on_click_ctx = (void *)(intptr_t)2;
    ++n;

    /* 局域网服务 (checkbox) */
    strcpy(items[n].label, "局域网服务");
    strcpy(items[n].value, "已关闭");
    items[n].type         = SETTINGS_ITEM_CHECKBOX;
    items[n].on_click     = settings_menu_cb;
    items[n].on_click_ctx = (void *)(intptr_t)3;
    ++n;

    /* 局域网IP (normal) */
    strcpy(items[n].label, "局域网IP");
    strcpy(items[n].value, "未获取");
    items[n].type = SETTINGS_ITEM_NORMAL;
    ++n;

    /* 省电模式 (action) */
    strcpy(items[n].label, "省电模式");
    strcpy(items[n].value, "手动进入");
    items[n].type         = SETTINGS_ITEM_ACTION;
    items[n].on_click     = settings_menu_cb;
    items[n].on_click_ctx = (void *)(intptr_t)4;
    ++n;

    /* Section: 关于 */
    strcpy(items[n].label, "关于");
    items[n].type = SETTINGS_ITEM_SECTION;
    ++n;

    /* 固件 (normal) */
    strcpy(items[n].label, "固件");
    strcpy(items[n].value, PROJECT_VER);
    items[n].type = SETTINGS_ITEM_NORMAL;
    ++n;

    ui_manager_set_settings_items(s_app.ui_mgr, items, n);

    /* Device info. */
    uint8_t mac_bytes[6] = {0};
    esp_read_mac(mac_bytes, ESP_MAC_WIFI_STA);
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X", mac_bytes[0], mac_bytes[1], mac_bytes[2],
             mac_bytes[3], mac_bytes[4], mac_bytes[5]);
    settings_page_set_device_info((page_renderer_t *)ui_manager_get_renderer(s_app.ui_mgr, UI_PAGE_SETTINGS), mac_str,
                                  "ESP32-S3");
    settings_page_set_firmware_version((page_renderer_t *)ui_manager_get_renderer(s_app.ui_mgr, UI_PAGE_SETTINGS),
                                       "v" PROJECT_VER);
}

/* ------------------------------------------------------------------ */
/* WiFi event callback                                                 */
/* ------------------------------------------------------------------ */

static void process_wifi_event(wifi_manager_event_t event)
{
    switch (event) {
    case WIFI_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WiFi connected");
        s_app.wifi_connected = true;
        sm_set_busy(SLEEP_BUSY_SRC_NET, false);
        protocol_start(&s_app.protocol);
        protocol_open_audio_channel(&s_app.protocol);
        start_sntp_clock_sync_once();
        if (!ui_manager_is_lan_http_server_running(s_app.ui_mgr)) {
            char ip[32] = {0};
            wifi_manager_get_ip(ip, sizeof(ip));
            if (ip[0] != '\0') {
                const bool started = ui_manager_start_lan_http_server(s_app.ui_mgr, ip);
                ESP_LOGI(TAG,
                         "LAN HTTP server auto-start after WiFi: started=%d "
                         "url=http://%s/",
                         started ? 1 : 0, ip);
                update_http_server_settings_item(started, started ? ip : "");
                update_lan_ip_settings_item(ip);
            }
        }
        if (ui_manager_get_current_page(s_app.ui_mgr) == UI_PAGE_AP_TRANSFER &&
            !ui_manager_is_ap_transfer_mode_running(s_app.ui_mgr)) {
            ESP_LOGI(TAG, "WiFi connected while config page is visible, "
                          "returning to gallery");
            ui_manager_switch_page(s_app.ui_mgr, UI_PAGE_GALLERY);
        }
        application_update_status_bar();
        arm_sync_sleep_timer();
        s_app.need_coding_plan_refresh = true;
        s_app.need_weather_fetch       = true;
        s_app.need_holiday_fetch       = true;
        break;
    case WIFI_EVENT_GOT_IP:
        application_update_status_bar();
        break;
    case WIFI_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "WiFi disconnected");
        s_app.wifi_connected = false;
        sm_set_busy(SLEEP_BUSY_SRC_NET, false);
        if (ui_manager_is_lan_http_server_running(s_app.ui_mgr)) {
            ui_manager_stop_lan_http_server(s_app.ui_mgr);
        }
        application_update_status_bar();
        break;
    case WIFI_EVENT_CONNECTING:
    default:
        s_app.wifi_connected = false;
        sm_set_busy(SLEEP_BUSY_SRC_NET, true);
        application_update_status_bar();
        break;
    }
}

static void on_wifi_event(wifi_manager_event_t event, void *user_data)
{
    (void)user_data;
    app_event_t ev = {
        .type = APP_EVENT_WIFI,
        .wifi_event = event
    };
    if (s_app.event_queue) {
        xQueueSend(s_app.event_queue, &ev, 0);
    }
}

/* ------------------------------------------------------------------ */
/* Sleep management                                                    */
/* ------------------------------------------------------------------ */

static void on_sync_sleep_timer(void *arg)
{
    (void)arg;
    enter_scheduled_sleep();
}

static void power_down_peripherals_for_sleep(void)
{
    board_power_epd_off();
    board_power_audio_off();
    board_power_amp_off();
}

static void enter_scheduled_sleep(void)
{
    if (is_local_http_service_running()) {
        ESP_LOGI(TAG, "Scheduled sleep skipped: local HTTP transfer service is running");
        arm_sync_sleep_timer();
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
        args.callback                = on_sync_sleep_timer;
        args.arg                     = NULL;
        args.dispatch_method         = ESP_TIMER_TASK;
        args.name                    = "app_sync_sleep";
        if (esp_timer_create(&args, &s_app.sleep_timer) == ESP_OK) {
            esp_timer_start_once(s_app.sleep_timer, 5ULL * 1000 * 1000); // 5 seconds
        }
        return;
    }

    ESP_LOGI(TAG, "Entering deep sleep; BOOT & RTC wake device");
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

    /* Wake-up period: use the slideshow interval when the gallery slideshow
     * is enabled (RTC alarm advances the slide), otherwise the sync interval. */
    int interval_minutes = ui_manager_get_gallery_slideshow_interval_minutes(s_app.ui_mgr);
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
     * still 5+ minutes away). Same treatment for the BOOT button (GPIO0). */
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
    esp_sleep_enable_ext1_wakeup((1ULL << BOOT_BUTTON_GPIO) | (1ULL << RTC_INT_GPIO), ESP_EXT1_WAKEUP_ANY_LOW);
    esp_deep_sleep_start();
}

static void arm_sync_sleep_timer(void)
{
    if (is_local_http_service_running()) {
        if (s_app.sleep_timer != NULL) {
            esp_timer_stop(s_app.sleep_timer);
        }
        ESP_LOGI(TAG, "Sync sleep timer skipped while local HTTP transfer "
                      "service is running");
        return;
    }
    if (ui_manager_get_gallery_slideshow_interval_minutes(s_app.ui_mgr) > 0) {
        if (s_app.sleep_timer != NULL) {
            esp_timer_stop(s_app.sleep_timer);
        }
        ESP_LOGI(TAG, "Sync sleep timer skipped while gallery slideshow is enabled");
        return;
    }

    settings_handle_t nvs              = settings_open(APP_SYNC_NS, false);
    int               interval_minutes = APP_DEFAULT_SYNC_INTERVAL_MIN;
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
    args.callback                = on_sync_sleep_timer;
    args.arg                     = NULL;
    args.dispatch_method         = ESP_TIMER_TASK;
    args.name                    = "app_sync_sleep";
    esp_err_t ret                = esp_timer_create(&args, &s_app.sleep_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create sync sleep timer: %s", esp_err_to_name(ret));
        return;
    }
    const int64_t delay_us = (int64_t)interval_minutes * 60 * 1000 * 1000;
    ESP_LOGI(TAG, "Sync sleep interval: %d minutes", interval_minutes);
    ESP_LOGI(TAG, "Scheduling sleep after sync interval: %d minutes", interval_minutes);
    ESP_ERROR_CHECK(esp_timer_start_once(s_app.sleep_timer, delay_us));
}

void application_enter_manual_sleep(void)
{
    ESP_LOGI(TAG, "Entering manual deep sleep; stopping local services and WiFi");
    if (s_app.sleep_timer != NULL) {
        esp_timer_stop(s_app.sleep_timer);
    }
    if (ui_manager_is_http_server_running(s_app.ui_mgr)) {
        ui_manager_stop_ap_transfer_mode(s_app.ui_mgr);
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
    power_down_peripherals_for_sleep();
    esp_sleep_enable_ext1_wakeup((1ULL << BOOT_BUTTON_GPIO) | (1ULL << RTC_INT_GPIO), ESP_EXT1_WAKEUP_ANY_LOW);
    esp_deep_sleep_start();
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

static void handle_image_received_async(const char *photo_id, void *ctx)
{
    app_event_t ev = { .type = APP_EVENT_UI_IMAGE_RECEIVED };
    if (photo_id) {
        strncpy(ev.image_received.photo_id, photo_id, sizeof(ev.image_received.photo_id) - 1);
        ev.image_received.photo_id[sizeof(ev.image_received.photo_id) - 1] = '\0';
    }
    if (s_app.event_queue) {
        xQueueSend(s_app.event_queue, &ev, 0);
    }
}

static void handle_settings_changed_async(int minutes, void *ctx)
{
    app_event_t ev = { .type = APP_EVENT_UI_SETTINGS_CHANGED };
    ev.settings_changed.slideshow_interval_minutes = minutes;
    if (s_app.event_queue) {
        xQueueSend(s_app.event_queue, &ev, 0);
    }
}

static void handle_photos_changed_async(void *ctx)
{
    app_event_t ev = { .type = APP_EVENT_UI_PHOTOS_CHANGED };
    if (s_app.event_queue) {
        xQueueSend(s_app.event_queue, &ev, 0);
    }
}

static bool handle_show_photo_sync(const char *photo_id, void *ctx)
{
    SemaphoreHandle_t sem = xSemaphoreCreateBinary();
    if (!sem) {
        return false;
    }
    bool success = false;
    show_photo_event_data_t data = {
        .photo_id = photo_id,
        .out_success = &success,
        .done_sem = sem
    };
    app_event_t ev = {
        .type = APP_EVENT_UI_SHOW_PHOTO,
        .show_photo = { .show_photo_data = &data }
    };
    if (s_app.event_queue && xQueueSend(s_app.event_queue, &ev, 0) == pdTRUE) {
        xSemaphoreTake(sem, portMAX_DELAY);
    }
    vSemaphoreDelete(sem);
    return success;
}
void application_init(void)
{
    memset(&s_app, 0, sizeof(s_app));
    s_app.state          = DEVICE_STATE_STARTING;
    s_app.wifi_connected = false;

    holiday_fetcher_init();
    s_app.event_queue = xQueueCreate(16, sizeof(app_event_t));
    if (!s_app.event_queue) {
        ESP_LOGE(TAG, "Failed to create event queue");
        s_app.state = DEVICE_STATE_FATAL_ERROR;
        return;
    }
    s_app.ui_mgr = ui_manager_create();
    if (!s_app.ui_mgr) {
        ESP_LOGE(TAG, "Failed to allocate UI manager");
        s_app.state = DEVICE_STATE_FATAL_ERROR;
        return;
    }
    ui_manager_init(s_app.ui_mgr, NULL, NULL);
    ui_manager_set_app_callbacks(s_app.ui_mgr,
                                 handle_image_received_async,
                                 handle_settings_changed_async,
                                 handle_photos_changed_async,
                                 handle_show_photo_sync,
                                 NULL);
    /* The refresh callback is provided by the display driver integration in
     * main.c; ui_manager renders into the framebuffer there. */

    build_settings_items();

    int slideshow_interval = ui_manager_get_gallery_slideshow_interval_minutes(s_app.ui_mgr);
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    bool is_rtc_wakeup = false;
    if (cause == ESP_SLEEP_WAKEUP_EXT1) {
        uint64_t pin_mask = esp_sleep_get_ext1_wakeup_status();
        ESP_LOGI(TAG, "Boot wakeup: ext1 pin_mask=0x%llx rtc_int_level=%d", (unsigned long long)pin_mask,
                 gpio_get_level(RTC_INT_GPIO));
        if (pin_mask & (1ULL << RTC_INT_GPIO)) {
            is_rtc_wakeup = true;
        }
    } else {
        ESP_LOGI(TAG, "Boot wakeup: cause=%d rtc_int_level=%d", (int)cause, gpio_get_level(RTC_INT_GPIO));
    }

    if (is_rtc_wakeup) {
        s_app.rtc_wakeup = true;
        ESP_LOGI(TAG, "RTC alarm wakeup detected");
        if (slideshow_interval > 0) {
            ESP_LOGI(TAG, "Slideshow RTC wakeup: switching to fullscreen gallery and advancing");
            ui_manager_switch_page(s_app.ui_mgr, UI_PAGE_GALLERY);
            page_renderer_t *gallery = page_registry_get_instance(UI_PAGE_GALLERY);
            if (gallery) {
                photo_gallery_page_t *g = (photo_gallery_page_t *)gallery;
                g->mode = PHOTO_GALLERY_MODE_FULLSCREEN;
                /* Restore the persisted slideshow position before advancing so
                 * deep sleep (which wipes RAM) does not reset to photo 2/3. */
                settings_handle_t gnvs = settings_open(APP_GALLERY_NS, false);
                if (gnvs) {
                    int saved_idx = (int)settings_get_int(gnvs, "current_idx", -1);
                    settings_close(gnvs);
                    if (saved_idx >= 0) {
                        photo_gallery_set_selected_index(gallery, saved_idx);
                    }
                }
                photo_gallery_select_next(gallery, true);
            }
        }
    }

    if (!(is_rtc_wakeup && slideshow_interval > 0)) {
        /* Pre-load coding plan NVS cache so the page renders instantly on
         * first view, even before WiFi connects. */
        ensure_coding_plan_initialised();
        if (esp_reset_reason() == ESP_RST_DEEPSLEEP) {
            ESP_LOGI(TAG, "Wake from deep sleep: flash activity LED and refresh UI");
            board_flash_activity_led();
            ui_manager_request_active_page_refresh(s_app.ui_mgr);
        }
    }

    /* WiFi status callback. */
    wifi_manager_register_callback(on_wifi_event, NULL);

    /* Initialize weather API client. */
    weather_api_init(CONFIG_WEATHER_API_KEY, NULL, on_weather_update, NULL);

    protocol_init(&s_app.protocol);
    stream_pipeline_init(&s_app.pipeline, &s_app.protocol, s_app.ui_mgr);

    if (is_rtc_wakeup) {
        sm_kick(10000, "boot");
    } else {
        sm_kick(30000, "boot");
    }
    s_app.state = DEVICE_STATE_IDLE;
}

void application_notify_wifi_if_connected(void)
{
    /* WiFi may have connected before application_init registered the event
     * callback; re-sync state and auto-start the LAN HTTP server. */
    if (wifi_manager_is_connected()) {
        s_app.wifi_connected = true;
        protocol_start(&s_app.protocol);
        protocol_open_audio_channel(&s_app.protocol);
        if (!ui_manager_is_lan_http_server_running(s_app.ui_mgr)) {
            char ip[32] = {0};
            wifi_manager_get_ip(ip, sizeof(ip));
            if (ip[0] != '\0') {
                const bool started = ui_manager_start_lan_http_server(s_app.ui_mgr, ip);
                ESP_LOGI(TAG,
                         "LAN HTTP server auto-start (post-init): started=%d "
                         "url=http://%s/",
                         started ? 1 : 0, ip);
                update_http_server_settings_item(started, started ? ip : "");
                update_lan_ip_settings_item(ip);
            }
        }
        start_sntp_clock_sync_once();
        s_app.need_coding_plan_refresh = true;
        s_app.need_weather_fetch       = true;
        s_app.need_holiday_fetch       = true;
        arm_sync_sleep_timer();
    }
}
static void render_low_battery_warning(void)
{
    uint8_t *fb = get_framebuffer();
    if (!fb)
        return;

    rawdraw_clear(fb, STYLE_SCREEN_WIDTH, STYLE_SCREEN_HEIGHT, RAWDRAW_COLOR_WHITE);

    const char *line1 = "电量耗尽";
    const char *line2 = "请充电";
    int         w1    = rawdraw_measure_text_width(line1, &SourceHanSansSC_Medium_slim);
    int         w2    = rawdraw_measure_text_width(line2, &SourceHanSansSC_Medium_slim);
    int         h1    = SourceHanSansSC_Medium_slim.line_height;

    int x1      = (STYLE_SCREEN_WIDTH - w1) / 2;
    int x2      = (STYLE_SCREEN_WIDTH - w2) / 2;
    int total_h = 2 * h1 + 10;
    int y_start = (STYLE_SCREEN_HEIGHT - total_h) / 2;

    rawdraw_draw_text(fb, STYLE_SCREEN_WIDTH, STYLE_SCREEN_HEIGHT, x1, y_start, line1, &SourceHanSansSC_Medium_slim,
                      RAWDRAW_COLOR_BLACK);
    rawdraw_draw_text(fb, STYLE_SCREEN_WIDTH, STYLE_SCREEN_HEIGHT, x2, y_start + h1 + 10, line2,
                      &SourceHanSansSC_Medium_slim, RAWDRAW_COLOR_BLACK);

    request_urgent_full_refresh();
    while (is_refresh_pending()) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void application_run(void)
{
    static int s_cp_refresh_counter = 0;
    app_event_t ev;
    TickType_t last_periodic = xTaskGetTickCount();

    while (true) {
        TickType_t now = xTaskGetTickCount();
        TickType_t elapsed = now - last_periodic;
        TickType_t timeout = (elapsed >= pdMS_TO_TICKS(1000)) ? 0 : (pdMS_TO_TICKS(1000) - elapsed);

        if (xQueueReceive(s_app.event_queue, &ev, timeout) == pdTRUE) {
            sm_kick(30000, "user_interaction");
            switch (ev.type) {
            case APP_EVENT_UP_CLICK: {
                ESP_LOGI(TAG, "Processing UP click");
                board_flash_activity_led();
                ui_button_event_t button_ev = {BTN_UP_CLICK};
                ui_manager_handle_input(s_app.ui_mgr, &button_ev);
                break;
            }
            case APP_EVENT_DOWN_CLICK: {
                ESP_LOGI(TAG, "Processing DOWN click");
                board_flash_activity_led();
                ui_button_event_t button_ev = {BTN_DOWN_CLICK};
                ui_manager_handle_input(s_app.ui_mgr, &button_ev);
                break;
            }
            case APP_EVENT_UP_DOUBLE_CLICK: {
                ESP_LOGI(TAG, "Processing UP double click");
                board_flash_activity_led();
                ui_button_event_t button_ev = {BTN_UP_DOUBLE_CLICK};
                ui_manager_handle_input(s_app.ui_mgr, &button_ev);
                break;
            }
            case APP_EVENT_BOOT_DOUBLE_CLICK: {
                ESP_LOGI(TAG, "Processing BOOT double click");
                board_flash_activity_led();
                ui_button_event_t button_ev = {BTN_BOOT_DOUBLE_CLICK};
                ui_manager_handle_input(s_app.ui_mgr, &button_ev);
                break;
            }
            case APP_EVENT_DOWN_DOUBLE_CLICK: {
                ESP_LOGI(TAG, "Processing DOWN double click");
                board_flash_activity_led();
                ui_button_event_t button_ev = {BTN_DOWN_DOUBLE_CLICK};
                ui_manager_handle_input(s_app.ui_mgr, &button_ev);
                break;
            }
            case APP_EVENT_UP_LONG_PRESS: {
                ESP_LOGI(TAG, "Processing UP long press");
                board_flash_activity_led();
                if (ui_manager_get_current_page(s_app.ui_mgr) == UI_PAGE_SETTINGS) {
                    ESP_LOGI(TAG, "UP long press - leaving settings");
                    ui_manager_switch_page(s_app.ui_mgr, UI_PAGE_GALLERY);
                }
                break;
            }
            case APP_EVENT_DOWN_LONG_PRESS: {
                ESP_LOGI(TAG, "Processing DOWN long press");
                board_flash_activity_led();
                ui_manager_switch_page(s_app.ui_mgr, UI_PAGE_SETTINGS);
                break;
            }
            case APP_EVENT_WIFI_CONFIG_COMBO_LONG_PRESS: {
                ESP_LOGI(TAG, "Processing UP+DOWN long press (wifi config)");
                board_flash_activity_led();
                if (ui_manager_is_lan_http_server_running(s_app.ui_mgr)) {
                    ui_manager_stop_lan_http_server(s_app.ui_mgr);
                }
                s_app.wifi_connected = false;
                char ssid[32] = "ZecTrix-AP";
                char pwd[32]  = "12345678";
                char url[32]  = "http://192.168.4.1";
                wifi_manager_get_ssid(ssid, sizeof(ssid));
                ui_manager_show_wifi_config_page(s_app.ui_mgr, ssid, pwd, url);
                application_update_status_bar();
                break;
            }
            case APP_EVENT_BOOT_CLICK: {
                ESP_LOGI(TAG, "Processing BOOT click");
                board_flash_activity_led();
                ui_button_event_t button_ev = {BTN_BOOT_CLICK};
                ui_manager_handle_input(s_app.ui_mgr, &button_ev);
                break;
            }
            case APP_EVENT_BOOT_LONG_PRESS: {
                ESP_LOGI(TAG, "Processing BOOT long press");
                board_flash_activity_led();
                ui_button_event_t button_ev = {BTN_BOOT_LONG_PRESS};
                ui_manager_handle_input(s_app.ui_mgr, &button_ev);
                break;
            }
            case APP_EVENT_WIFI: {
                ESP_LOGI(TAG, "Processing WiFi event: %d", ev.wifi_event);
                process_wifi_event(ev.wifi_event);
                break;
            }
            case APP_EVENT_TIME_SYNC: {
                ESP_LOGI(TAG, "Time sync: refreshing calendar");
                page_renderer_t *cal_page = (page_renderer_t *)ui_manager_get_renderer(s_app.ui_mgr, UI_PAGE_CALENDAR);
                if (cal_page) {
                    calendar_page_t *cal = (calendar_page_t *)cal_page;
                    time_t    now_t = time(NULL);
                    struct tm tm_buf;
                    localtime_r(&now_t, &tm_buf);
                    cal->today_year  = tm_buf.tm_year + 1900;
                    cal->today_month = tm_buf.tm_mon + 1;
                    cal->today_day   = tm_buf.tm_mday;
                    cal->cal.today_year  = cal->today_year;
                    cal->cal.today_month = cal->today_month;
                    cal->cal.today_day   = cal->today_day;
                    cal->year  = cal->today_year;
                    cal->month = cal->today_month;
                    widget_calendar_set_date(&cal->cal, cal->year, cal->month);
                    cal->base.needs_full_refresh_flag = true;
                }
                application_update_status_bar();
                break;
            }
            case APP_EVENT_UI_IMAGE_RECEIVED: {
                ESP_LOGI(TAG, "Processing UI_IMAGE_RECEIVED: %s", ev.image_received.photo_id);
                photo_gallery_refresh_photo_list((page_renderer_t *)page_registry_get_instance(UI_PAGE_GALLERY));
                int count = photo_gallery_get_photo_count((page_renderer_t *)page_registry_get_instance(UI_PAGE_GALLERY));
                if (count > 0 && ev.image_received.photo_id[0] != '\0') {
                    photo_gallery_set_selected_by_id((page_renderer_t *)page_registry_get_instance(UI_PAGE_GALLERY), ev.image_received.photo_id);
                }
                ui_manager_request_active_page_refresh(s_app.ui_mgr);
                break;
            }
            case APP_EVENT_UI_SETTINGS_CHANGED: {
                int minutes = ev.settings_changed.slideshow_interval_minutes;
                ESP_LOGI(TAG, "Processing UI_SETTINGS_CHANGED: %d min", minutes);
                ui_manager_set_gallery_slideshow_interval_minutes(s_app.ui_mgr, minutes);
                char label[16];
                if (minutes <= 0) {
                    snprintf(label, sizeof(label), "关闭");
                } else {
                    snprintf(label, sizeof(label), "%dmin", minutes);
                }
                ui_manager_update_settings_item(s_app.ui_mgr, 3, label);
                ui_manager_request_active_page_refresh(s_app.ui_mgr);
                break;
            }
            case APP_EVENT_UI_PHOTOS_CHANGED: {
                ESP_LOGI(TAG, "Processing UI_PHOTOS_CHANGED");
                photo_gallery_refresh_photo_list((page_renderer_t *)page_registry_get_instance(UI_PAGE_GALLERY));
                break;
            }
            case APP_EVENT_UI_SHOW_PHOTO: {
                show_photo_event_data_t *data = (show_photo_event_data_t *)ev.show_photo.show_photo_data;
                if (data) {
                    ESP_LOGI(TAG, "Processing UI_SHOW_PHOTO: %s", data->photo_id);
                    *data->out_success = ui_manager_show_photo_by_id(s_app.ui_mgr, data->photo_id);
                    xSemaphoreGive(data->done_sem);
                }
                break;
            }
            default:
                break;
            }
        }

        now = xTaskGetTickCount();
        if ((now - last_periodic) >= pdMS_TO_TICKS(1000)) {
            last_periodic = now;

            int pct = charge_status_get_battery_percent();
            if (pct > 0 && pct <= 3 && !charge_status_is_charging()) {
                ESP_LOGW(TAG, "Battery low (%d%%) and not charging. Shutting down...", pct);
                render_low_battery_warning();
                board_power_vbat_off();
                esp_deep_sleep_start();
            }
            ui_manager_pump_clock_refresh(s_app.ui_mgr);

            if (s_app.wifi_connected) {
                if (s_app.need_weather_fetch) {
                    s_app.need_weather_fetch = false;
                    weather_api_fetch();
                }
                if (s_app.need_coding_plan_refresh) {
                    s_app.need_coding_plan_refresh = false;
                    refresh_coding_plan();
                }
                if (s_app.need_holiday_fetch) {
                    s_app.need_holiday_fetch = false;
                    time_t    t   = time(NULL);
                    struct tm tmr;
                    localtime_r(&t, &tmr);
                    int year = tmr.tm_year + 1900;
                    if (holiday_fetcher_fetch(year)) {
                        ui_manager_request_active_page_refresh(s_app.ui_mgr);
                    }
                }

                if (++s_cp_refresh_counter >= APP_CODING_PLAN_REFRESH_SECONDS) {
                    s_cp_refresh_counter = 0;
                    refresh_coding_plan();
                }
            } else {
                s_cp_refresh_counter           = 0;
                s_app.need_weather_fetch       = false;
                s_app.need_coding_plan_refresh = false;
                s_app.need_holiday_fetch       = false;
            }

            if (sm_can_sleep_now() && !is_local_http_service_running()) {
                ESP_LOGI(TAG, "System idle. Sleep manager allows sleep. Entering sleep...");
                enter_scheduled_sleep();
            }
        }
    }
}


device_state_t application_get_device_state(void)
{
    return s_app.state;
}

bool application_set_device_state(device_state_t state)
{
    const device_state_t old = s_app.state;
    s_app.state              = state;
    ESP_LOGI(TAG, "State %d -> %d", old, state);
    return true;
}

void *application_get_ui_manager(void)
{
    return s_app.ui_mgr;
}

void application_update_status_bar(void)
{
    ui_manager_status_bar_t data;
    ui_manager_get_status_bar_data(s_app.ui_mgr, &data);

    int battery_level = charge_status_get_battery_percent();
    if (battery_level < 0)
        battery_level = -1;

    strncpy(data.page_title, ui_manager_get_page_title(ui_manager_get_current_page(s_app.ui_mgr)),
            sizeof(data.page_title) - 1);
    data.page_title[sizeof(data.page_title) - 1] = '\0';
    data.wifi_connected                          = s_app.wifi_connected;
    data.server_connected                        = ui_manager_is_http_server_running(s_app.ui_mgr);
    data.battery_level                           = battery_level;

    ui_manager_update_status_bar(s_app.ui_mgr, &data);

    update_wifi_settings_item(s_app.wifi_connected, NULL);
    char lan_ip[32] = {0};
    if (s_app.wifi_connected) {
        wifi_manager_get_ip(lan_ip, sizeof(lan_ip));
    }
    update_lan_ip_settings_item(lan_ip);
    update_http_server_settings_item(ui_manager_is_lan_http_server_running(s_app.ui_mgr), lan_ip);

    ui_manager_request_active_page_refresh(s_app.ui_mgr);
}

/* ------------------------------------------------------------------ */
/* Button routing                                                      */
/* ------------------------------------------------------------------ */

void application_on_up_click(void)
{
    app_event_t ev = { .type = APP_EVENT_UP_CLICK };
    if (s_app.event_queue) {
        xQueueSend(s_app.event_queue, &ev, 0);
    }
}

void application_on_down_click(void)
{
    app_event_t ev = { .type = APP_EVENT_DOWN_CLICK };
    if (s_app.event_queue) {
        xQueueSend(s_app.event_queue, &ev, 0);
    }
}

void application_on_up_double_click(void)
{
    app_event_t ev = { .type = APP_EVENT_UP_DOUBLE_CLICK };
    if (s_app.event_queue) {
        xQueueSend(s_app.event_queue, &ev, 0);
    }
}

void application_on_boot_double_click(void)
{
    app_event_t ev = { .type = APP_EVENT_BOOT_DOUBLE_CLICK };
    if (s_app.event_queue) {
        xQueueSend(s_app.event_queue, &ev, 0);
    }
}

void application_on_down_double_click(void)
{
    app_event_t ev = { .type = APP_EVENT_DOWN_DOUBLE_CLICK };
    if (s_app.event_queue) {
        xQueueSend(s_app.event_queue, &ev, 0);
    }
}

void application_on_up_long_press(void)
{
    app_event_t ev = { .type = APP_EVENT_UP_LONG_PRESS };
    if (s_app.event_queue) {
        xQueueSend(s_app.event_queue, &ev, 0);
    }
}

void application_on_down_long_press(void)
{
    app_event_t ev = { .type = APP_EVENT_DOWN_LONG_PRESS };
    if (s_app.event_queue) {
        xQueueSend(s_app.event_queue, &ev, 0);
    }
}

void application_on_wifi_config_combo_long_press(void)
{
    app_event_t ev = { .type = APP_EVENT_WIFI_CONFIG_COMBO_LONG_PRESS };
    if (s_app.event_queue) {
        xQueueSend(s_app.event_queue, &ev, 0);
    }
}

void application_on_boot_click(void)
{
    app_event_t ev = { .type = APP_EVENT_BOOT_CLICK };
    if (s_app.event_queue) {
        xQueueSend(s_app.event_queue, &ev, 0);
    }
}

void application_on_boot_long_press(void)
{
    app_event_t ev = { .type = APP_EVENT_BOOT_LONG_PRESS };
    if (s_app.event_queue) {
        xQueueSend(s_app.event_queue, &ev, 0);
    }
}


static void on_weather_update(const weather_data_t *data, void *user_data)
{
    (void)user_data;
    if (data) {
        ESP_LOGI(TAG, "Weather callback fired, updating weather page");
        page_renderer_t *weather_page = (page_renderer_t *)ui_manager_get_renderer(s_app.ui_mgr, UI_PAGE_WEATHER);
        if (weather_page) {
            weather_page_update(weather_page, data);
            weather_page_set_city_name(weather_page, weather_api_get_city_name());
            ui_manager_request_active_page_refresh(s_app.ui_mgr);
        }
    }
}
