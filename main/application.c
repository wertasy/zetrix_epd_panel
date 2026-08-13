/**
 * @file application.c
 * @brief Application singleton — C port of C++ Application.
 *
 * Owns the RawDraw UI manager (via ui_manager.h), routes button events,
 * manages WiFi / sync-sleep state, and updates the status bar. Audio
 * service is omitted (Phase 4 audio pipeline parked); sound feedback
 * uses the existing audio_player module.
 *
 * Phase 2.1: sync orchestration, sleep management, SNTP, and settings
 * menu construction have been extracted into app_sync.c, app_sleep.c,
 * app_sntp.c, and app_settings_menu.c respectively. The shared singleton
 * state and cross-module declarations live in application_internal.h.
 */
#include "application.h"
#include "application_internal.h"

#include <string.h>
#include <time.h>
#include <sys/time.h>

#include <esp_log.h>
#include <esp_mac.h>
#include <esp_sleep.h>
#include <esp_bit_defs.h>
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
#include "epd_driver.h"
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
#include "ap_transfer_server.h"
#include "ap_transfer_page.h"
#include "data_refresh.h"

#define TAG "Application"

#ifdef ESP_PLATFORM
#    include "esp_attr.h"
#else
#    ifndef EXT_RAM_BSS_ATTR
#        define EXT_RAM_BSS_ATTR
#    endif
#endif

/* ------------------------------------------------------------------ */
/* Singleton state (extern in application_internal.h)                  */
/* ------------------------------------------------------------------ */

EXT_RAM_BSS_ATTR application_t s_app;

/* AP/HTTP transfer server — owned by application (moved from ui_manager). */
ap_transfer_server_t s_transfer_server;

/* Last battery % pushed to the status bar, so the periodic loop only
 * re-refreshes when it actually changes. battery_level is only written by
 * application_update_status_bar(); with no independent refresh trigger the
 * icon would freeze at its boot value (often -1 while the ADC initialises)
 * and never appear unless a wifi/settings event happens to update it. */
static int s_last_battery_pct = -999;
/* ------------------------------------------------------------------ */
/* Static helpers                                                      */
/* ------------------------------------------------------------------ */

/* Adapts ui_manager_append_chat_text(const ui_manager_t*, const char*) to text_chunk_cb_t void(const char*, void*) */
static void stream_text_chunk_cb(const char *chunk, void *ctx)
{
    ui_manager_append_chat_text((ui_manager_t *)ctx, chunk);
}

bool app_is_lan_http_running(void)
{
    return ap_transfer_server_is_running(&s_transfer_server) && ap_transfer_server_is_lan_mode(&s_transfer_server);
}

static bool is_ap_transfer_mode_running(void)
{
    return ap_transfer_server_is_running(&s_transfer_server) && ap_transfer_server_is_ap_mode(&s_transfer_server);
}

/* AP transfer server state callback — updates the AP transfer page UI. */
static void ap_server_state_cb(int state, const char *message, void *ctx)
{
    (void)ctx;
    bool should_refresh = false;
    switch (state) {
    case 1: /* kApStarted */
        ap_transfer_page_set_state((page_renderer_t *)page_registry_get_instance(UI_PAGE_AP_TRANSFER),
                                   AP_TRANSFER_STATE_WAITING_CONNECTION, message);
        should_refresh = true;
        break;
    case 2: /* kClientConnected */
        ap_transfer_page_set_state((page_renderer_t *)page_registry_get_instance(UI_PAGE_AP_TRANSFER),
                                   AP_TRANSFER_STATE_CLIENT_CONNECTED, message);
        break;
    case 5: /* kImageSaved */
        ap_transfer_page_set_state((page_renderer_t *)page_registry_get_instance(UI_PAGE_AP_TRANSFER),
                                   AP_TRANSFER_STATE_COMPLETE, message);
        should_refresh = true;
        break;
    case 6: /* kError */
        ap_transfer_page_set_state((page_renderer_t *)page_registry_get_instance(UI_PAGE_AP_TRANSFER),
                                   AP_TRANSFER_STATE_ERROR, message);
        should_refresh = true;
        break;
    case 0: /* kStopped */
    default:
        ap_transfer_page_set_state((page_renderer_t *)page_registry_get_instance(UI_PAGE_AP_TRANSFER),
                                   AP_TRANSFER_STATE_WAITING_CONNECTION, message);
        should_refresh = true;
        break;
    }
    if (should_refresh && ui_manager_get_current_page(s_app.ui_mgr) == UI_PAGE_AP_TRANSFER) {
        ui_manager_request_active_page_refresh(s_app.ui_mgr);
    }
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
        app_sntp_start_once();
        if (ui_manager_get_current_page(s_app.ui_mgr) == UI_PAGE_AP_TRANSFER && !is_ap_transfer_mode_running()) {
            ESP_LOGI(TAG, "WiFi connected while config page is visible, "
                          "returning to gallery");
            ui_manager_switch_page(s_app.ui_mgr, UI_PAGE_GALLERY);
        }
        application_update_status_bar();
        app_sleep_arm_sync_timer();
        s_app.need_coding_plan_refresh = true;
        s_app.need_weather_fetch = true;
        s_app.need_holiday_fetch = true;
        break;
    case WIFI_EVENT_GOT_IP:
        application_update_status_bar();
        break;
    case WIFI_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "WiFi disconnected");
        s_app.wifi_connected = false;
        sm_set_busy(SLEEP_BUSY_SRC_NET, false);
        if (app_is_lan_http_running()) {
            ap_transfer_server_stop(&s_transfer_server);
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
    app_event_t ev = {.type = APP_EVENT_WIFI, .wifi_event = event};
    if (s_app.event_queue) {
        xQueueSend(s_app.event_queue, &ev, 0);
    }
}

/* ------------------------------------------------------------------ */
/* Async event handlers (post to the main event queue)                 */
/* ------------------------------------------------------------------ */

static void handle_image_received_async(const char *photo_id, void *ctx)
{
    app_event_t ev = {.type = APP_EVENT_UI_IMAGE_RECEIVED};
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
    app_event_t ev = {.type = APP_EVENT_UI_SETTINGS_CHANGED};
    ev.settings_changed.slideshow_interval_minutes = minutes;
    if (s_app.event_queue) {
        xQueueSend(s_app.event_queue, &ev, 0);
    }
}

static void handle_photos_changed_async(void *ctx)
{
    app_event_t ev = {.type = APP_EVENT_UI_PHOTOS_CHANGED};
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
    show_photo_event_data_t data = {.photo_id = photo_id, .out_success = &success, .done_sem = sem};
    app_event_t ev = {.type = APP_EVENT_UI_SHOW_PHOTO, .show_photo = {.show_photo_data = &data}};
    if (s_app.event_queue && xQueueSend(s_app.event_queue, &ev, 0) == pdTRUE) {
        xSemaphoreTake(sem, portMAX_DELAY);
    }
    vSemaphoreDelete(sem);
    return success;
}
/* ------------------------------------------------------------------ */
/* Application init                                                    */
/* ------------------------------------------------------------------ */

void application_init(void)
{
    memset(&s_app, 0, sizeof(s_app));
    s_app.state = DEVICE_STATE_STARTING;
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
    /* AP/HTTP transfer server — lifecycle and callbacks owned by application. */
    ap_transfer_server_init(&s_transfer_server);
    ap_transfer_server_set_state_callback(&s_transfer_server, ap_server_state_cb, NULL);
    ap_transfer_server_set_image_received_callback(&s_transfer_server, handle_image_received_async, NULL);
    ap_transfer_server_set_settings_changed_callback(&s_transfer_server, handle_settings_changed_async, NULL);
    ap_transfer_server_set_photos_changed_callback(&s_transfer_server, handle_photos_changed_async, NULL);
    ap_transfer_server_set_show_photo_callback(&s_transfer_server, handle_show_photo_sync, NULL);
    /* Data refresh request channel — pages call data_refresh_request() instead
     * of directly invoking network API fetch functions (Phase 1.4 decoupling). */
    data_refresh_set_callback(app_sync_on_data_refresh_request, NULL);
    /* The refresh callback is provided by the display driver integration in
     * main.c; ui_manager renders into the framebuffer there. */

    app_settings_menu_build();

    int slideshow_interval = ui_manager_get_gallery_slideshow_interval_minutes(s_app.ui_mgr);
    const uint32_t wakeup_causes = esp_sleep_get_wakeup_causes();
    bool is_rtc_wakeup = false;
    if (wakeup_causes & BIT(ESP_SLEEP_WAKEUP_EXT1)) {
        uint64_t pin_mask = esp_sleep_get_ext1_wakeup_status();
        ESP_LOGI(TAG, "Boot wakeup: ext1 pin_mask=0x%llx rtc_int_level=%d", (unsigned long long)pin_mask,
                 gpio_get_level(RTC_INT_GPIO));
        if (pin_mask & (1ULL << RTC_INT_GPIO)) {
            is_rtc_wakeup = true;
        }
    } else {
        ESP_LOGI(TAG, "Boot wakeup: causes=0x%lx rtc_int_level=%d", (unsigned long)wakeup_causes,
                 gpio_get_level(RTC_INT_GPIO));
    }

    if (is_rtc_wakeup) {
        s_app.rtc_wakeup = true;
        ESP_LOGI(TAG, "RTC alarm wakeup detected");
        /* Only run the slideshow fast-path when the last-viewed page before
         * sleep was the gallery with the slideshow enabled. Otherwise keep
         * the restored page (from RTC memory) and let the normal data sync
         * refresh it — non-gallery pages need their network data after wake. */
        const ui_page_id_t saved_page = ui_manager_get_rtc_saved_page();
        if (slideshow_interval > 0 && saved_page == UI_PAGE_GALLERY) {
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
        app_sync_ensure_coding_plan_initialised();
        if (esp_reset_reason() == ESP_RST_DEEPSLEEP) {
            ESP_LOGI(TAG, "Wake from deep sleep: showing persisted frame until fresh data arrives");
        }
    }

    /* WiFi status callback. */
    wifi_manager_register_callback(on_wifi_event, NULL);

    /* Initialize weather API client. */
    weather_api_init(CONFIG_WEATHER_API_KEY, NULL, app_sync_on_weather_update, NULL);

    protocol_init(&s_app.protocol);
    stream_pipeline_init(&s_app.pipeline, &s_app.protocol, stream_text_chunk_cb, s_app.ui_mgr);

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
     * callback; re-sync state. The LAN HTTP server stays OFF by default —
     * it is only started by the user (gallery BOOT long-press or settings). */
    if (wifi_manager_is_connected()) {
        s_app.wifi_connected = true;
        protocol_start(&s_app.protocol);
        protocol_open_audio_channel(&s_app.protocol);
        app_sntp_start_once();
        s_app.need_coding_plan_refresh = true;
        s_app.need_weather_fetch = true;
        s_app.need_holiday_fetch = true;
        app_sleep_arm_sync_timer();
    }
}

/* ------------------------------------------------------------------ */
/* Low battery warning                                                 */
/* ------------------------------------------------------------------ */

static void render_low_battery_warning(void)
{
    uint8_t *fb = get_framebuffer();
    if (!fb)
        return;

    rawdraw_clear(fb, STYLE_SCREEN_WIDTH, STYLE_SCREEN_HEIGHT, RAWDRAW_COLOR_WHITE);

    const char *line1 = "电量耗尽";
    const char *line2 = "请充电";
    int w1 = rawdraw_measure_text_width(line1, &SourceHanSansSC_Medium_slim);
    int w2 = rawdraw_measure_text_width(line2, &SourceHanSansSC_Medium_slim);
    int h1 = SourceHanSansSC_Medium_slim.line_height;

    int x1 = (STYLE_SCREEN_WIDTH - w1) / 2;
    int x2 = (STYLE_SCREEN_WIDTH - w2) / 2;
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

/* Forward a simple button event to the UI manager (log + LED + dispatch). */
static void forward_ui_button(button_event_type_t btn, const char *label)
{
    ESP_LOGI(TAG, "Processing %s", label);
    board_flash_activity_led();
    ui_button_event_t button_ev = {btn};
    ui_manager_handle_input(s_app.ui_mgr, &button_ev);
}

/* ------------------------------------------------------------------ */
/* Main event loop                                                     */
/* ------------------------------------------------------------------ */

void application_run(void)
{
    app_event_t ev;
    TickType_t last_periodic = xTaskGetTickCount();

    while (true) {
        TickType_t now = xTaskGetTickCount();
        TickType_t elapsed = now - last_periodic;
        TickType_t timeout = (elapsed >= pdMS_TO_TICKS(1000)) ? 0 : (pdMS_TO_TICKS(1000) - elapsed);

        if (xQueueReceive(s_app.event_queue, &ev, timeout) == pdTRUE) {
            sm_kick(30000, "user_interaction");
            switch (ev.type) {
            case APP_EVENT_UP_CLICK:
                forward_ui_button(BTN_UP_CLICK, "UP click");
                break;
            case APP_EVENT_DOWN_CLICK:
                forward_ui_button(BTN_DOWN_CLICK, "DOWN click");
                break;
            case APP_EVENT_UP_DOUBLE_CLICK:
                forward_ui_button(BTN_UP_DOUBLE_CLICK, "UP double click");
                break;
            case APP_EVENT_BOOT_DOUBLE_CLICK:
                forward_ui_button(BTN_BOOT_DOUBLE_CLICK, "BOOT double click");
                break;
            case APP_EVENT_DOWN_DOUBLE_CLICK:
                forward_ui_button(BTN_DOWN_DOUBLE_CLICK, "DOWN double click");
                break;
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
                if (app_is_lan_http_running()) {
                    ap_transfer_server_stop(&s_transfer_server);
                }
                s_app.wifi_connected = false;
                char ssid[32] = "ZecTrix-AP";
                char pwd[32] = "12345678";
                char url[32] = "http://192.168.4.1";
                wifi_manager_get_ssid(ssid, sizeof(ssid));
                ui_manager_show_wifi_config_page(s_app.ui_mgr, ssid, pwd, url);
                application_update_status_bar();
                break;
            }
            case APP_EVENT_BOOT_CLICK:
                forward_ui_button(BTN_BOOT_CLICK, "BOOT click");
                break;
            case APP_EVENT_BOOT_LONG_PRESS: {
                ESP_LOGI(TAG, "Processing BOOT long press");
                board_flash_activity_led();
                ui_page_id_t boot_page = ui_manager_get_current_page(s_app.ui_mgr);
                if (boot_page == UI_PAGE_AP_TRANSFER || is_ap_transfer_mode_running()) {
                    ESP_LOGI(TAG, "BOOT long press - exiting AP transfer mode");
                    ap_transfer_server_stop(&s_transfer_server);
                    ui_manager_switch_page(s_app.ui_mgr, UI_PAGE_GALLERY);
                } else if (boot_page == UI_PAGE_GALLERY) {
                    if (app_is_lan_http_running()) {
                        ESP_LOGI(TAG, "Gallery long press BOOT - stopping LAN HTTP server");
                        ap_transfer_server_stop(&s_transfer_server);
                    } else if (s_app.wifi_connected) {
                        ESP_LOGI(TAG, "Gallery long press BOOT - starting LAN HTTP server");
                        char lan_ip[32] = {0};
                        wifi_manager_get_ip(lan_ip, sizeof(lan_ip));
                        ap_transfer_server_start_lan(&s_transfer_server, lan_ip);
                    } else {
                        ESP_LOGW(TAG, "Gallery long press BOOT - WiFi not connected, cannot start LAN server");
                    }
                    ui_manager_trigger_refresh(s_app.ui_mgr, false);
                } else if (boot_page == UI_PAGE_WIFI) {
                    ESP_LOGI(TAG, "WiFi page long press BOOT - entering AP transfer mode");
                    ap_transfer_page_use_default_instructions(
                        (page_renderer_t *)page_registry_get_instance(UI_PAGE_AP_TRANSFER));
                    ap_transfer_server_start(&s_transfer_server);
                    ui_manager_switch_page(s_app.ui_mgr, UI_PAGE_AP_TRANSFER);
                } else {
                    ui_button_event_t button_ev = {BTN_BOOT_LONG_PRESS};
                    ui_manager_handle_input(s_app.ui_mgr, &button_ev);
                }
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
                    time_t now_t = time(NULL);
                    struct tm tm_buf;
                    localtime_r(&now_t, &tm_buf);
                    cal->today_year = tm_buf.tm_year + 1900;
                    cal->today_month = tm_buf.tm_mon + 1;
                    cal->today_day = tm_buf.tm_mday;
                    cal->cal.today_year = cal->today_year;
                    cal->cal.today_month = cal->today_month;
                    cal->cal.today_day = cal->today_day;
                    cal->year = cal->today_year;
                    cal->month = cal->today_month;
                    widget_calendar_set_date(&cal->cal, cal->year, cal->month);
                    cal->base.needs_full_refresh_flag = true;
                }
                /* Status-bar date/central text may have changed with SNTP
                 * calibration. Only request an EPD refresh when the calendar
                 * page is visible; elsewhere the updated status bar appears
                 * on the next natural refresh/page switch — avoids a ~15 s
                 * full refresh of the current page just for the date. */
                if (ui_manager_get_current_page(s_app.ui_mgr) == UI_PAGE_CALENDAR) {
                    application_update_status_bar();
                }
                break;
            }
            case APP_EVENT_UI_IMAGE_RECEIVED: {
                ESP_LOGI(TAG, "Processing UI_IMAGE_RECEIVED: %s", ev.image_received.photo_id);
                photo_gallery_refresh_photo_list((page_renderer_t *)page_registry_get_instance(UI_PAGE_GALLERY));
                int count =
                    photo_gallery_get_photo_count((page_renderer_t *)page_registry_get_instance(UI_PAGE_GALLERY));
                if (count > 0 && ev.image_received.photo_id[0] != '\0') {
                    photo_gallery_set_selected_by_id((page_renderer_t *)page_registry_get_instance(UI_PAGE_GALLERY),
                                                     ev.image_received.photo_id);
                }
                /* Photo list data is updated regardless; only flash the EPD
                 * when the gallery is on screen (photo belongs to gallery). */
                if (ui_manager_get_current_page(s_app.ui_mgr) == UI_PAGE_GALLERY) {
                    ui_manager_request_active_page_refresh(s_app.ui_mgr);
                }
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

        if ((now - last_periodic) >= pdMS_TO_TICKS(1000)) {
            last_periodic = now;
            int pct = charge_status_get_battery_percent();
            if (pct > 0 && pct <= 3 && !charge_status_is_charging()) {
                ESP_LOGW(TAG, "Battery low (%d%%) and not charging. Shutting down...", pct);
                render_low_battery_warning();
                board_power_vbat_off();
                esp_deep_sleep_start();
            }
            /* Battery icon updates independently of wifi events: update the
             * status bar only when the % actually changes. */
            if (pct != s_last_battery_pct) {
                s_last_battery_pct = pct;
                application_update_status_bar();
            }
            ui_manager_pump_clock_refresh(s_app.ui_mgr);

            if (s_app.wifi_connected) {
                if (s_app.need_weather_fetch) {
                    s_app.need_weather_fetch = false;
                    weather_api_fetch();
                }
                if (s_app.need_coding_plan_refresh) {
                    s_app.need_coding_plan_refresh = false;
                    app_sync_refresh_coding_plan();
                }
                if (s_app.need_holiday_fetch) {
                    s_app.need_holiday_fetch = false;
                    time_t t = time(NULL);
                    struct tm tmr;
                    localtime_r(&t, &tmr);
                    int year = tmr.tm_year + 1900;
                    if (holiday_fetcher_fetch(year)) {
                        /* Holiday data feeds the calendar page; only refresh
                         * the EPD when that page is visible (full refresh is
                         * ~15 s — don't flash the current page otherwise). */
                        if (ui_manager_get_current_page(s_app.ui_mgr) == UI_PAGE_CALENDAR) {
                            ui_manager_request_active_page_refresh(s_app.ui_mgr);
                        }
                    }
                }

                if (++s_cp_refresh_counter >= APP_CODING_PLAN_REFRESH_SECONDS) {
                    s_cp_refresh_counter = 0;
                    app_sync_refresh_coding_plan();
                }
            } else {
                s_cp_refresh_counter = 0;
                s_app.need_weather_fetch = false;
                s_app.need_coding_plan_refresh = false;
                s_app.need_holiday_fetch = false;
            }

            if (sm_can_sleep_now() && !app_sleep_is_local_http_running()) {
                ESP_LOGI(TAG, "System idle. Sleep manager allows sleep. Entering sleep...");
                app_sleep_enter_scheduled();
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Public accessors                                                    */
/* ------------------------------------------------------------------ */

device_state_t application_get_device_state(void)
{
    return s_app.state;
}

bool application_set_device_state(device_state_t state)
{
    const device_state_t old = s_app.state;
    s_app.state = state;
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
    data.wifi_connected = s_app.wifi_connected;
    data.server_connected = ap_transfer_server_is_running(&s_transfer_server);
    data.battery_level = battery_level;
    ui_manager_update_status_bar(s_app.ui_mgr, &data);

    app_settings_update_wifi_item(s_app.wifi_connected, NULL);
    char lan_ip[32] = {0};
    if (s_app.wifi_connected) {
        wifi_manager_get_ip(lan_ip, sizeof(lan_ip));
    }
    app_settings_update_lan_ip_item(lan_ip);
    app_settings_update_http_server_item(app_is_lan_http_running(), lan_ip);

    ui_manager_request_active_page_refresh(s_app.ui_mgr);
}

/* ------------------------------------------------------------------ */
/* Button routing                                                      */
/* ------------------------------------------------------------------ */

static void post_button_event(app_event_type_t type)
{
    app_event_t ev = {.type = type};
    if (s_app.event_queue) {
        xQueueSend(s_app.event_queue, &ev, 0);
    }
}

void application_on_up_click(void)
{
    post_button_event(APP_EVENT_UP_CLICK);
}
void application_on_down_click(void)
{
    post_button_event(APP_EVENT_DOWN_CLICK);
}
void application_on_up_double_click(void)
{
    post_button_event(APP_EVENT_UP_DOUBLE_CLICK);
}
void application_on_boot_double_click(void)
{
    post_button_event(APP_EVENT_BOOT_DOUBLE_CLICK);
}
void application_on_down_double_click(void)
{
    post_button_event(APP_EVENT_DOWN_DOUBLE_CLICK);
}
void application_on_up_long_press(void)
{
    post_button_event(APP_EVENT_UP_LONG_PRESS);
}
void application_on_down_long_press(void)
{
    post_button_event(APP_EVENT_DOWN_LONG_PRESS);
}
void application_on_wifi_config_combo_long_press(void)
{
    post_button_event(APP_EVENT_WIFI_CONFIG_COMBO_LONG_PRESS);
}
void application_on_boot_click(void)
{
    post_button_event(APP_EVENT_BOOT_CLICK);
}
void application_on_boot_long_press(void)
{
    post_button_event(APP_EVENT_BOOT_LONG_PRESS);
}

/* ------------------------------------------------------------------ */
/* Sleep control (thin wrapper — implementation in app_sleep.c)        */
/* ------------------------------------------------------------------ */

void application_enter_manual_sleep(void)
{
    app_sleep_enter_manual();
}
