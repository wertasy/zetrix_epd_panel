/**
 * @file app_settings_menu.c
 * @brief Settings menu construction, settings item updates, and the
 *        settings callback dispatcher.
 *
 * Extracted from application.c (Phase 2.1 module split). All functions
 * access the shared s_app singleton via application_internal.h.
 */
#include "application_internal.h"
#include "app_page_runtime.h"
#include <string.h>

#include <esp_log.h>
#include <esp_mac.h>
#include <esp_system.h>
#include <esp_timer.h>

#include "settings.h"
#include "settings_page.h"
#include "ui_manager.h"
#include "wifi_manager.h"

#ifndef PROJECT_VER
#    define PROJECT_VER "3.8.0"
#endif

#define TAG "Application"

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

/* ------------------------------------------------------------------ */
/* Settings item updates                                               */
/* ------------------------------------------------------------------ */

void app_settings_update_wifi_item(bool connected, const char *value)
{
    ui_manager_update_settings_checked(s_app.ui_mgr, APP_SETTINGS_WIFI_INDEX, connected);
    if (value) {
        ui_manager_update_settings_item(s_app.ui_mgr, APP_SETTINGS_WIFI_INDEX, value);
    } else {
        ui_manager_update_settings_item(s_app.ui_mgr, APP_SETTINGS_WIFI_INDEX, connected ? "已连接" : "未连接");
    }
}

void app_settings_update_http_server_item(bool running, const char *ip)
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

void app_settings_update_lan_ip_item(const char *ip)
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

void app_settings_menu_cb(void *ctx)
{
    const intptr_t action = (intptr_t)ctx;
    switch (action) {
    case 1: { /* Slideshow interval cycle. */
        settings_handle_t nvs = settings_open(APP_GALLERY_NS, true);
        int current = 5;
        if (nvs) {
            current = (int)settings_get_int(nvs, APP_SLIDESHOW_KEY, 5);
        }
        static const int options[] = {0, 5, 10, 30};
        int next = 5;
        for (unsigned i = 0; i < sizeof(options) / sizeof(options[0]); ++i) {
            if (options[i] == current) {
                next = options[(i + 1) % (sizeof(options) / sizeof(options[0]))];
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
        /* Runtime override drives wake/sleep intervals from the policy
         * framework; re-arm the sync timer with the new effective value. */
        page_runtime_set_wake_interval_override(UI_PAGE_GALLERY, next);
        if (s_app.wifi_connected || wifi_manager_is_connected()) {
            app_sleep_arm_sync_timer();
        }
        break;
    }
    case 6: { /* Sync sleep interval cycle (D6): 30 → 60 → 120. */
        static const int options[] = {30, 60, 120};
        settings_handle_t sync_nvs = settings_open(APP_SYNC_NS, true);
        int current = APP_DEFAULT_SYNC_INTERVAL_MIN;
        if (sync_nvs) {
            current = (int)settings_get_int(sync_nvs, APP_SYNC_INTERVAL_KEY, APP_DEFAULT_SYNC_INTERVAL_MIN);
        }
        int next = options[0];
        for (unsigned i = 0; i < sizeof(options) / sizeof(options[0]); ++i) {
            if (options[i] == current) {
                next = options[(i + 1) % (sizeof(options) / sizeof(options[0]))];
                break;
            }
        }
        if (sync_nvs) {
            settings_set_int(sync_nvs, APP_SYNC_INTERVAL_KEY, next);
            settings_close(sync_nvs);
        }
        /* Invalidate the RAM cache in app_sleep so the next arm re-reads. */
        app_sleep_invalidate_sync_interval_cache();
        if (s_app.wifi_connected || wifi_manager_is_connected()) {
            app_sleep_arm_sync_timer();
        }
        char label[16];
        format_minutes_label(next, label, sizeof(label));
        ui_manager_update_settings_item(s_app.ui_mgr, APP_SETTINGS_SYNC_INTERVAL_INDEX, label);
        break;
    }
    case 2: { /* WiFi toggle. */
        if (s_app.wifi_connected || wifi_manager_is_connected()) {
            ESP_LOGI(TAG, "Wi-Fi setting toggled OFF");
            if (app_is_lan_http_running()) {
                app_page_runtime_service_release_user(APP_SVC_LAN_HTTP);
                app_settings_update_http_server_item(false, NULL);
            }
            wifi_manager_disconnect();
            s_app.wifi_connected = false;
            app_settings_update_wifi_item(false, NULL);
            app_settings_update_lan_ip_item("");
        } else {
            ESP_LOGI(TAG, "Wi-Fi setting toggled ON");
            app_settings_update_wifi_item(false, "连接中");
            wifi_manager_connect("", "");
        }
        application_update_status_bar();
        break;
    }
    case 3: { /* LAN HTTP server toggle. */
        if (app_is_lan_http_running()) {
            ESP_LOGI(TAG, "LAN HTTP server toggled OFF");
            app_page_runtime_service_release_user(APP_SVC_LAN_HTTP);
            app_settings_update_http_server_item(false, NULL);
            application_update_status_bar();
            if (s_app.wifi_connected || wifi_manager_is_connected()) {
                app_sleep_arm_sync_timer();
            }
            return;
        }
        if (!s_app.wifi_connected && !wifi_manager_is_connected()) {
            ESP_LOGW(TAG, "LAN HTTP server requires WiFi connection");
            app_settings_update_http_server_item(false, "需先连接WiFi");
            application_update_status_bar();
            return;
        }
        char ip[32] = {0};
        wifi_manager_get_ip(ip, sizeof(ip));
        if (ip[0] == '\0') {
            ESP_LOGW(TAG, "LAN HTTP server requires station IP");
            app_settings_update_http_server_item(false, "等待IP");
            application_update_status_bar();
            return;
        }
        /* USER ownership: survives page switches until user off / idle
         * timeout (D3) / sleep teardown. */
        const bool started = app_page_runtime_service_acquire(APP_SVC_LAN_HTTP, SVC_OWNER_USER, UI_PAGE_SETTINGS);
        ESP_LOGI(TAG, "LAN HTTP server toggled ON: started=%d url=http://%s/", started ? 1 : 0, ip);
        if (started && s_app.sleep_timer != NULL) {
            esp_timer_stop(s_app.sleep_timer);
            ESP_LOGI(TAG, "Sync sleep timer paused while LAN HTTP server is running");
        }
        app_settings_update_http_server_item(started, started ? ip : "");
        app_settings_update_lan_ip_item(started ? ip : "");
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

void app_settings_menu_build(void)
{
    settings_handle_t gallery_nvs = settings_open(APP_GALLERY_NS, false);
    int slideshow_interval = 5;
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
    int n = 0;
    memset(items, 0, sizeof(items));

    /* Section: 系统 */
    strcpy(items[n].label, "系统");
    items[n].type = SETTINGS_ITEM_SECTION;
    ++n;

    /* 重启 (action) */
    strcpy(items[n].label, "重启");
    strcpy(items[n].value, "执行");
    items[n].type = SETTINGS_ITEM_ACTION;
    items[n].on_click = app_settings_menu_cb;
    items[n].on_click_ctx = (void *)(intptr_t)5;
    ++n;

    /* Section: 相册 */
    strcpy(items[n].label, "相册");
    items[n].type = SETTINGS_ITEM_SECTION;
    ++n;

    /* 轮播间隔 (action) */
    strcpy(items[n].label, "轮播间隔");
    format_minutes_label(slideshow_interval, items[n].value, sizeof(items[n].value));
    items[n].type = SETTINGS_ITEM_ACTION;
    items[n].on_click = app_settings_menu_cb;
    items[n].on_click_ctx = (void *)(intptr_t)1;
    ++n;

    /* Section: 网络 */
    strcpy(items[n].label, "网络");
    items[n].type = SETTINGS_ITEM_SECTION;
    ++n;

    /* Wi-Fi (checkbox) */
    strcpy(items[n].label, "Wi-Fi");
    strcpy(items[n].value, "未连接");
    items[n].type = SETTINGS_ITEM_CHECKBOX;
    items[n].on_click = app_settings_menu_cb;
    items[n].on_click_ctx = (void *)(intptr_t)2;
    ++n;

    /* 局域网服务 (checkbox) */
    strcpy(items[n].label, "局域网服务");
    strcpy(items[n].value, "已关闭");
    items[n].type = SETTINGS_ITEM_CHECKBOX;
    items[n].on_click = app_settings_menu_cb;
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
    items[n].type = SETTINGS_ITEM_ACTION;
    items[n].on_click = app_settings_menu_cb;
    ++n;

    /* 固件 (normal) */
    strcpy(items[n].label, "固件");
    strcpy(items[n].value, PROJECT_VER);
    items[n].type = SETTINGS_ITEM_NORMAL;
    ++n;

    /* Section: 关于 */
    strcpy(items[n].label, "关于");
    items[n].type = SETTINGS_ITEM_SECTION;
    ++n;

    /* 刷新间隔 (action, D6): 30 → 60 → 120 minutes, never 0.
     * 0 means "never auto-sleep" in NVS semantics and stays an advanced
     * NVS-only value; alignment pages (calendar) refresh on content
     * change and ignore this interval anyway. */
    strcpy(items[n].label, "刷新间隔");
    int sync_interval = APP_DEFAULT_SYNC_INTERVAL_MIN;
    {
        settings_handle_t sync_nvs = settings_open(APP_SYNC_NS, false);
        if (sync_nvs) {
            sync_interval = (int)settings_get_int(sync_nvs, APP_SYNC_INTERVAL_KEY, APP_DEFAULT_SYNC_INTERVAL_MIN);
            settings_close(sync_nvs);
        }
        if (sync_interval != 30 && sync_interval != 60 && sync_interval != 120) {
            sync_interval = 30; /* unknown/NVS-only values display as default */
        }
        format_minutes_label(sync_interval, items[n].value, sizeof(items[n].value));
    }
    items[n].type = SETTINGS_ITEM_ACTION;
    items[n].on_click = app_settings_menu_cb;
    items[n].on_click_ctx = (void *)(intptr_t)6;
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
