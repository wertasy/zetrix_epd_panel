/**
 * @file app_sync.c
 * @brief Data sync orchestration — coding plan, weather, SNTP, and data
 *        refresh request routing.
 *
 * Extracted from application.c (Phase 2.1 module split). All functions
 * access the shared s_app singleton via application_internal.h.
 */
#include "application_internal.h"

#include <string.h>
#include <stdlib.h>
#include <time.h>

#include <esp_log.h>

#include "config.h"
#include "coding_plan_page.h"
#include "weather_page.h"
#include "fridge_memo_api.h"
#include "fridge_memo_page.h"
#include "page_registry.h"
#include "page_runtime.h"
#include "rtc_pcf8563.h"

#define TAG "Application"

/* Counter shared with application_run (ticks once per second). */
int s_cp_refresh_counter = 0;

/* ------------------------------------------------------------------ */
/* Coding Plan usage refresh                                           */
/* ------------------------------------------------------------------ */

void app_sync_ensure_coding_plan_initialised(void)
{
    static bool s_cp_inited = false;
    if (s_cp_inited)
        return;
    coding_plan_api_init(CONFIG_CODING_PLAN_API_TOKEN, CONFIG_CODING_PLAN_API_ORG, CONFIG_CODING_PLAN_API_PROJECT);
    coding_plan_api_set_callback(app_sync_on_coding_plan_update, NULL);
    s_cp_inited = true;

    /* If NVS cache exists, immediately render it so the page shows data
     * before the network fetch completes. */
    if (coding_plan_api_has_cached_data()) {
        app_sync_on_coding_plan_update(coding_plan_api_get_cached_data(), NULL);
    }
}

void app_sync_on_coding_plan_update(const coding_plan_api_data_t *data, void *user_data)
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
    snprintf(page_data->five_hour_reset_time, sizeof(page_data->five_hour_reset_time), "%s",
             data->five_hour_reset_time);
    snprintf(page_data->week_reset_time, sizeof(page_data->week_reset_time), "%s", data->week_reset_time);
    page_data->five_hour_tokens = data->five_hour_tokens;
    page_data->week_tokens = data->week_tokens;
    page_data->five_hour_pct = data->five_hour_pct;
    page_data->week_pct = data->week_pct;
    page_data->per_model_count = data->per_model_count;
    for (int i = 0; i < data->per_model_count && i < CODING_PLAN_MAX_MODELS; i++) {
        page_data->per_model[i] = data->per_model[i];
    }
    page_data->hourly_count = data->hourly_count;
    memcpy(page_data->hourly_tokens, data->hourly_tokens, sizeof(page_data->hourly_tokens));

    coding_plan_page_update(page, page_data);
    free(page_data);
    /* Only refresh the EPD when the coding-plan page is actually visible.
     * A full refresh on this 4-color panel takes ~15 s; refreshing it for
     * background data while another page (e.g. gallery) is on screen would
     * flash the current page twice in a row for no visible benefit. The page
     * data is updated regardless, so switching to it shows fresh numbers. */
    if (ui_manager_get_current_page(s_app.ui_mgr) == UI_PAGE_CODING_PLAN) {
        ui_manager_request_active_page_refresh(s_app.ui_mgr);
    }
}

void app_sync_refresh_coding_plan(void)
{
    app_sync_ensure_coding_plan_initialised();
    coding_plan_api_fetch_async();
}

/* ------------------------------------------------------------------ */
/* Weather callback                                                    */
/* ------------------------------------------------------------------ */

void app_sync_on_weather_update(const weather_data_t *data, void *user_data)
{
    (void)user_data;
    if (data) {
        ESP_LOGI(TAG, "Weather callback fired, updating weather page");
        page_renderer_t *weather_page = (page_renderer_t *)ui_manager_get_renderer(s_app.ui_mgr, UI_PAGE_WEATHER);
        if (weather_page) {
            weather_page_update(weather_page, data);
            weather_page_set_city_name(weather_page, weather_api_get_city_name());
            /* Same page-awareness as the coding-plan callback: only trigger
             * the ~15 s EPD refresh when a weather page is on screen. */
            if (ui_manager_get_current_page(s_app.ui_mgr) == UI_PAGE_WEATHER ||
                ui_manager_get_current_page(s_app.ui_mgr) == UI_PAGE_WEATHER_DETAIL) {
                ui_manager_request_active_page_refresh(s_app.ui_mgr);
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Data refresh request routing                                        */
/* ------------------------------------------------------------------ */

/* Called when a page renderer requests a data refresh (Phase 1.4 decoupling). */
void app_sync_on_data_refresh_request(int page, void *ctx)
{
    (void)ctx;
    switch ((ui_page_id_t)page) {
    case UI_PAGE_WEATHER:
        page_runtime_set_pending(PAGE_DATA_WEATHER);
        break;
    case UI_PAGE_CODING_PLAN:
        page_runtime_set_pending(PAGE_DATA_CODING_PLAN);
        break;
    case UI_PAGE_FRIDGE_MEMO:
        app_sync_refresh_fridge_memo();
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* SNTP callback (invoked by app_sntp_start_once)                      */
/* ------------------------------------------------------------------ */

void app_sync_on_sntp_sync(struct timeval *tv)
{
    (void)tv;
    ESP_LOGI(TAG, "SNTP time sync complete; writing RTC and refreshing calendar");

    /* Write the corrected time back to the PCF8563 RTC so cold boots start
     * with the right date without needing WiFi. */
    time_t now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    pcf8563_set_time(&tm_buf);

    /* Notify the main loop so it can refresh date-dependent pages. */
    app_event_t ev = {.type = APP_EVENT_TIME_SYNC};
    if (s_app.event_queue) {
        xQueueSend(s_app.event_queue, &ev, 0);
    }
}

/* ------------------------------------------------------------------ */
/* Fridge memo                                                         */
/* ------------------------------------------------------------------ */

static char s_fridge_pending_delete_name[FRIDGE_MEMO_NAME_LEN];

static void app_sync_on_fridge_memo_update(const fridge_memo_snapshot_t *snap, void *user_data)
{
    (void)user_data;
    if (!snap)
        return;

    page_renderer_t *page = page_registry_get_instance(UI_PAGE_FRIDGE_MEMO);
    if (!page)
        return;

    fridge_memo_page_update(page, snap);
    page_renderer_mark_full_refresh(page); /* full refresh on this page's next render */
    fridge_memo_page_set_offline(page, false);
    if (s_fridge_pending_delete_name[0]) {
        char msg[FRIDGE_MEMO_FOOTER_TEXT_LEN + FRIDGE_MEMO_NAME_LEN];
        snprintf(msg, sizeof(msg), "已删除：%s", s_fridge_pending_delete_name);
        fridge_memo_page_set_footer_message(page, msg);
        s_fridge_pending_delete_name[0] = '\0';
    }
    /* Data lands regardless of visibility; the EPD only flashes when the
     * fridge page is actually on screen (coding_plan pattern). */
    if (ui_manager_get_current_page(s_app.ui_mgr) == UI_PAGE_FRIDGE_MEMO)
        ui_manager_request_active_page_refresh(s_app.ui_mgr);
}

static void app_sync_on_fridge_memo_error(const char *message, void *user_data)
{
    (void)user_data;
    page_renderer_t *page = page_registry_get_instance(UI_PAGE_FRIDGE_MEMO);
    if (!page)
        return;

    fridge_memo_page_set_offline(page, true); /* error path: offline banner */
    fridge_memo_page_set_footer_message(page, message);
    if (ui_manager_get_current_page(s_app.ui_mgr) == UI_PAGE_FRIDGE_MEMO)
        ui_manager_request_active_page_refresh(s_app.ui_mgr);
}

static void app_sync_on_fridge_memo_delete(const char *item_id, void *user_data)
{
    (void)user_data;
    /* Resolve the display name first (local, pre-acceptance) so the result
     * footer can name the deleted item; the pending slot is only claimed
     * once the request is actually accepted by the API. */
    const fridge_memo_snapshot_t *snap = fridge_memo_api_get_cached_data();
    char name[FRIDGE_MEMO_NAME_LEN] = {0};
    if (snap) {
        for (int i = 0; i < snap->count; ++i) {
            if (strcmp(snap->items[i].id, item_id) == 0) {
                snprintf(name, sizeof(name), "%s", snap->items[i].name);
                break;
            }
        }
    }
    if (!fridge_memo_api_delete_async(item_id)) {
        page_renderer_t *page = page_registry_get_instance(UI_PAGE_FRIDGE_MEMO);
        if (!page)
            return;
        if (fridge_memo_api_is_busy()) {
            /* A fetch/delete is already in flight — transient, not an outage. */
            fridge_memo_page_set_footer_message(page, "正在处理上一条，请稍候");
        } else {
            fridge_memo_page_set_offline(page, true);
            fridge_memo_page_set_footer_message(page, "删除失败：后端不可达");
        }
        if (ui_manager_get_current_page(s_app.ui_mgr) == UI_PAGE_FRIDGE_MEMO)
            ui_manager_request_active_page_refresh(s_app.ui_mgr);
        return;
    }
    snprintf(s_fridge_pending_delete_name, sizeof(s_fridge_pending_delete_name), "%s", name);
}

void app_sync_ensure_fridge_memo_initialised(void)
{
    static bool s_fm_inited = false;
    if (s_fm_inited)
        return;
    fridge_memo_api_init(NULL); /* base_url from NVS "fridge" */
    fridge_memo_api_set_callback(app_sync_on_fridge_memo_update, NULL);
    fridge_memo_api_set_error_callback(app_sync_on_fridge_memo_error, NULL);
    fridge_memo_page_set_delete_request_handler(page_registry_get_instance(UI_PAGE_FRIDGE_MEMO),
                                                app_sync_on_fridge_memo_delete, NULL);
    s_fm_inited = true;

    /* Cache-first: render NVS snapshot immediately (design §4.4). */
    if (fridge_memo_api_has_cached_data()) {
        app_sync_on_fridge_memo_update(fridge_memo_api_get_cached_data(), NULL);
        s_fridge_pending_delete_name[0] = '\0';
    }
}

void app_sync_refresh_fridge_memo(void)
{
    app_sync_ensure_fridge_memo_initialised();
    fridge_memo_api_fetch_async();
}
