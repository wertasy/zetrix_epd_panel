/**
 * @file ui_manager.c
 * @brief RawDraw UI manager — C port of C++ ui::RawDrawUiManager.
 *
 * Owns all 19 page renderers (statically pre-allocated), routes button
 * events, draws the status bar and global page frame, and provides page
 * data update APIs. Framebuffer access is injected via refresh callback
 * so this component does not depend on main/.
 */
#include "ui_manager.h"
#include <assert.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include "rawdraw_ext.h"
#include "theme.h"
#include "style.h"
#include "layout.h"
#include "clock.h"
#include "ui_text_util.h"
#include "voice_wakeup.h"
#include "fa_settings.h"
#include "nvs_state.h"
#include "epd_driver.h"
#include "page_registry.h"

#include "pages/chat_page.h"
#ifdef CONFIG_PAGE_EBOOK_ENABLE
#    include "pages/ebook_page.h"
#endif
#include "pages/wifi_page.h"
#include "pages/settings_page.h"
#include "pages/photo_gallery_page.h"
#ifdef CONFIG_PAGE_LIFEBAR_ENABLE
#    include "pages/lifebar_page.h"
#endif
#include "pages/calendar_page.h"
#include "pages/ap_transfer_page.h"

#include <esp_log.h>
#include <esp_timer.h>

#define TAG "RawDrawUiManager"
#define RAWDRAW_THEME_NVS_KEY "rawdraw_theme"

/* RTC memory survives deep sleep — used to restore the last-viewed page
 * on wake without Flash writes. Magic validates against stale/cold-boot. */
#define RTC_PAGE_MAGIC 0xDEAD5AA5U
static RTC_NOINIT_ATTR uint32_t s_rtc_last_page;
static RTC_NOINIT_ATTR uint32_t s_rtc_page_magic;

/* Four-color e-paper cannot reliably partial-refresh the status bar; keep the
 * minute-clock timer code but leave it disabled (see C++ comment). */
#define ENABLE_MINUTE_CLOCK_REFRESH 0

/* ------------------------------------------------------------------ */
/* Page titles                                                         */
/* ------------------------------------------------------------------ */

const char *ui_manager_get_page_title(ui_page_id_t page)
{
    return page_registry_get_name(page);
}

/* ------------------------------------------------------------------ */
/* UI manager state (struct definition in ui_manager_internal.h)       */
/* ------------------------------------------------------------------ */
#include "ui_manager_internal.h"

/* ------------------------------------------------------------------ */
/* Quick switch items                                                  */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* Static helpers                                                      */
/* ------------------------------------------------------------------ */

static int current_local_minute_key(void)
{
    time_t now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    return tm_buf.tm_year * 366 * 24 * 60 + tm_buf.tm_yday * 24 * 60 + tm_buf.tm_hour * 60 + tm_buf.tm_min;
}

/* ------------------------------------------------------------------ */
/* Renderer registry                                                   */
/* ------------------------------------------------------------------ */

static page_renderer_t *get_renderer_for_page(ui_manager_t *mgr, ui_page_id_t page)
{
    (void)mgr;
    return page_registry_get_instance(page);
}

static void init_renderer(ui_manager_t *mgr, ui_page_id_t page)
{
    page_renderer_t *r = page_registry_get_instance(page);
    if (r) {
        page_renderer_init(r, mgr->width, mgr->height);
        page_renderer_mark_full_refresh(r);
    }
}

/* ------------------------------------------------------------------ */
/* Status bar                                                          */
/* ------------------------------------------------------------------ */

static void draw_global_page_frame(uint8_t *fb, int width, int height)
{
    if (!fb || width <= 4 || height <= 4)
        return;
    const rawdraw_paint_style_t border = rawdraw_theme_style(THEME_TOKEN_BORDER);
    rawdraw_draw_round_rect_border(fb, width, height, (rawdraw_rect_t){1, 1, width - 2, height - 2},
                                   STYLE_BORDER_RADIUS_MD, border.border_width, border.border);
}

/* Quick switch overlay rendering is in ui_quick_switch.c */

static bool handle_quick_switch_input(ui_manager_t *mgr, const ui_button_event_t *event)
{
    if (!mgr->quick_switch_open)
        return false;
    const int total = mgr->quick_count;
    if (total <= 0) {
        mgr->quick_switch_open = false;
        return false;
    }
    switch (event->type) {
    case BTN_UP_CLICK:
        mgr->quick_switch_index = (mgr->quick_switch_index + total - 1) % total;
        if (mgr->quick_switch_index < mgr->quick_switch_first_visible) {
            mgr->quick_switch_first_visible = mgr->quick_switch_index;
        }
        if (mgr->quick_switch_index == total - 1) {
            mgr->quick_switch_first_visible =
                (0 > total - QUICK_SWITCH_VISIBLE_COUNT) ? 0 : (total - QUICK_SWITCH_VISIBLE_COUNT);
        }
        return true;
    case BTN_DOWN_CLICK:
        mgr->quick_switch_index = (mgr->quick_switch_index + 1) % total;
        if (mgr->quick_switch_index >= mgr->quick_switch_first_visible + QUICK_SWITCH_VISIBLE_COUNT) {
            mgr->quick_switch_first_visible = mgr->quick_switch_index - QUICK_SWITCH_VISIBLE_COUNT + 1;
        }
        if (mgr->quick_switch_index == 0) {
            mgr->quick_switch_first_visible = 0;
        }
        return true;
    case BTN_BOOT_CLICK:
        /* Switch without an internal render: ui_manager_switch_page() fires
         * refresh_cb synchronously, and the caller (ui_manager_handle_input)
         * ALSO refreshes after this returns — that would push the same frame
         * to the EPD twice in a row. Close the overlay and switch page with
         * no render; the caller's single refresh_cb renders the new page. */
        mgr->quick_switch_open = false;
        ui_manager_set_current_page_without_render(mgr, mgr->quick_items[mgr->quick_switch_index]->id);
        return true;
    case BTN_UP_LONG_PRESS:
    case BTN_DOWN_LONG_PRESS:
        mgr->quick_switch_open = false;
        return true;
    default:
        return true;
    }
}

/* ------------------------------------------------------------------ */
/* Refresh                                                             */
/* ------------------------------------------------------------------ */

void ui_manager_trigger_refresh(ui_manager_t *mgr, bool urgent)
{
    if (!mgr)
        return;
    if (mgr->refresh_cb) {
        mgr->refresh_cb((rawdraw_rect_t){0, 0, mgr->width, mgr->height}, urgent, mgr->refresh_ctx);
    }
}

void ui_manager_request_full_refresh(ui_manager_t *mgr)
{
    /* no-op (full_refresh_pending field is removed) */
}

void ui_manager_request_active_page_refresh(ui_manager_t *mgr)
{
    if (mgr)
        mgr->active_page_refresh_pending = true;
}

void ui_manager_set_data_refresh_cb(ui_manager_t *mgr, void (*cb)(ui_page_id_t page, void *ctx), void *ctx)
{
    if (mgr) {
        mgr->data_refresh_cb = cb;
        mgr->data_refresh_ctx = ctx;
    }
}

void ui_manager_request_data_refresh(ui_manager_t *mgr, ui_page_id_t page)
{
    if (mgr && mgr->data_refresh_cb)
        mgr->data_refresh_cb(page, mgr->data_refresh_ctx);
}

/* ------------------------------------------------------------------ */
/* Init / deinit                                                       */
/* ------------------------------------------------------------------ */

ui_manager_t *ui_manager_create(void)
{
    ui_manager_t *mgr = (ui_manager_t *)calloc(1, sizeof(ui_manager_t));
    return mgr;
}

void ui_manager_delete(ui_manager_t *mgr)
{
    free(mgr);
}

void ui_manager_set_refresh_callback(ui_manager_t *mgr, ui_manager_refresh_cb_t cb, void *user_data)
{
    if (!mgr)
        return;
    mgr->refresh_cb = cb;
    mgr->refresh_ctx = user_data;
}

static void on_display_refresh_idle_cb(void *user_data)
{
    (void)user_data;
    ESP_LOGI(TAG, "Display refresh idle");
}

void ui_manager_init(ui_manager_t *mgr, ui_manager_refresh_cb_t refresh_cb, void *user_data)
{
    if (!mgr)
        return;
    memset(mgr, 0, sizeof(*mgr));
    page_registry_init();
    mgr->quick_count = page_registry_quick_switch_items(mgr->quick_items, UI_PAGE_COUNT);
    mgr->width = STYLE_SCREEN_WIDTH;
    mgr->height = STYLE_SCREEN_HEIGHT;
    mgr->current_page = UI_PAGE_GALLERY;
    /* P0: Restore last-viewed page from RTC memory on deep-sleep wake. */
    if (s_rtc_page_magic == RTC_PAGE_MAGIC && s_rtc_last_page < (uint32_t)UI_PAGE_COUNT) {
        mgr->current_page = (ui_page_id_t)s_rtc_last_page;
    }
    mgr->refresh_cb = refresh_cb;
    mgr->refresh_ctx = user_data;
    mgr->quick_switch_open = false;
    mgr->quick_switch_index = 0;
    mgr->last_clock_minute_key = current_local_minute_key();
    mgr->gallery_slideshow_interval_minutes = 0;

    /* Theme from NVS. */
    char theme_key[32] = "industrial";
    nvs_state_get_string(RAWDRAW_THEME_NVS_KEY, theme_key, sizeof(theme_key));
    rawdraw_theme_set_by_key(theme_key);

    /* Page renderers: run one-time init for every registered page at boot. */
    for (ui_page_id_t pid = 0; pid < UI_PAGE_COUNT; ++pid) {
        init_renderer(mgr, pid);
    }
    /* Enter hook for the initial active page. */
    page_renderer_enter(page_registry_get_instance(mgr->current_page));
    widget_voice_wakeup_init(&mgr->voice_wakeup, &SourceHanSansSC_Regular_slim);
    epd_clock_init(&mgr->clock, CLOCK_DEFAULT_X, CLOCK_DEFAULT_Y, &font_zectrix_16_1);
    epd_clock_set_color(&mgr->clock, rawdraw_theme_style(THEME_TOKEN_ACCENT).fg);

    /* Status bar defaults. */
    strncpy(mgr->status_bar.page_title, ui_manager_get_page_title(mgr->current_page),
            sizeof(mgr->status_bar.page_title) - 1);
    mgr->status_bar.wifi_connected = false;
    mgr->status_bar.server_connected = false;
    mgr->status_bar.battery_level = -1;
    mgr->status_bar.battery_charging = false;
    mgr->status_bar.battery_vertical = false;
    mgr->status_bar.date_format[0] = '\0';

    set_on_refresh_idle(on_display_refresh_idle_cb, mgr);

    ESP_LOGI(TAG, "RawDraw UI Manager initialized: %dx%d, page=%s", mgr->width, mgr->height,
             ui_manager_get_page_title(mgr->current_page));
}

/* ------------------------------------------------------------------ */
/* Page switching                                                      */
/* ------------------------------------------------------------------ */

void ui_manager_switch_page(ui_manager_t *mgr, ui_page_id_t page)
{
    if (!mgr)
        return;
    if (page == mgr->current_page)
        return;

    ESP_LOGI(TAG, "Switching page: %s -> %s", ui_manager_get_page_title(mgr->current_page),
             ui_manager_get_page_title(page));

    /* Lifecycle: exit current page, then enter the new page. */
    page_renderer_exit(page_registry_get_instance(mgr->current_page));
    page_renderer_enter(page_registry_get_instance(page));
    mgr->current_page = page;
    s_rtc_last_page = (uint32_t)page;
    s_rtc_page_magic = RTC_PAGE_MAGIC;
    if (mgr->page_switch_cb) {
        mgr->page_switch_cb(page, mgr->page_switch_ctx);
    }
    strncpy(mgr->status_bar.page_title, ui_manager_get_page_title(page), sizeof(mgr->status_bar.page_title) - 1);
    mgr->status_bar.page_title[sizeof(mgr->status_bar.page_title) - 1] = '\0';

    if (mgr->refresh_cb) {
        mgr->refresh_cb((rawdraw_rect_t){0, 0, mgr->width, mgr->height}, true, mgr->refresh_ctx);
    }
}

void ui_manager_set_current_page_without_render(ui_manager_t *mgr, ui_page_id_t page)
{
    if (!mgr || page == mgr->current_page)
        return;
    page_renderer_exit(page_registry_get_instance(mgr->current_page));
    page_renderer_enter(page_registry_get_instance(page));
    mgr->current_page = page;
    s_rtc_last_page = (uint32_t)page;
    s_rtc_page_magic = RTC_PAGE_MAGIC;
    if (mgr->page_switch_cb) {
        mgr->page_switch_cb(page, mgr->page_switch_ctx);
    }
    strncpy(mgr->status_bar.page_title, ui_manager_get_page_title(page), sizeof(mgr->status_bar.page_title) - 1);
}

ui_page_id_t ui_manager_get_current_page(const ui_manager_t *mgr)
{
    assert(mgr != NULL);
    return mgr ? mgr->current_page : UI_PAGE_GALLERY;
}

ui_page_id_t ui_manager_get_rtc_saved_page(void)
{
    if (s_rtc_page_magic == RTC_PAGE_MAGIC && s_rtc_last_page < (uint32_t)UI_PAGE_COUNT) {
        return (ui_page_id_t)s_rtc_last_page;
    }
    return UI_PAGE_GALLERY; /* default */
}

page_renderer_t *ui_manager_get_active_renderer(const ui_manager_t *mgr)
{
    return mgr ? get_renderer_for_page((ui_manager_t *)mgr, mgr->current_page) : NULL;
}

page_renderer_t *ui_manager_get_renderer(const ui_manager_t *mgr, ui_page_id_t page)
{
    return mgr ? get_renderer_for_page((ui_manager_t *)mgr, page) : NULL;
}

bool ui_manager_is_display_refresh_pending(const ui_manager_t *mgr)
{
    (void)mgr;
    return false;
}

/* ------------------------------------------------------------------ */
/* Rendering                                                           */
/* ------------------------------------------------------------------ */

void ui_manager_render_all(ui_manager_t *mgr, uint8_t *fb, int width, int height)
{
    if (!mgr || !fb)
        return;

    const bool gallery_fullscreen =
        mgr->current_page == UI_PAGE_GALLERY &&
        photo_gallery_is_fullscreen_mode((page_renderer_t *)page_registry_get_instance(UI_PAGE_GALLERY));
    const bool ebook_portrait_reader =
#ifdef CONFIG_PAGE_EBOOK_ENABLE
        mgr->current_page == UI_PAGE_EBOOK &&
        ebook_page_is_portrait_reader((page_renderer_t *)page_registry_get_instance(UI_PAGE_EBOOK));
#else
        false;
#endif

    /* Update central_text based on current page state. */
    mgr->status_bar.central_text[0] = '\0';
#ifdef CONFIG_PAGE_EBOOK_ENABLE
    if (mgr->current_page == UI_PAGE_EBOOK && !ebook_portrait_reader) {
        if (ebook_page_is_reader_mode((page_renderer_t *)page_registry_get_instance(UI_PAGE_EBOOK))) {
            snprintf(mgr->status_bar.central_text, sizeof(mgr->status_bar.central_text), "%s  %d/%d",
                     ebook_page_get_reader_filename((page_renderer_t *)page_registry_get_instance(UI_PAGE_EBOOK)),
                     ebook_page_get_current_page((page_renderer_t *)page_registry_get_instance(UI_PAGE_EBOOK)) + 1,
                     ebook_page_get_total_pages((page_renderer_t *)page_registry_get_instance(UI_PAGE_EBOOK)));
        }
    }
#endif
    if (mgr->current_page == UI_PAGE_CALENDAR) {
        snprintf(mgr->status_bar.central_text, sizeof(mgr->status_bar.central_text), "%d年%d月 ← →",
                 calendar_page_get_year((page_renderer_t *)page_registry_get_instance(UI_PAGE_CALENDAR)),
                 calendar_page_get_month((page_renderer_t *)page_registry_get_instance(UI_PAGE_CALENDAR)));
    }

    /* Draw the active page content. */
    page_renderer_t *renderer = ui_manager_get_active_renderer(mgr);
    if (renderer) {
        page_renderer_render(renderer, fb, width, height);
    }

    /* Fullscreen gallery / portrait reader are chrome-free. */
    if (!gallery_fullscreen && !ebook_portrait_reader) {
        ui_status_bar_draw(mgr, fb, width, height);
        draw_global_page_frame(fb, width, height);
    }

    /* Voice wakeup overlay. */
    if (widget_voice_wakeup_is_visible(&mgr->voice_wakeup)) {
        widget_voice_wakeup_render(&mgr->voice_wakeup, fb, width, height);
    }

    if (mgr->quick_switch_open) {
        ui_quick_switch_draw_overlay(mgr, fb, width, height);
    }

    /* Render is complete — consume the pending refresh flags so the periodic
     * pump does not schedule a second, identical EPD update. Without this,
     * any synchronous render (e.g. boot/render_ui_and_refresh) leaves
     * active_page_refresh_pending set, and the next pump tick refreshes the
     * panel again — visible as a double flash, especially on RTC wake. */
    mgr->active_page_refresh_pending = false;
    mgr->transient_refresh_pending = false;
}

/* ------------------------------------------------------------------ */
/* Input routing                                                       */
/* ------------------------------------------------------------------ */
/*
 * Find the adjacent page in the quick-switch list (wrapping).
 * dir = -1 for previous, +1 for next. Returns current_page if
 * the current page is not in the list or no other page exists.
 */
static ui_page_id_t find_adjacent_page(ui_manager_t *mgr, int dir)
{
    if (!mgr || mgr->quick_count == 0)
        return mgr ? mgr->current_page : UI_PAGE_GALLERY;
    int found = -1;
    for (int i = 0; i < mgr->quick_count; i++) {
        if (mgr->quick_items[i]->id == mgr->current_page) {
            found = i;
            break;
        }
    }
    if (found < 0)
        return mgr->current_page;
    int next = (found + dir + mgr->quick_count) % mgr->quick_count;
    return mgr->quick_items[next]->id;
}

bool ui_manager_handle_input(ui_manager_t *mgr, const ui_button_event_t *event)
{
    if (!mgr || !event)
        return false;

    /* Input is never blocked — events are already queued by the event
     * loop and processed serially. The EPD refresh runs asynchronously
     * in the display task; multiple rapid inputs just update the
     * framebuffer and the latest frame is sent on the next refresh. */

    if (event->type == BTN_BOOT_DOUBLE_CLICK) {
        mgr->quick_switch_open = !mgr->quick_switch_open;
        if (mgr->refresh_cb) {
            mgr->refresh_cb((rawdraw_rect_t){0, 0, mgr->width, mgr->height}, false, mgr->refresh_ctx);
        }
        return true;
    }

    if (event->type == BTN_UP_DOUBLE_CLICK) {
        if (mgr->quick_switch_open) {
            mgr->quick_switch_open = false;
            if (mgr->refresh_cb) {
                mgr->refresh_cb((rawdraw_rect_t){0, 0, mgr->width, mgr->height}, false, mgr->refresh_ctx);
            }
            return true;
        }
        ui_page_id_t prev = find_adjacent_page(mgr, -1);
        if (prev != mgr->current_page) {
            ui_manager_switch_page(mgr, prev);
        }
        return true;
    }

    if (event->type == BTN_DOWN_DOUBLE_CLICK) {
        if (mgr->quick_switch_open) {
            mgr->quick_switch_open = false;
            if (mgr->refresh_cb) {
                mgr->refresh_cb((rawdraw_rect_t){0, 0, mgr->width, mgr->height}, false, mgr->refresh_ctx);
            }
            return true;
        }
        ui_page_id_t next = find_adjacent_page(mgr, +1);
        if (next != mgr->current_page) {
            ui_manager_switch_page(mgr, next);
        }
        return true;
    }

    if (mgr->quick_switch_open && handle_quick_switch_input(mgr, event)) {
        if (mgr->refresh_cb) {
            mgr->refresh_cb((rawdraw_rect_t){0, 0, mgr->width, mgr->height}, false, mgr->refresh_ctx);
        }
        return true;
    }

    page_renderer_t *renderer = ui_manager_get_active_renderer(mgr);
    if (!renderer)
        return false;

    bool handled = page_renderer_handle_input(renderer, event);
    if (handled) {
        if (mgr->refresh_cb) {
            mgr->refresh_cb((rawdraw_rect_t){0, 0, mgr->width, mgr->height}, false, mgr->refresh_ctx);
        }
    }
    return handled;
}

bool ui_manager_is_quick_switch_open(const ui_manager_t *mgr)
{
    return mgr && mgr->quick_switch_open;
}

/* ------------------------------------------------------------------ */
/* Status bar data                                                     */
/* ------------------------------------------------------------------ */

void ui_manager_update_status_bar(ui_manager_t *mgr, const ui_manager_status_bar_t *data)
{
    if (!mgr || !data)
        return;
    mgr->status_bar = *data;
}

void ui_manager_get_status_bar_data(ui_manager_t *mgr, ui_manager_status_bar_t *out)
{
    if (!mgr || !out)
        return;
    *out = mgr->status_bar;
}

/* ------------------------------------------------------------------ */
/* WiFi config page                                                    */
/* ------------------------------------------------------------------ */

void ui_manager_show_wifi_config_page(ui_manager_t *mgr, const char *ssid, const char *password, const char *url)
{
    if (!mgr)
        return;
    ap_transfer_page_set_instruction_content((page_renderer_t *)page_registry_get_instance(UI_PAGE_AP_TRANSFER),
                                             "WiFi 传图", ssid, password, url, "", "长按 BOOT 退出");
    ui_manager_switch_page(mgr, UI_PAGE_AP_TRANSFER);
}

/* ------------------------------------------------------------------ */
/* Chat forwarding                                                     */
/* ------------------------------------------------------------------ */

void ui_manager_add_chat_message(ui_manager_t *mgr, const char *text, int role)
{
    if (!mgr)
        return;
    chat_page_add_message((page_renderer_t *)page_registry_get_instance(UI_PAGE_CHAT), text, (chat_role_t)role);
    page_renderer_mark_full_refresh((page_renderer_t *)page_registry_get_instance(UI_PAGE_CHAT));
    ui_manager_request_active_page_refresh(mgr);
}

void ui_manager_clear_chat(ui_manager_t *mgr)
{
    if (!mgr)
        return;
    chat_page_clear((page_renderer_t *)page_registry_get_instance(UI_PAGE_CHAT));
    page_renderer_mark_full_refresh((page_renderer_t *)page_registry_get_instance(UI_PAGE_CHAT));
    ui_manager_request_active_page_refresh(mgr);
}

void ui_manager_begin_chat_stream(ui_manager_t *mgr)
{
    if (!mgr)
        return;
    chat_page_begin_stream((page_renderer_t *)page_registry_get_instance(UI_PAGE_CHAT));
    page_renderer_mark_full_refresh((page_renderer_t *)page_registry_get_instance(UI_PAGE_CHAT));
    ui_manager_request_active_page_refresh(mgr);
}

bool ui_manager_append_chat_text(ui_manager_t *mgr, const char *chunk)
{
    if (!mgr)
        return false;
    bool ok = chat_page_append_text((page_renderer_t *)page_registry_get_instance(UI_PAGE_CHAT), chunk);
    if (ok) {
        page_renderer_mark_full_refresh((page_renderer_t *)page_registry_get_instance(UI_PAGE_CHAT));
        ui_manager_request_active_page_refresh(mgr);
    }
    return ok;
}

void ui_manager_end_chat_stream(ui_manager_t *mgr)
{
    if (!mgr)
        return;
    chat_page_end_stream((page_renderer_t *)page_registry_get_instance(UI_PAGE_CHAT));
    page_renderer_mark_full_refresh((page_renderer_t *)page_registry_get_instance(UI_PAGE_CHAT));
    ui_manager_request_active_page_refresh(mgr);
}

void ui_manager_show_chat_status(ui_manager_t *mgr, const char *status, int role)
{
    if (!mgr)
        return;
    chat_page_show_status((page_renderer_t *)page_registry_get_instance(UI_PAGE_CHAT), status, (chat_role_t)role);
    page_renderer_mark_full_refresh((page_renderer_t *)page_registry_get_instance(UI_PAGE_CHAT));
    ui_manager_request_active_page_refresh(mgr);
}

void ui_manager_hide_chat_status(ui_manager_t *mgr)
{
    if (!mgr)
        return;
    chat_page_hide_status((page_renderer_t *)page_registry_get_instance(UI_PAGE_CHAT));
    page_renderer_mark_full_refresh((page_renderer_t *)page_registry_get_instance(UI_PAGE_CHAT));
    ui_manager_request_active_page_refresh(mgr);
}

void ui_manager_set_chat_listening(ui_manager_t *mgr, bool listening)
{
    if (!mgr)
        return;
    chat_page_set_listening((page_renderer_t *)page_registry_get_instance(UI_PAGE_CHAT), listening);
    page_renderer_mark_full_refresh((page_renderer_t *)page_registry_get_instance(UI_PAGE_CHAT));
    ui_manager_request_active_page_refresh(mgr);
}

void ui_manager_set_chat_bottom_status(ui_manager_t *mgr, const char *status)
{
    if (!mgr)
        return;
    chat_page_set_bottom_status((page_renderer_t *)page_registry_get_instance(UI_PAGE_CHAT), status);
    page_renderer_mark_full_refresh((page_renderer_t *)page_registry_get_instance(UI_PAGE_CHAT));
    ui_manager_request_active_page_refresh(mgr);
}

/* ------------------------------------------------------------------ */
/* Settings / theme forwarding                                         */
/* ------------------------------------------------------------------ */

void ui_manager_set_settings_items(ui_manager_t *mgr, const void *items, int count)
{
    if (!mgr)
        return;
    settings_page_set_items((page_renderer_t *)page_registry_get_instance(UI_PAGE_SETTINGS), items, count);
    page_renderer_mark_full_refresh((page_renderer_t *)page_registry_get_instance(UI_PAGE_SETTINGS));
}

void ui_manager_update_settings_item(ui_manager_t *mgr, int index, const char *value)
{
    if (!mgr)
        return;
    settings_page_update_item((page_renderer_t *)page_registry_get_instance(UI_PAGE_SETTINGS), index, value);
    page_renderer_mark_full_refresh((page_renderer_t *)page_registry_get_instance(UI_PAGE_SETTINGS));
}

void ui_manager_update_settings_checked(ui_manager_t *mgr, int index, bool checked)
{
    if (!mgr)
        return;
    settings_page_update_checked((page_renderer_t *)page_registry_get_instance(UI_PAGE_SETTINGS), index, checked);
    page_renderer_mark_full_refresh((page_renderer_t *)page_registry_get_instance(UI_PAGE_SETTINGS));
}

void ui_manager_set_rawdraw_theme(ui_manager_t *mgr, int theme_id)
{
    if (!mgr)
        return;
    rawdraw_theme_set((rawdraw_theme_id_t)theme_id);
    epd_clock_set_color(&mgr->clock, rawdraw_theme_style(THEME_TOKEN_ACCENT).fg);
    nvs_state_set_string(RAWDRAW_THEME_NVS_KEY, rawdraw_theme_key((rawdraw_theme_id_t)theme_id));
    for (int i = 0; i < UI_PAGE_COUNT; ++i) {
        page_renderer_t *r = get_renderer_for_page(mgr, (ui_page_id_t)i);
        if (r)
            page_renderer_mark_full_refresh(r);
    }
    ui_manager_trigger_refresh(mgr, true);
}

int ui_manager_get_rawdraw_theme(const ui_manager_t *mgr)
{
    (void)mgr;
    return (int)rawdraw_theme_current_id();
}

/* ------------------------------------------------------------------ */
/* WiFi status forwarding                                              */
/* ------------------------------------------------------------------ */

void ui_manager_update_wifi_status(ui_manager_t *mgr, const void *status)
{
    if (!mgr)
        return;
    wifi_page_update((page_renderer_t *)page_registry_get_instance(UI_PAGE_WIFI), (const wifi_status_t *)status);
    page_renderer_mark_full_refresh((page_renderer_t *)page_registry_get_instance(UI_PAGE_WIFI));
}

void ui_manager_set_wifi_blinking(ui_manager_t *mgr, bool blinking)
{
    if (!mgr)
        return;
    wifi_page_set_blinking((page_renderer_t *)page_registry_get_instance(UI_PAGE_WIFI), blinking);
}

/* ------------------------------------------------------------------ */
/* Life bar forwarding                                                 */
/* ------------------------------------------------------------------ */

void ui_manager_set_lifebar_visible(ui_manager_t *mgr, bool visible)
{
#ifdef CONFIG_PAGE_LIFEBAR_ENABLE
    if (!mgr)
        return;
    lifebar_page_set_visible((page_renderer_t *)page_registry_get_instance(UI_PAGE_LIFEBAR), visible);
    page_renderer_mark_full_refresh((page_renderer_t *)page_registry_get_instance(UI_PAGE_LIFEBAR));
#endif
}

bool ui_manager_is_lifebar_visible(const ui_manager_t *mgr)
{
#ifdef CONFIG_PAGE_LIFEBAR_ENABLE
    return mgr && lifebar_page_is_visible((page_renderer_t *)page_registry_get_instance(UI_PAGE_LIFEBAR));
#else
    return false;
#endif
}

/* ------------------------------------------------------------------ */
/* Gallery slideshow                                                   */
/* ------------------------------------------------------------------ */

static void on_gallery_slideshow_timer(void *arg)
{
    ui_manager_t *mgr = (ui_manager_t *)arg;
    if (mgr) {
        mgr->gallery_slideshow_pending = true;
    }
}

static void arm_gallery_slideshow_timer(ui_manager_t *mgr)
{
    if (!mgr)
        return;
    if (!mgr->gallery_slideshow_timer) {
        esp_timer_create_args_t args = {
            .callback = on_gallery_slideshow_timer, .arg = mgr, .name = "gallery_slideshow"};
        esp_err_t err = esp_timer_create(&args, &mgr->gallery_slideshow_timer);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create gallery slideshow timer: %d", err);
            return;
        }
    }
    esp_timer_stop(mgr->gallery_slideshow_timer);
    int interval = mgr->gallery_slideshow_interval_minutes;
    if (interval <= 0) {
        return;
    }
    esp_timer_start_once(mgr->gallery_slideshow_timer, (uint64_t)interval * 60 * 1000000ULL);
    ESP_LOGI(TAG, "Armed gallery slideshow timer for %d minute(s)", interval);
}

static bool advance_gallery_slideshow(ui_manager_t *mgr)
{
    if (!mgr)
        return false;
    arm_gallery_slideshow_timer(mgr);
    if (mgr->current_page != UI_PAGE_GALLERY) {
        return false;
    }
    ESP_LOGI(TAG, "Advancing gallery slideshow");
    bool advanced = photo_gallery_select_next((page_renderer_t *)page_registry_get_instance(UI_PAGE_GALLERY), true);
    if (advanced) {
        ui_manager_request_active_page_refresh(mgr);
    }
    return advanced;
}

void ui_manager_set_gallery_slideshow_interval_minutes(ui_manager_t *mgr, int minutes)
{
    if (!mgr)
        return;
    mgr->gallery_slideshow_interval_minutes = (minutes > 0) ? minutes : 0;
    mgr->gallery_slideshow_pending = false;
    arm_gallery_slideshow_timer(mgr);
}

int ui_manager_get_gallery_slideshow_interval_minutes(const ui_manager_t *mgr)
{
    return mgr ? mgr->gallery_slideshow_interval_minutes : 0;
}

bool ui_manager_show_photo_by_id(ui_manager_t *mgr, const char *photo_id)
{
    if (!mgr)
        return false;
    return photo_gallery_set_selected_by_id((page_renderer_t *)page_registry_get_instance(UI_PAGE_GALLERY), photo_id);
}

/* ------------------------------------------------------------------ */
/* Clock / transient refresh pump                                      */
/* ------------------------------------------------------------------ */

void ui_manager_pump_clock_refresh(ui_manager_t *mgr)
{
    if (!mgr)
        return;

    /* No input lock — allow pump-driven refreshes freely. */

    if (mgr->gallery_slideshow_pending) {
        mgr->gallery_slideshow_pending = false;
        advance_gallery_slideshow(mgr);
    }
    bool page_pending = mgr->active_page_refresh_pending;
    mgr->active_page_refresh_pending = false;
    bool transient_pending = mgr->transient_refresh_pending;
    mgr->transient_refresh_pending = false;

    if (page_pending || transient_pending) {
        ESP_LOGI(TAG, "pump: consuming page_pending=%d transient=%d", page_pending ? 1 : 0, transient_pending ? 1 : 0);
        if (mgr->refresh_cb) {
            mgr->refresh_cb((rawdraw_rect_t){0, 0, mgr->width, mgr->height}, false, mgr->refresh_ctx);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Voice wakeup                                                        */
/* ------------------------------------------------------------------ */

void ui_manager_voice_wakeup_tick(ui_manager_t *mgr)
{
    if (!mgr)
        return;
    int64_t now = esp_timer_get_time();
    widget_voice_wakeup_tick(&mgr->voice_wakeup, now);
}

void ui_manager_voice_wakeup_trigger(ui_manager_t *mgr, bool network_available)
{
    if (!mgr)
        return;
    if (network_available) {
        widget_voice_wakeup_start_recording(&mgr->voice_wakeup);
    } else {
        widget_voice_wakeup_show_offline(&mgr->voice_wakeup);
    }
    if (mgr->refresh_cb) {
        mgr->refresh_cb((rawdraw_rect_t){0, 0, mgr->width, mgr->height}, false, mgr->refresh_ctx);
    }
}

void ui_manager_voice_wakeup_done(ui_manager_t *mgr)
{
    if (!mgr)
        return;
    widget_voice_wakeup_done(&mgr->voice_wakeup);
    if (mgr->refresh_cb) {
        mgr->refresh_cb((rawdraw_rect_t){0, 0, mgr->width, mgr->height}, false, mgr->refresh_ctx);
    }
}

bool ui_manager_voice_wakeup_is_active(const ui_manager_t *mgr)
{
    return mgr && widget_voice_wakeup_is_visible(&mgr->voice_wakeup);
}

/* ------------------------------------------------------------------ */
/* Dimensions                                                          */
/* ------------------------------------------------------------------ */

int ui_manager_get_width(const ui_manager_t *mgr)
{
    return mgr ? mgr->width : STYLE_SCREEN_WIDTH;
}

int ui_manager_get_height(const ui_manager_t *mgr)
{
    return mgr ? mgr->height : STYLE_SCREEN_HEIGHT;
}

/* ------------------------------------------------------------------ */
/* Callbacks                                                           */
/* ------------------------------------------------------------------ */

void ui_manager_set_page_switch_callback(ui_manager_t *mgr, ui_manager_page_switch_cb_t cb, void *user_data)
{
    if (!mgr)
        return;
    mgr->page_switch_cb = cb;
    mgr->page_switch_ctx = user_data;
}
