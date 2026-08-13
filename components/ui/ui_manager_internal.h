/**
 * @file ui_manager_internal.h
 * @brief Internal header exposing the ui_manager struct for sub-modules.
 *
 * The struct is opaque to external callers (ui_manager.h) but sub-modules
 * within the ui component need field access for rendering.
 */
#ifndef COMPONENTS_UI_UI_MANAGER_INTERNAL_H_
#define COMPONENTS_UI_UI_MANAGER_INTERNAL_H_

#include "ui_manager.h"
#include "page_registry.h"
#include "rawdraw_ext.h"
#include "clock.h"
#include "voice_wakeup.h"
#include <esp_timer.h>

struct ui_manager {
    int width;
    int height;

    ui_page_id_t current_page;

    ui_manager_status_bar_t status_bar;
    ui_manager_refresh_cb_t refresh_cb;
    void *refresh_ctx;
    ui_manager_page_switch_cb_t page_switch_cb;
    void *page_switch_ctx;

    bool quick_switch_open;
    int quick_switch_index;
    int quick_switch_first_visible;
    const page_entry_t *quick_items[UI_PAGE_COUNT];
    int quick_count;

    widget_voice_wakeup_state_t voice_wakeup;

    esp_timer_handle_t gallery_slideshow_timer;
    volatile bool clock_refresh_pending;
    volatile bool transient_refresh_pending;
    volatile bool active_page_refresh_pending;
    volatile bool gallery_slideshow_pending;
    int last_clock_minute_key;
    int gallery_slideshow_interval_minutes;

    epd_clock_t clock;

    void (*data_refresh_cb)(ui_page_id_t page, void *ctx);
    void *data_refresh_ctx;
};

/* ---- Sub-module functions (defined in ui_status_bar.c / ui_quick_switch.c) ---- */

/* ui_status_bar.c — top status bar rendering */
void ui_status_bar_draw(struct ui_manager *mgr, uint8_t *fb, int width, int height);

/* ui_quick_switch.c — quick-switch overlay rendering */
void ui_quick_switch_draw_overlay(struct ui_manager *mgr, uint8_t *fb, int width, int height);
rawdraw_rect_t ui_quick_switch_get_bounds(const struct ui_manager *mgr);

#define QUICK_SWITCH_VISIBLE_COUNT 5

#endif /* COMPONENTS_UI_UI_MANAGER_INTERNAL_H_ */
