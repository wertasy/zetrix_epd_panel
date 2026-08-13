/**
 * @file ui_status_bar.c
 * @brief Top status bar rendering — extracted from ui_manager.c (Phase 2.2).
 *
 * Contains: battery icon, server status marker, seven-segment mini clock,
 * and the full status bar layout/draw function.
 */
#include "ui_manager_internal.h"
#include "ui_text_util.h"
#include "fa_settings.h"
#include "theme.h"
#include "layout.h"

#include <string.h>
#include <stdio.h>

/* ============================================================ */
/* Battery icon                                                 */
/* ============================================================ */

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
        const int seg_h = 3;
        const int gap = 1;
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
        const int seg_w = 6;
        const int gap = 1;
        const int filled = level >= 90 ? 3 : (level >= 50 ? 2 : (level > 10 ? 1 : 0));
        for (int i = 0; i < filled; ++i) {
            const int sx = x + 2 + i * (seg_w + gap);
            rawdraw_fill_rect(fb, width, height, (rawdraw_rect_t){sx, y + 2, seg_w, body_h - 4}, battery_color);
        }
    }
}

/* ============================================================ */
/* Server status marker                                         */
/* ============================================================ */

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

/* ============================================================ */
/* Seven-segment mini clock                                     */
/* ============================================================ */

static void draw_mini_time_digit(uint8_t *fb, int width, int height, int x, int y, char digit, rawdraw_color_t color)
{
    if (digit < '0' || digit > '9')
        return;
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
    return 32;
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

/* ============================================================ */
/* Full status bar                                              */
/* ============================================================ */

void ui_status_bar_draw(struct ui_manager *mgr, uint8_t *fb, int width, int height)
{
    int bar_height = STYLE_STATUS_BAR_HEIGHT;
    int padding = STYLE_STATUS_BAR_PADDING;
    const lv_font_t *title_font = &SourceHanSansSC_Regular_slim;
    const rawdraw_paint_style_t bg_style = rawdraw_theme_style(THEME_TOKEN_BACKGROUND_PRIMARY);
    const rawdraw_paint_style_t text_style = rawdraw_theme_style(THEME_TOKEN_TEXT_PRIMARY);
    const rawdraw_paint_style_t accent_style = rawdraw_theme_style(THEME_TOKEN_ACCENT);
    const rawdraw_paint_style_t border_style = rawdraw_theme_style(THEME_TOKEN_BORDER);
    const rawdraw_paint_style_t danger_style = rawdraw_theme_style(THEME_TOKEN_DANGER);

    rawdraw_draw_styled_rect(fb, width, height, (rawdraw_rect_t){0, 0, width, bar_height}, &bg_style);
    rawdraw_fill_rect(
        fb, width, height,
        (rawdraw_rect_t){1, bar_height - STYLE_SHELL_DIVIDER_THICKNESS, width - 2, STYLE_SHELL_DIVIDER_THICKNESS},
        border_style.border);

    const int center_y = bar_height / 2;
    const int sig_bar_w = 3;
    const int sig_bar_gap = 2;
    const int sig_bar_heights[] = {6, 9, 12, 15};
    const int wifi_group_w = 4 * sig_bar_w + 3 * sig_bar_gap;
    int x = padding + 3;

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
        const int bt_y = rawdraw_layout_ink_centered_text_top_y(&fa_settings_16, bt_icon, center_y, 0);
        rawdraw_draw_styled_text(fb, width, height, left_content_x, bt_y, bt_icon, &fa_settings_16, &accent_style);
        left_content_x += rawdraw_measure_text_width(bt_icon, &fa_settings_16) + 8;
    }

    char date_str[32] = {0};
    const bool hide_date = strcmp(mgr->status_bar.date_format, "hidden") == 0;
    if (!hide_date) {
        if (mgr->status_bar.server_date[0] != '\0') {
            if (strcmp(mgr->status_bar.date_format, "iso") == 0) {
                strncpy(date_str, mgr->status_bar.server_date, sizeof(date_str) - 1);
            } else {
                int y = 0, m = 0, d = 0;
                if (sscanf(mgr->status_bar.server_date, "%d-%d-%d", &y, &m, &d) == 3) {
                    snprintf(date_str, sizeof(date_str), "%d月%d日", m, d);
                } else {
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
    int right_x = width - padding;
    if (mgr->status_bar.battery_level >= 0) {
        const int battery_h = mgr->status_bar.battery_vertical ? 16 : 12;
        const int battery_w = mgr->status_bar.battery_vertical ? 9 : 26;
        const int battery_x = width - padding - battery_slot_w + (battery_slot_w - battery_w);
        const int battery_y = center_y - battery_h / 2;
        draw_battery_icon(fb, width, height, battery_x, battery_y, mgr->status_bar.battery_level,
                          mgr->status_bar.battery_vertical);
        right_x = battery_x - 8;
    }

    const int clock_w = mini_time_width(time_str);
    draw_mini_time_text(fb, width, height, right_x - clock_w, center_y - 5, time_str, accent_style.fg);
    right_x = right_x - clock_w - 6;

    const char *title =
        mgr->status_bar.central_text[0] != '\0' ? mgr->status_bar.central_text : mgr->status_bar.page_title;
    const int right_safe = (left_safe + 40 > right_x - 2) ? left_safe + 40 : right_x - 2;
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
    int title_w = title_icon_w + title_icon_gap + title_text_w;
    int title_x = (width - title_w) / 2;
    if (title_x < left_safe)
        title_x = left_safe;
    if (title_x + title_w > right_safe)
        title_x = (left_safe > right_safe - title_w) ? left_safe : (right_safe - title_w);
    const int title_y = rawdraw_layout_ink_centered_text_top_y_in_box(title_font, display_title, 0, bar_height, 0);
    int title_text_x = title_x;
    if (title_icon_w > 0) {
        const int icon_y = rawdraw_layout_ink_centered_text_top_y_in_box(&fa_settings_16, title_icon, 0, bar_height, 0);
        rawdraw_draw_styled_text(fb, width, height, title_x, icon_y, title_icon, &fa_settings_16, &text_style);
        title_text_x += title_icon_w + title_icon_gap;
    }
    rawdraw_draw_styled_text(fb, width, height, title_text_x, title_y, display_title, title_font, &text_style);
}
