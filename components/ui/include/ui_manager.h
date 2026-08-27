/**
 * @file ui_manager.h
 * @brief RawDraw UI manager — C port of C++ ui::RawDrawUiManager.
 *
 * Owns all 19 page renderers (statically pre-allocated per plan to avoid
 * heap fragmentation), routes button events, draws the status bar and
 * global page frame, and provides page data update APIs.
 */
#ifndef COMPONENTS_UI_INCLUDE_UI_MANAGER_H_
#define COMPONENTS_UI_INCLUDE_UI_MANAGER_H_

#include <stdint.h>
#include <stdbool.h>

#include "page_renderer.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Page identifiers                                                    */
/* ------------------------------------------------------------------ */

typedef enum {
    UI_PAGE_CHAT = 0,
    UI_PAGE_EBOOK,
    UI_PAGE_WIFI,
    UI_PAGE_SETTINGS,
    UI_PAGE_GALLERY,
    UI_PAGE_WEATHER,
    UI_PAGE_NEWS,
    UI_PAGE_WEATHER_DETAIL,
    UI_PAGE_PHOTO_DETAIL,
    UI_PAGE_LIFEBAR,
    UI_PAGE_ALMANAC,
    UI_PAGE_LOG,
    UI_PAGE_YEAR_PROGRESS,
    UI_PAGE_CALENDAR,
    UI_PAGE_FONT_DEBUG,
    UI_PAGE_FONT_METRICS,
    UI_PAGE_AP_TRANSFER,
    UI_PAGE_CAR_MOVE,
    UI_PAGE_CODING_PLAN,
    UI_PAGE_FRIDGE_MEMO,
    UI_PAGE_COUNT,
} ui_page_id_t;

/* ------------------------------------------------------------------ */
/* Status bar data                                                     */
/* ------------------------------------------------------------------ */

typedef struct {
    char page_title[32];
    char central_text[48]; /* overrides page_title when non-empty */
    bool wifi_connected;
    bool server_connected;
    bool bluetooth_enabled;
    int battery_level; /* -1 = unknown */
    bool battery_charging;
    bool battery_vertical;
    char date_format[8]; /* "" = M月D日, "iso", "hidden" */
    char server_date[16]; /* yyyy-mm-dd from server, fallback for RTC */
    char server_weekday[8]; /* 周一~周日 from server */
} ui_manager_status_bar_t;

/* ------------------------------------------------------------------ */
/* Refresh callback                                                    */
/* ------------------------------------------------------------------ */

typedef void (*ui_manager_refresh_cb_t)(rawdraw_rect_t rect, bool urgent, void *user_data);
typedef void (*ui_manager_page_switch_cb_t)(ui_page_id_t page, void *user_data);

/* ------------------------------------------------------------------ */
/* UI manager                                                          */
/* ------------------------------------------------------------------ */

typedef struct ui_manager ui_manager_t;

/**
 * @brief Allocate a UI manager instance (heap-allocated, contains all
 *        statically pre-allocated page renderers). Call ui_manager_delete()
 *        to release.
 */
ui_manager_t *ui_manager_create(void);
void ui_manager_delete(ui_manager_t *mgr);

/**
 * @brief Initialize the UI manager.
 *
 * @param refresh_cb Optional callback triggered after RenderAll to push the
 *                   framebuffer to the EPD. May be NULL.
 * @param user_data  Opaque pointer passed to refresh callback.
 */
void ui_manager_init(ui_manager_t *mgr, ui_manager_refresh_cb_t refresh_cb, void *user_data);
void ui_manager_set_refresh_callback(ui_manager_t *mgr, ui_manager_refresh_cb_t cb, void *user_data);
void ui_manager_set_page_switch_callback(ui_manager_t *mgr, ui_manager_page_switch_cb_t cb, void *user_data);

/* Page switching. */
void ui_manager_switch_page(ui_manager_t *mgr, ui_page_id_t page);
void ui_manager_set_current_page_without_render(ui_manager_t *mgr, ui_page_id_t page);
ui_page_id_t ui_manager_get_current_page(const ui_manager_t *mgr);
ui_page_id_t ui_manager_get_rtc_saved_page(void);
page_renderer_t *ui_manager_get_active_renderer(const ui_manager_t *mgr);
page_renderer_t *ui_manager_get_renderer(const ui_manager_t *mgr, ui_page_id_t page);
bool ui_manager_is_display_refresh_pending(const ui_manager_t *mgr);
const char *ui_manager_get_page_title(ui_page_id_t page);

bool ui_manager_handle_input(ui_manager_t *mgr, const ui_button_event_t *event);
bool ui_manager_is_quick_switch_open(const ui_manager_t *mgr);

/* WiFi config page. */
void ui_manager_show_wifi_config_page(ui_manager_t *mgr, const char *ssid, const char *password, const char *url);

/* Rendering. */
void ui_manager_render_all(ui_manager_t *mgr, uint8_t *fb, int width, int height);
void ui_manager_update_status_bar(ui_manager_t *mgr, const ui_manager_status_bar_t *data);
void ui_manager_get_status_bar_data(ui_manager_t *mgr, ui_manager_status_bar_t *out);

/* Page data update APIs (forwarded to the page renderers). */
void ui_manager_add_chat_message(ui_manager_t *mgr, const char *text, int role);
void ui_manager_clear_chat(ui_manager_t *mgr);
void ui_manager_begin_chat_stream(ui_manager_t *mgr);
bool ui_manager_append_chat_text(ui_manager_t *mgr, const char *chunk);
void ui_manager_end_chat_stream(ui_manager_t *mgr);
void ui_manager_show_chat_status(ui_manager_t *mgr, const char *status, int role);
void ui_manager_hide_chat_status(ui_manager_t *mgr);
void ui_manager_set_chat_listening(ui_manager_t *mgr, bool listening);
void ui_manager_set_chat_bottom_status(ui_manager_t *mgr, const char *status);

/* Settings page. */
void ui_manager_set_settings_items(ui_manager_t *mgr, const void *items, int count);
void ui_manager_update_settings_item(ui_manager_t *mgr, int index, const char *value);
void ui_manager_update_settings_checked(ui_manager_t *mgr, int index, bool checked);

/* Theme. */
void ui_manager_set_rawdraw_theme(ui_manager_t *mgr, int theme_id);
int ui_manager_get_rawdraw_theme(const ui_manager_t *mgr);

/* Fridge memo page. */
void ui_manager_update_fridge_memo(ui_manager_t *mgr, const void *snapshot);
void ui_manager_set_fridge_memo_footer(ui_manager_t *mgr, const char *msg);
void ui_manager_set_fridge_memo_offline(ui_manager_t *mgr, bool offline);

/* WiFi status. */
void ui_manager_update_wifi_status(ui_manager_t *mgr, const void *status);
void ui_manager_set_wifi_blinking(ui_manager_t *mgr, bool blinking);

/* Life bar. */
void ui_manager_set_lifebar_visible(ui_manager_t *mgr, bool visible);
bool ui_manager_is_lifebar_visible(const ui_manager_t *mgr);

/* Gallery slideshow. */
void ui_manager_set_gallery_slideshow_interval_minutes(ui_manager_t *mgr, int minutes);
int ui_manager_get_gallery_slideshow_interval_minutes(const ui_manager_t *mgr);
bool ui_manager_show_photo_by_id(ui_manager_t *mgr, const char *photo_id);

/* Refresh control. */
void ui_manager_request_full_refresh(ui_manager_t *mgr);
void ui_manager_request_active_page_refresh(ui_manager_t *mgr);
void ui_manager_trigger_refresh(ui_manager_t *mgr, bool urgent);
void ui_manager_pump_clock_refresh(ui_manager_t *mgr);
void ui_manager_set_data_refresh_cb(ui_manager_t *mgr, void (*cb)(ui_page_id_t page, void *ctx), void *ctx);
void ui_manager_request_data_refresh(ui_manager_t *mgr, ui_page_id_t page);

/* Voice wakeup. */
void ui_manager_voice_wakeup_tick(ui_manager_t *mgr);
void ui_manager_voice_wakeup_trigger(ui_manager_t *mgr, bool network_available);
void ui_manager_voice_wakeup_done(ui_manager_t *mgr);
bool ui_manager_voice_wakeup_is_active(const ui_manager_t *mgr);

/* Display dimensions. */
int ui_manager_get_width(const ui_manager_t *mgr);
int ui_manager_get_height(const ui_manager_t *mgr);

#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_UI_INCLUDE_UI_MANAGER_H_ */
