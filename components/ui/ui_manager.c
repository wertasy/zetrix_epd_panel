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
#include "custom_lcd_display.h"
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
#include "pages/ap_transfer_server.h"
#include "wifi_manager.h"

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
/* UI manager state                                                    */
/* ------------------------------------------------------------------ */

struct ui_manager {
    int width;
    int height;

    ui_page_id_t current_page;

    ui_manager_status_bar_t     status_bar;
    ui_manager_refresh_cb_t     refresh_cb;
    void                       *refresh_ctx;
    ui_manager_page_switch_cb_t page_switch_cb;
    void                       *page_switch_ctx;

    bool quick_switch_open;
    int  quick_switch_index;
    int  quick_switch_first_visible;
    /* Quick-switch items cache (populated from page_registry in init). */
    const page_entry_t *quick_items[UI_PAGE_COUNT];
    int                 quick_count;

    /* AP transfer server (optional, statically allocated). */
    ap_transfer_server_t ap_server;

    /* Voice wakeup overlay. */
    widget_voice_wakeup_state_t voice_wakeup;

    /* esp_timer handles. */
    esp_timer_handle_t gallery_slideshow_timer;
    volatile bool      clock_refresh_pending;
    volatile bool      transient_refresh_pending;
    volatile bool      active_page_refresh_pending;
    volatile bool      gallery_slideshow_pending;
    int                last_clock_minute_key;
    int                gallery_slideshow_interval_minutes;

    epd_clock_t clock;
    /* App-specific callbacks to delegate events to UI thread event queue */
    void (*app_image_received_cb)(const char *photo_id, void *ctx);
    void (*app_settings_changed_cb)(int slideshow_interval_minutes, void *ctx);
    void (*app_photos_changed_cb)(void *ctx);
    bool (*app_show_photo_cb)(const char *photo_id, void *ctx);
    void *app_cb_ctx;
};

/* ------------------------------------------------------------------ */
/* Quick switch items                                                  */
/* ------------------------------------------------------------------ */

/* Page titles and quick-switch entries are now provided by page_registry;
 * the old kQuickSwitchItems array and QUICK_SWITCH_COUNT macro are gone. */
#define QUICK_SWITCH_VISIBLE_COUNT 5

/* ------------------------------------------------------------------ */
/* Static helpers                                                      */
/* ------------------------------------------------------------------ */

static bool is_navigation_click(const ui_button_event_t *event)
{
    return event->type == BTN_UP_CLICK || event->type == BTN_DOWN_CLICK || event->type == BTN_BOOT_CLICK;
}

static int current_local_minute_key(void)
{
    time_t    now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    return tm_buf.tm_year * 366 * 24 * 60 + tm_buf.tm_yday * 24 * 60 + tm_buf.tm_hour * 60 + tm_buf.tm_min;
}

static void draw_battery_icon(uint8_t *fb, int width, int height, int x, int y, int level, bool vertical)
{
    if (level < 0)
        level = 0;
    if (level > 100)
        level = 100;
    const rawdraw_color_t battery_color =
        level <= 15 ? rawdraw_theme_style(THEME_TOKEN_DANGER).border : rawdraw_theme_style(THEME_TOKEN_TEXT_PRIMARY).fg;
    if (vertical) {
        const int body_w = 9;
        const int body_h = 14;
        rawdraw_draw_rect_border(fb, width, height, (rawdraw_rect_t){x, y + 2, body_w, body_h}, 1, battery_color);
        rawdraw_fill_rect(fb, width, height, (rawdraw_rect_t){x + 3, y, 3, 2}, battery_color);
        const int seg_h  = 3;
        const int gap    = 1;
        const int filled = level >= 90 ? 3 : (level >= 50 ? 2 : (level > 10 ? 1 : 0));
        for (int i = 0; i < 3; ++i) {
            if (i >= 3 - filled) {
                const int sy = y + 3 + i * (seg_h + gap);
                rawdraw_fill_rect(fb, width, height, (rawdraw_rect_t){x + 2, sy, body_w - 4, seg_h}, battery_color);
            }
        }
    } else {
        const int body_w = 24;
        const int body_h = 12;
        rawdraw_draw_rect_border(fb, width, height, (rawdraw_rect_t){x, y, body_w, body_h}, 1, battery_color);
        rawdraw_fill_rect(fb, width, height, (rawdraw_rect_t){x + body_w, y + 3, 2, 6}, battery_color);
        const int seg_w  = 6;
        const int gap    = 1;
        const int filled = level >= 90 ? 3 : (level >= 50 ? 2 : (level > 10 ? 1 : 0));
        for (int i = 0; i < filled; ++i) {
            const int sx = x + 2 + i * (seg_w + gap);
            rawdraw_fill_rect(fb, width, height, (rawdraw_rect_t){sx, y + 2, seg_w, body_h - 4}, battery_color);
        }
    }
}

static void draw_server_status_marker(uint8_t *fb, int width, int height, int x, int center_y, bool server_connected,
                                      bool wifi_connected)
{
    if (!fb || width <= 0)
        return;
    const rawdraw_color_t success = rawdraw_theme_style(THEME_TOKEN_SUCCESS_LIKE).fg;
    const rawdraw_color_t warning = rawdraw_theme_style(THEME_TOKEN_WARNING).border;
    if (server_connected) {
        rawdraw_draw_line(fb, width, height, (rawdraw_point_t){x - 4, center_y}, (rawdraw_point_t){x + 4, center_y},
                          success);
        rawdraw_draw_line(fb, width, height, (rawdraw_point_t){x, center_y - 4}, (rawdraw_point_t){x, center_y + 4},
                          success);
        rawdraw_draw_line(fb, width, height, (rawdraw_point_t){x - 3, center_y - 3},
                          (rawdraw_point_t){x + 3, center_y + 3}, success);
        rawdraw_draw_line(fb, width, height, (rawdraw_point_t){x - 3, center_y + 3},
                          (rawdraw_point_t){x + 3, center_y - 3}, success);
        return;
    }
    if (wifi_connected) {
        rawdraw_draw_rect_border(fb, width, height, (rawdraw_rect_t){x - 3, center_y - 3, 7, 7}, 1, warning);
    }
}

static void draw_mini_time_digit(uint8_t *fb, int width, int height, int x, int y, char digit, rawdraw_color_t color)
{
    if (digit < '0' || digit > '9')
        return;
    /* Seven-segment bits: A B C D E F G. */
    static const uint8_t kSegments[10] = {
        0b1111110, 0b0110000, 0b1101101, 0b1111001, 0b0110011, 0b1011011, 0b1011111, 0b1110000, 0b1111111, 0b1111011,
    };
    const uint8_t seg = kSegments[digit - '0'];
    if (seg & 0b1000000)
        rawdraw_draw_hline(fb, width, height, y, x + 1, x + 4, color);
    if (seg & 0b0100000)
        rawdraw_draw_vline(fb, width, height, x + 5, y + 1, y + 4, color);
    if (seg & 0b0010000)
        rawdraw_draw_vline(fb, width, height, x + 5, y + 6, y + 9, color);
    if (seg & 0b0001000)
        rawdraw_draw_hline(fb, width, height, y + 10, x + 1, x + 4, color);
    if (seg & 0b0000100)
        rawdraw_draw_vline(fb, width, height, x, y + 6, y + 9, color);
    if (seg & 0b0000010)
        rawdraw_draw_vline(fb, width, height, x, y + 1, y + 4, color);
    if (seg & 0b0000001)
        rawdraw_draw_hline(fb, width, height, y + 5, x + 1, x + 4, color);
}

static int mini_time_width(const char *text)
{
    (void)text;
    return 32; /* "HH:MM": 4 digits * 6px + colon 2px + gaps. */
}

static void draw_mini_time_text(uint8_t *fb, int width, int height, int x, int y, const char *text,
                                rawdraw_color_t color)
{
    if (!fb || !text || strlen(text) < 5)
        return;
    int cursor_x = x;
    for (int i = 0; i < 5; ++i) {
        if (text[i] == ':') {
            rawdraw_set_pixel(fb, width, height, cursor_x, y + 3, color);
            rawdraw_set_pixel(fb, width, height, cursor_x, y + 7, color);
            cursor_x += 4;
        } else if (text[i] == '-') {
            rawdraw_draw_hline(fb, width, height, y + 5, cursor_x + 1, cursor_x + 4, color);
            cursor_x += 7;
        } else {
            draw_mini_time_digit(fb, width, height, cursor_x, y, text[i], color);
            cursor_x += 7;
        }
    }
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

static void draw_status_bar(ui_manager_t *mgr, uint8_t *fb, int width, int height)
{
    int                         bar_height   = STYLE_STATUS_BAR_HEIGHT;
    int                         padding      = STYLE_STATUS_BAR_PADDING;
    const lv_font_t            *title_font   = &SourceHanSansSC_Regular_slim;
    const rawdraw_paint_style_t bg_style     = rawdraw_theme_style(THEME_TOKEN_BACKGROUND_PRIMARY);
    const rawdraw_paint_style_t text_style   = rawdraw_theme_style(THEME_TOKEN_TEXT_PRIMARY);
    const rawdraw_paint_style_t accent_style = rawdraw_theme_style(THEME_TOKEN_ACCENT);
    const rawdraw_paint_style_t border_style = rawdraw_theme_style(THEME_TOKEN_BORDER);
    const rawdraw_paint_style_t danger_style = rawdraw_theme_style(THEME_TOKEN_DANGER);

    rawdraw_draw_styled_rect(fb, width, height, (rawdraw_rect_t){0, 0, width, bar_height}, &bg_style);
    rawdraw_fill_rect(
        fb, width, height,
        (rawdraw_rect_t){1, bar_height - STYLE_SHELL_DIVIDER_THICKNESS, width - 2, STYLE_SHELL_DIVIDER_THICKNESS},
        border_style.border);

    const int center_y          = bar_height / 2;
    const int sig_bar_w         = 3;
    const int sig_bar_gap       = 2;
    const int sig_bar_heights[] = {6, 9, 12, 15};
    const int wifi_group_w      = 4 * sig_bar_w + 3 * sig_bar_gap;
    int       x                 = padding + 3;

    if (mgr->status_bar.wifi_connected) {
        for (int i = 0; i < 4; ++i) {
            int bx = x + i * (sig_bar_w + sig_bar_gap);
            int by = center_y + 7 - sig_bar_heights[i];
            rawdraw_fill_rect(fb, width, height, (rawdraw_rect_t){bx, by, sig_bar_w, sig_bar_heights[i]},
                              rawdraw_theme_style(THEME_TOKEN_SUCCESS_LIKE).fg);
        }
    } else {
        for (int i = 0; i < 4; ++i) {
            int bx = x + i * (sig_bar_w + sig_bar_gap);
            int by = center_y + 7 - sig_bar_heights[i];
            rawdraw_draw_rect_border(fb, width, height, (rawdraw_rect_t){bx, by, sig_bar_w, sig_bar_heights[i]}, 1,
                                     text_style.fg);
        }
        rawdraw_draw_line(fb, width, height, (rawdraw_point_t){x, center_y - 6},
                          (rawdraw_point_t){x + wifi_group_w, center_y + 7}, danger_style.border);
    }

    const int marker_x = x + wifi_group_w + 10;
    draw_server_status_marker(fb, width, height, marker_x, center_y, mgr->status_bar.server_connected,
                              mgr->status_bar.wifi_connected);
    int left_content_x = marker_x + 14;
    if (mgr->status_bar.bluetooth_enabled) {
        const char *bt_icon = FA_SETTINGS_BLUETOOTH;
        const int   bt_y    = rawdraw_layout_ink_centered_text_top_y(&fa_settings_16, bt_icon, center_y, 0);
        rawdraw_draw_styled_text(fb, width, height, left_content_x, bt_y, bt_icon, &fa_settings_16, &accent_style);
        left_content_x += rawdraw_measure_text_width(bt_icon, &fa_settings_16) + 8;
    }

    /* Date + weekday on left side, after server marker (skip if "hidden"). */
    char       date_str[32] = {0};
    const bool hide_date    = strcmp(mgr->status_bar.date_format, "hidden") == 0;
    if (!hide_date) {
        if (mgr->status_bar.server_date[0] != '\0') {
            if (strcmp(mgr->status_bar.date_format, "iso") == 0) {
                strncpy(date_str, mgr->status_bar.server_date, sizeof(date_str) - 1);
            } else {
                int y = 0, m = 0, d = 0;
                if (sscanf(mgr->status_bar.server_date, "%d-%d-%d", &y, &m, &d) == 3) {
                    snprintf(date_str, sizeof(date_str), "%d月%d日", m, d);
                } else {
                    /* Malformed date — fall back to the raw iso string to avoid "0月0日". */
                    strncpy(date_str, mgr->status_bar.server_date, sizeof(date_str) - 1);
                }
            }
            if (mgr->status_bar.server_weekday[0] != '\0') {
                strncat(date_str, " ", sizeof(date_str) - strlen(date_str) - 1);
                strncat(date_str, mgr->status_bar.server_weekday, sizeof(date_str) - strlen(date_str) - 1);
            }
        } else {
            const bool iso = strcmp(mgr->status_bar.date_format, "iso") == 0;
            epd_clock_get_date_string(date_str, sizeof(date_str), iso);
        }
    }
    const int date_x = left_content_x;
    const int date_w = hide_date ? 0 : rawdraw_measure_text_width(date_str, title_font);
    if (!hide_date) {
        rawdraw_draw_styled_text(fb, width, height, date_x,
                                 rawdraw_layout_ink_centered_text_top_y(title_font, date_str, center_y, 0), date_str,
                                 title_font, &text_style);
    }
    const int left_safe = date_x + date_w + 8;

    const char *time_str = epd_clock_get_time_string();
    if (!time_str || time_str[0] == '\0') {
        time_str = "--:--";
    }

    const int battery_slot_w = 30;
    int       right_x        = width - padding;
    if (mgr->status_bar.battery_level >= 0) {
        const int battery_h = mgr->status_bar.battery_vertical ? 16 : 12;
        const int battery_w = mgr->status_bar.battery_vertical ? 9 : 26;
        const int battery_x = width - padding - battery_slot_w + (battery_slot_w - battery_w);
        const int battery_y = center_y - battery_h / 2;
        draw_battery_icon(fb, width, height, battery_x, battery_y, mgr->status_bar.battery_level,
                          mgr->status_bar.battery_vertical);
        right_x = battery_x - 8;
    }

    /* Time on right side. */
    const int clock_w = mini_time_width(time_str);
    draw_mini_time_text(fb, width, height, right_x - clock_w, center_y - 5, time_str, accent_style.fg);
    right_x = right_x - clock_w - 6;

    const char *title =
        mgr->status_bar.central_text[0] != '\0' ? mgr->status_bar.central_text : mgr->status_bar.page_title;
    const int   right_safe = (left_safe + 40 > right_x - 2) ? left_safe + 40 : right_x - 2;
    const char *title_icon = NULL;
    for (int i = 0; i < mgr->quick_count; ++i) {
        if (mgr->quick_items[i]->id == mgr->current_page) {
            title_icon = mgr->quick_items[i]->icon;
            break;
        }
    }
    const int title_icon_gap = (title_icon && title_icon[0] != '\0') ? 5 : 0;
    const int title_icon_w =
        (title_icon && title_icon[0] != '\0') ? rawdraw_measure_text_width(title_icon, &fa_settings_16) : 0;
    const int title_max_w = (44 > right_safe - left_safe) ? 44 : (right_safe - left_safe);
    const int text_max_w =
        (20 > title_max_w - title_icon_w - title_icon_gap) ? 20 : (title_max_w - title_icon_w - title_icon_gap);
    char display_title[64];
    ui_text_fit_to_width(title, title_font, text_max_w, display_title, sizeof(display_title));
    int title_text_w = rawdraw_measure_text_width(display_title, title_font);
    int title_w      = title_icon_w + title_icon_gap + title_text_w;
    int title_x      = (width - title_w) / 2;
    if (title_x < left_safe)
        title_x = left_safe;
    if (title_x + title_w > right_safe)
        title_x = (left_safe > right_safe - title_w) ? left_safe : (right_safe - title_w);
    const int title_y      = rawdraw_layout_ink_centered_text_top_y_in_box(title_font, display_title, 0, bar_height, 0);
    int       title_text_x = title_x;
    if (title_icon_w > 0) {
        const int icon_y = rawdraw_layout_ink_centered_text_top_y_in_box(&fa_settings_16, title_icon, 0, bar_height, 0);
        rawdraw_draw_styled_text(fb, width, height, title_x, icon_y, title_icon, &fa_settings_16, &text_style);
        title_text_x += title_icon_w + title_icon_gap;
    }
    rawdraw_draw_styled_text(fb, width, height, title_text_x, title_y, display_title, title_font, &text_style);
}

static void draw_global_page_frame(uint8_t *fb, int width, int height)
{
    if (!fb || width <= 4 || height <= 4)
        return;
    const rawdraw_paint_style_t border = rawdraw_theme_style(THEME_TOKEN_BORDER);
    rawdraw_draw_round_rect_border(fb, width, height, (rawdraw_rect_t){1, 1, width - 2, height - 2},
                                   STYLE_BORDER_RADIUS_MD, border.border_width, border.border);
}

/* ------------------------------------------------------------------ */
/* Quick switch overlay                                                */
/* ------------------------------------------------------------------ */

static rawdraw_rect_t get_quick_switch_bounds(const ui_manager_t *mgr)
{
    const int overlay_w = 224;
    const int overlay_h = 204;
    return (rawdraw_rect_t){(mgr->width - overlay_w) / 2, STYLE_STATUS_BAR_HEIGHT + 26, overlay_w, overlay_h};
}

static void draw_quick_switch_overlay(ui_manager_t *mgr, uint8_t *fb, int width, int height)
{
    const rawdraw_rect_t        overlay        = get_quick_switch_bounds(mgr);
    const int                   overlay_x      = overlay.x;
    const int                   overlay_y      = overlay.y;
    const int                   overlay_w      = overlay.w;
    const int                   overlay_h      = overlay.h;
    const int                   titlebar_h     = 28;
    const int                   shadow_offset  = 2;
    const int                   item_h         = 24;
    const int                   item_gap       = 3;
    const lv_font_t            *font           = &SourceHanSansSC_Regular_slim;
    const lv_font_t            *title_font     = &SourceHanSansSC_Regular_slim;
    const rawdraw_paint_style_t modal_style    = rawdraw_theme_component(ROLE_MODAL);
    const rawdraw_paint_style_t selected_style = rawdraw_theme_component(ROLE_QUICK_SWITCH_ROW);
    const rawdraw_paint_style_t text_style     = rawdraw_theme_style(THEME_TOKEN_TEXT_PRIMARY);
    const rawdraw_paint_style_t border_style   = rawdraw_theme_style(THEME_TOKEN_BORDER);
    const rawdraw_paint_style_t shadow_style   = rawdraw_theme_style(THEME_TOKEN_SHADOW);

    rawdraw_draw_styled_round_rect(
        fb, width, height, (rawdraw_rect_t){overlay_x + shadow_offset, overlay_y + shadow_offset, overlay_w, overlay_h},
        STYLE_BORDER_RADIUS_MD, &shadow_style);
    rawdraw_draw_styled_round_rect(fb, width, height, (rawdraw_rect_t){overlay_x, overlay_y, overlay_w, overlay_h},
                                   STYLE_BORDER_RADIUS_MD, &modal_style);
    const char *title   = "快速切换";
    const int   title_w = rawdraw_measure_text_width(title, title_font);
    rawdraw_draw_styled_text(fb, width, height, overlay_x + (overlay_w - title_w) / 2,
                             rawdraw_layout_ink_centered_text_top_y_in_box(title_font, title, overlay_y, titlebar_h, 0),
                             title, title_font, &text_style);
    rawdraw_draw_hline(fb, width, height, overlay_y + titlebar_h, overlay_x + 1, overlay_x + overlay_w - 2,
                       border_style.border);
    for (int yy = overlay_y + 6; yy < overlay_y + titlebar_h - 5; yy += 4) {
        rawdraw_draw_hline(fb, width, height, yy, overlay_x + 14, overlay_x + (overlay_w - title_w) / 2 - 8,
                           border_style.border);
        rawdraw_draw_hline(fb, width, height, yy, overlay_x + (overlay_w + title_w) / 2 + 8, overlay_x + overlay_w - 14,
                           border_style.border);
    }

    int y = overlay_y + titlebar_h + 10;
    for (int vi = 0; vi < QUICK_SWITCH_VISIBLE_COUNT; ++vi) {
        int i = mgr->quick_switch_first_visible + vi;
        if (i >= mgr->quick_count)
            break;
        const bool                  selected  = i == mgr->quick_switch_index;
        const int                   row_x     = overlay_x + 12;
        const int                   row_w     = overlay_w - 24 - 10;
        const rawdraw_paint_style_t row_style = selected ? selected_style : modal_style;
        rawdraw_draw_styled_round_rect(fb, width, height, (rawdraw_rect_t){row_x, y, row_w, item_h},
                                       STYLE_BORDER_RADIUS_MD, &row_style);
        if (selected) {
            rawdraw_fill_rect(fb, width, height, (rawdraw_rect_t){row_x + 5, y + 6, 3, item_h - 12}, row_style.border);
        }
        const lv_font_t      *icon_font    = &fa_settings_16;
        const int             icon_gap     = 6;
        const int             text_start_x = row_x + 18;
        const rawdraw_color_t text_color   = row_style.fg;

        if (mgr->quick_items[i]->icon && mgr->quick_items[i]->icon[0] != '\0') {
            const int icon_x = text_start_x;
            const int icon_y =
                rawdraw_layout_ink_centered_text_top_y_in_box(icon_font, mgr->quick_items[i]->icon, y, item_h, 0);
            rawdraw_draw_text(fb, width, height, icon_x, icon_y, mgr->quick_items[i]->icon, icon_font, text_color);
            const int icon_w  = rawdraw_measure_text_width(mgr->quick_items[i]->icon, icon_font);
            const int label_x = icon_x + icon_w + icon_gap;
            const int label_y =
                rawdraw_layout_ink_centered_text_top_y_in_box(font, mgr->quick_items[i]->name, y, item_h, 0);
            rawdraw_draw_text(fb, width, height, label_x, label_y, mgr->quick_items[i]->name, font, text_color);
        } else {
            rawdraw_draw_text(
                fb, width, height, text_start_x,
                rawdraw_layout_ink_centered_text_top_y_in_box(font, mgr->quick_items[i]->name, y, item_h, 0),
                mgr->quick_items[i]->name, font, text_color);
        }
        y += item_h + item_gap;
    }

    if (mgr->quick_count > QUICK_SWITCH_VISIBLE_COUNT) {
        const int sb_w       = 4;
        const int sb_x       = overlay_x + overlay_w - 14;
        const int sb_top     = overlay_y + titlebar_h + 10;
        const int sb_bottom  = overlay_y + overlay_h - 28;
        const int sb_track_h = sb_bottom - sb_top;
        rawdraw_draw_styled_rect(fb, width, height, (rawdraw_rect_t){sb_x, sb_top, sb_w, sb_track_h}, &modal_style);
        rawdraw_draw_rect_border(fb, width, height, (rawdraw_rect_t){sb_x, sb_top, sb_w, sb_track_h}, 1,
                                 border_style.border);
        const int thumb_h = (8 > sb_track_h * QUICK_SWITCH_VISIBLE_COUNT / mgr->quick_count)
                                ? 8
                                : (sb_track_h * QUICK_SWITCH_VISIBLE_COUNT / mgr->quick_count);
        const int max_scroll =
            (1 > mgr->quick_count - QUICK_SWITCH_VISIBLE_COUNT) ? 1 : (mgr->quick_count - QUICK_SWITCH_VISIBLE_COUNT);
        const int thumb_y = sb_top + (sb_track_h - thumb_h) * mgr->quick_switch_first_visible / max_scroll;
        rawdraw_fill_rect(fb, width, height, (rawdraw_rect_t){sb_x, thumb_y, sb_w, thumb_h}, selected_style.border);
    }

    rawdraw_draw_hline(fb, width, height, overlay_y + overlay_h - 24, overlay_x + 14, overlay_x + overlay_w - 14,
                       border_style.border);
    rawdraw_draw_styled_text(
        fb, width, height, overlay_x + 18,
        rawdraw_layout_ink_centered_text_top_y_in_box(font, "UP/DN 选择  BOOT 进入", overlay_y + overlay_h - 24, 24, 0),
        "UP/DN 选择  BOOT 进入", font, &text_style);
}

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
        ui_manager_switch_page(mgr, mgr->quick_items[mgr->quick_switch_index]->id);
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

/* ------------------------------------------------------------------ */
/* Init / deinit                                                       */
/* ------------------------------------------------------------------ */

static void image_received_cb(const char *photo_id, void *ctx)
{
    ui_manager_t *mgr = (ui_manager_t *)ctx;
    if (!mgr)
        return;
    if (mgr->app_image_received_cb) {
        mgr->app_image_received_cb(photo_id, mgr->app_cb_ctx);
    } else {
        photo_gallery_refresh_photo_list((page_renderer_t *)page_registry_get_instance(UI_PAGE_GALLERY));
        int count = photo_gallery_get_photo_count((page_renderer_t *)page_registry_get_instance(UI_PAGE_GALLERY));
        if (count > 0 && photo_id) {
            photo_gallery_set_selected_by_id((page_renderer_t *)page_registry_get_instance(UI_PAGE_GALLERY), photo_id);
        }
        ui_manager_request_active_page_refresh(mgr);
    }
}

#define APP_SETTINGS_SLIDESHOW_INDEX 3

static void settings_changed_cb(int minutes, void *ctx)
{
    ui_manager_t *mgr = (ui_manager_t *)ctx;
    if (!mgr)
        return;
    if (mgr->app_settings_changed_cb) {
        mgr->app_settings_changed_cb(minutes, mgr->app_cb_ctx);
    } else {
        ui_manager_set_gallery_slideshow_interval_minutes(mgr, minutes);
        char label[16];
        if (minutes <= 0) {
            snprintf(label, sizeof(label), "关闭");
        } else {
            snprintf(label, sizeof(label), "%dmin", minutes);
        }
        ui_manager_update_settings_item(mgr, APP_SETTINGS_SLIDESHOW_INDEX, label);
        ui_manager_request_active_page_refresh(mgr);
    }
}

static void photos_changed_cb(void *ctx)
{
    ui_manager_t *mgr = (ui_manager_t *)ctx;
    if (!mgr)
        return;
    if (mgr->app_photos_changed_cb) {
        mgr->app_photos_changed_cb(mgr->app_cb_ctx);
    } else {
        photo_gallery_refresh_photo_list((page_renderer_t *)page_registry_get_instance(UI_PAGE_GALLERY));
    }
}

static bool show_photo_cb(const char *photo_id, void *ctx)
{
    ui_manager_t *mgr = (ui_manager_t *)ctx;
    if (!mgr)
        return false;
    if (mgr->app_show_photo_cb) {
        return mgr->app_show_photo_cb(photo_id, mgr->app_cb_ctx);
    } else {
        return ui_manager_show_photo_by_id(mgr, photo_id);
    }
}

static void   ap_server_state_cb(int state, const char *message, void *ctx);
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
    mgr->refresh_cb  = cb;
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
    mgr->quick_count                        = page_registry_quick_switch_items(mgr->quick_items, UI_PAGE_COUNT);
    mgr->width                              = STYLE_SCREEN_WIDTH;
    mgr->height                             = STYLE_SCREEN_HEIGHT;
    mgr->current_page                       = UI_PAGE_GALLERY;
    /* P0: Restore last-viewed page from RTC memory on deep-sleep wake. */
    if (s_rtc_page_magic == RTC_PAGE_MAGIC && s_rtc_last_page < (uint32_t)UI_PAGE_COUNT) {
        mgr->current_page = (ui_page_id_t)s_rtc_last_page;
    }
    mgr->refresh_cb                         = refresh_cb;
    mgr->refresh_ctx                        = user_data;
    mgr->quick_switch_open                  = false;
    mgr->quick_switch_index                 = 0;
    mgr->last_clock_minute_key              = current_local_minute_key();
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
    mgr->status_bar.wifi_connected   = false;
    mgr->status_bar.server_connected = false;
    mgr->status_bar.battery_level    = -1;
    mgr->status_bar.battery_charging = false;
    mgr->status_bar.battery_vertical = false;
    mgr->status_bar.date_format[0]   = '\0';

    /* AP transfer server wiring. */
    ap_transfer_server_init(&mgr->ap_server);
    ap_transfer_server_set_state_callback(&mgr->ap_server, ap_server_state_cb, mgr);
    ap_transfer_server_set_image_received_callback(&mgr->ap_server, image_received_cb, mgr);
    ap_transfer_server_set_settings_changed_callback(&mgr->ap_server, settings_changed_cb, mgr);
    ap_transfer_server_set_photos_changed_callback(&mgr->ap_server, photos_changed_cb, mgr);
    ap_transfer_server_set_show_photo_callback(&mgr->ap_server, show_photo_cb, mgr);
    set_on_refresh_idle(on_display_refresh_idle_cb, mgr);

    ESP_LOGI(TAG, "RawDraw UI Manager initialized: %dx%d, page=%s", mgr->width, mgr->height,
             ui_manager_get_page_title(mgr->current_page));
}

void ui_manager_set_app_callbacks(ui_manager_t *mgr,
                                  void (*image_received)(const char *, void *),
                                  void (*settings_changed)(int, void *),
                                  void (*photos_changed)(void *),
                                  bool (*show_photo)(const char *, void *),
                                  void *ctx)
{
    if (!mgr)
        return;
    mgr->app_image_received_cb   = image_received;
    mgr->app_settings_changed_cb = settings_changed;
    mgr->app_photos_changed_cb   = photos_changed;
    mgr->app_show_photo_cb       = show_photo;
    mgr->app_cb_ctx              = ctx;
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
    strncpy(mgr->status_bar.page_title, ui_manager_get_page_title(page), sizeof(mgr->status_bar.page_title) - 1);
}

ui_page_id_t ui_manager_get_current_page(const ui_manager_t *mgr)
{
    return mgr ? mgr->current_page : UI_PAGE_CHAT;
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
        draw_status_bar(mgr, fb, width, height);
        draw_global_page_frame(fb, width, height);
    }

    /* Voice wakeup overlay. */
    if (widget_voice_wakeup_is_visible(&mgr->voice_wakeup)) {
        widget_voice_wakeup_render(&mgr->voice_wakeup, fb, width, height);
    }

    if (mgr->quick_switch_open) {
        draw_quick_switch_overlay(mgr, fb, width, height);
    }
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

    if (event->type == BTN_BOOT_LONG_PRESS) {
        if (mgr->current_page == UI_PAGE_AP_TRANSFER || ui_manager_is_ap_transfer_mode_running(mgr)) {
            ESP_LOGI(TAG, "BOOT long press - exiting AP transfer mode");
            ui_manager_stop_ap_transfer_mode(mgr);
            return true;
        }
        if (mgr->current_page == UI_PAGE_GALLERY) {
            /* Toggle LAN HTTP server (uses existing WiFi, no AP hotspot). */
            if (ui_manager_is_lan_http_server_running(mgr)) {
                ESP_LOGI(TAG, "Gallery long press BOOT - stopping LAN HTTP server");
                ap_transfer_server_stop(&mgr->ap_server);
            } else if (mgr->status_bar.wifi_connected) {
                ESP_LOGI(TAG, "Gallery long press BOOT - starting LAN HTTP server");
                char ip[32] = {0};
                wifi_manager_get_ip(ip, sizeof(ip));
                ap_transfer_server_start_lan(&mgr->ap_server, ip);
            } else {
                ESP_LOGW(TAG, "Gallery long press BOOT - WiFi not connected, cannot start LAN server");
            }
            if (mgr->refresh_cb) {
                mgr->refresh_cb((rawdraw_rect_t){0, 0, mgr->width, mgr->height}, false, mgr->refresh_ctx);
            }
            return true;
        }
        if (mgr->current_page == UI_PAGE_WIFI) {
            ESP_LOGI(TAG, "WiFi page long press BOOT - entering AP transfer mode");
            ui_manager_start_ap_transfer_mode(mgr);
            return true;
        }
    }

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
/* AP transfer mode                                                    */
/* ------------------------------------------------------------------ */

bool ui_manager_is_ap_transfer_running(const ui_manager_t *mgr)
{
    return mgr && ap_transfer_server_is_running((ap_transfer_server_t *)&mgr->ap_server);
}

bool ui_manager_is_ap_transfer_mode_running(const ui_manager_t *mgr)
{
    return mgr && ap_transfer_server_is_running((ap_transfer_server_t *)&mgr->ap_server) &&
           ap_transfer_server_is_ap_mode((ap_transfer_server_t *)&mgr->ap_server);
}

bool ui_manager_is_lan_http_server_running(const ui_manager_t *mgr)
{
    return mgr && ap_transfer_server_is_running((ap_transfer_server_t *)&mgr->ap_server) &&
           ap_transfer_server_is_lan_mode((ap_transfer_server_t *)&mgr->ap_server);
}

bool ui_manager_is_http_server_running(const ui_manager_t *mgr)
{
    return mgr && ap_transfer_server_is_running((ap_transfer_server_t *)&mgr->ap_server);
}

void ui_manager_start_ap_transfer_mode(ui_manager_t *mgr)
{
    if (!mgr)
        return;
    ap_transfer_page_use_default_instructions((page_renderer_t *)page_registry_get_instance(UI_PAGE_AP_TRANSFER));
    ap_transfer_server_start(&mgr->ap_server);
    ui_manager_switch_page(mgr, UI_PAGE_AP_TRANSFER);
}

void ui_manager_stop_ap_transfer_mode(ui_manager_t *mgr)
{
    if (!mgr)
        return;
    ap_transfer_server_stop(&mgr->ap_server);
    ui_manager_switch_page(mgr, UI_PAGE_GALLERY);
}

void ui_manager_show_wifi_config_page(ui_manager_t *mgr, const char *ssid, const char *password, const char *url)
{
    if (!mgr)
        return;
    ap_transfer_page_set_instruction_content((page_renderer_t *)page_registry_get_instance(UI_PAGE_AP_TRANSFER),
                                             "WiFi 传图", ssid, password, url, "", "长按 BOOT 退出");
    ui_manager_switch_page(mgr, UI_PAGE_AP_TRANSFER);
}

bool ui_manager_start_lan_http_server(ui_manager_t *mgr, const char *ip_address)
{
    if (!mgr)
        return false;
    return ap_transfer_server_start_lan(&mgr->ap_server, ip_address);
}

void ui_manager_stop_lan_http_server(ui_manager_t *mgr)
{
    if (!mgr)
        return;
    ap_transfer_server_stop(&mgr->ap_server);
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
    mgr->gallery_slideshow_pending          = false;
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
    bool page_pending                = mgr->active_page_refresh_pending;
    mgr->active_page_refresh_pending = false;
    bool transient_pending           = mgr->transient_refresh_pending;
    mgr->transient_refresh_pending   = false;

    if (page_pending || transient_pending) {
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
    mgr->page_switch_cb  = cb;
    mgr->page_switch_ctx = user_data;
}

static void ap_server_state_cb(int state, const char *message, void *ctx)
{
    (void)message;
    ui_manager_t *mgr = (ui_manager_t *)ctx;
    if (!mgr)
        return;
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
    if (should_refresh && mgr->current_page == UI_PAGE_AP_TRANSFER) {
        ui_manager_request_active_page_refresh(mgr);
    }
}
