/**
 * @file settings_page.c
 * @brief Settings page renderer core — C port of C++ rawdraw::SettingsRenderer.
 *
 * Card-based settings page: left category sidebar (SECTION items), right
 * option pane (NORMAL / CHECKBOX / ACTION rows). Modal dialogs (about,
 * volume, storage, server, server list, theme, OTA) are rendered by the
 * sibling translation units settings_about.c / settings_dialogs.c /
 * settings_themes.c and dispatched from Render() below.
 */
#include "settings_page.h"
#include "page_registry.h"

#include "rawdraw_ext.h"
#include "style.h"
#include "layout.h"
#include "ui_text_util.h"
#include "fa_settings.h"
#include "font_engine.h"

#include <esp_timer.h>

#include <stdio.h>
#include <string.h>

#ifndef PROJECT_VER
#    define PROJECT_VER "3.8.0"
#endif

/* ------------------------------------------------------------------ */
/* Layout constants (single source of truth, from the C++ renderer).   */
/* ------------------------------------------------------------------ */

#define kTextOpticalNudgeY 0
#define kIconOpticalNudgeY 0
#define kValueOpticalNudgeY 0
#define kVolumeDialogClearPad 0
#define kAboutRowHeight 24
#define kDialogClearPad 0
#define kDialogClearRadiusBoost 0
#define kCategoryHintDurationUs (2 * 1000 * 1000)
#define kSettingsNavDividerX 90
#define kSettingsNavItemH 44
#define kSettingsTableRowH 34
#define kSettingsContentTopGap 8
#define kSettingsTableTop (STYLE_STATUS_BAR_HEIGHT + kSettingsContentTopGap)
#define kVisibleOptionCount 8
#define kServerListVisibleRows 5
#define kOtaVisibleRows 4

static const lv_font_t *const kSettingsFont = &SourceHanSansSC_Regular_slim;
static const lv_font_t *const kSettingsTitleFont = &SourceHanSansSC_Medium_slim;
static const lv_font_t *const kSettingsIconFont = &fa_settings_16;
static const lv_font_t *const kSettingsValueFont = &SourceHanSansSC_Regular_slim;

/* ------------------------------------------------------------------ */
/* Shared helpers (exported for the dialog TUs).                       */
/* ------------------------------------------------------------------ */

rawdraw_color_t settings_page_token_ink_on_paper(rawdraw_theme_token_t token)
{
    const rawdraw_paint_style_t style = rawdraw_theme_style(token);
    /* Accent/Danger/Progress tokens are full paint styles: fg is intended
     * for text drawn on their colored bg. On white paper the visible
     * semantic ink is the colored bg/border instead. */
    if (style.bg != RAWDRAW_COLOR_WHITE)
        return style.bg;
    if (style.border != RAWDRAW_COLOR_WHITE)
        return style.border;
    return style.fg;
}

static const char *settings_icon_for_label(const char *label)
{
    if (!label)
        return FA_SETTINGS_INFO;
    if (strcmp(label, "系统") == 0)
        return FA_SETTINGS_GEAR;
    if (strcmp(label, "网络") == 0 || strcmp(label, "Wi-Fi") == 0)
        return FA_SETTINGS_WIFI;
    if (strcmp(label, "功能") == 0 || strcmp(label, "语音唤醒") == 0)
        return FA_SETTINGS_WRENCH;
    if (strcmp(label, "时钟显示") == 0)
        return FA_SETTINGS_CLOCK;
    if (strcmp(label, "存储") == 0 || strcmp(label, "存储空间") == 0)
        return FA_SETTINGS_DATABASE;
    if (strcmp(label, "关于") == 0)
        return FA_SETTINGS_INFO;
    if (strcmp(label, "音量") == 0)
        return FA_SETTINGS_VOLUME;
    if (strcmp(label, "电池方向") == 0)
        return FA_SETTINGS_BATTERY;
    if (strcmp(label, "重启") == 0 || strcmp(label, "同步间隔") == 0 || strcmp(label, "同步记录") == 0)
        return FA_SETTINGS_SYNC;
    if (strcmp(label, "关机") == 0)
        return FA_SETTINGS_POWER;
    if (strcmp(label, "日期格式") == 0)
        return FA_SETTINGS_CALENDAR;
    if (strcmp(label, "AI对话长度") == 0)
        return FA_SETTINGS_COMMENT;
    if (strcmp(label, "服务") == 0)
        return FA_SETTINGS_SERVER;
    if (strcmp(label, "服务地址") == 0)
        return FA_SETTINGS_MAP_MARKER;
    if (strcmp(label, "蓝牙") == 0)
        return FA_SETTINGS_BLUETOOTH;
    return FA_SETTINGS_INFO; /* fallback */
}

void settings_page_draw_vector_icon(uint8_t *fb, int width, int height, const char *label, int x, int center_y,
                                    rawdraw_color_t color)
{
    const char *icon_char = settings_icon_for_label(label);
    /* Ink-box centering (baseline math) keeps icon glyphs optically aligned. */
    const int top_y = rawdraw_layout_ink_centered_text_top_y(&fa_settings_16, icon_char, center_y, 0);
    rawdraw_draw_text(fb, width, height, x, top_y, icon_char, &fa_settings_16, color);
}

void settings_page_clear_dialog_region(uint8_t *fb, int width, int height, int x, int y, int w, int h, int radius,
                                       int pad)
{
    const rawdraw_paint_style_t bg = rawdraw_theme_style(THEME_TOKEN_BACKGROUND_PRIMARY);
    int r = radius + kDialogClearRadiusBoost;
    if (r < 0)
        r = 0;
    rawdraw_draw_styled_round_rect(fb, width, height, (rawdraw_rect_t){x - pad, y - pad, w + pad * 2, h + pad * 2}, r,
                                   &bg);
}

/* ------------------------------------------------------------------ */
/* Selection / scrolling helpers                                       */
/* ------------------------------------------------------------------ */

static int get_first_selectable_index(const settings_page_t *r)
{
    for (int i = 0; i < r->item_count; ++i) {
        if (r->items[i].type != SETTINGS_ITEM_SECTION)
            return i;
    }
    return 0;
}

static int get_last_selectable_index(const settings_page_t *r)
{
    for (int i = r->item_count - 1; i >= 0; --i) {
        if (r->items[i].type != SETTINGS_ITEM_SECTION)
            return i;
    }
    return 0;
}

static int find_prev_selectable(const settings_page_t *r, int index)
{
    for (int i = index - 1; i >= 0; --i) {
        if (r->items[i].type != SETTINGS_ITEM_SECTION)
            return i;
    }
    return index;
}

static int find_next_selectable(const settings_page_t *r, int index)
{
    for (int i = index + 1; i < r->item_count; ++i) {
        if (r->items[i].type != SETTINGS_ITEM_SECTION)
            return i;
    }
    return index;
}

/* Count non-section items from first_visible_index (up to kVisibleOptionCount)
 * and report whether the selection lies inside that window. */
static int count_visible_from(const settings_page_t *r, int start, bool *selection_visible)
{
    int visible_count = 0;
    *selection_visible = false;
    for (int i = start; i < r->item_count; ++i) {
        if (r->items[i].type == SETTINGS_ITEM_SECTION)
            continue;
        if (i == r->selected_index)
            *selection_visible = true;
        visible_count++;
        if (visible_count >= kVisibleOptionCount)
            break;
    }
    return visible_count;
}

static void ensure_selection_visible(settings_page_t *r)
{
    if (r->item_count == 0) {
        r->first_visible_index = 0;
        r->scroll_offset = 0;
        return;
    }

    if (r->items[r->selected_index].type == SETTINGS_ITEM_SECTION) {
        r->selected_index = get_first_selectable_index(r);
    }

    if (r->first_visible_index < 0 || r->first_visible_index >= r->item_count ||
        r->items[r->first_visible_index].type == SETTINGS_ITEM_SECTION) {
        r->first_visible_index = r->selected_index;
    }

    bool selection_visible = false;
    count_visible_from(r, r->first_visible_index, &selection_visible);

    while (!selection_visible) {
        if (r->selected_index < r->first_visible_index) {
            r->first_visible_index = r->selected_index;
        } else {
            int next = find_next_selectable(r, r->first_visible_index);
            if (next == r->first_visible_index)
                break;
            r->first_visible_index = next;
        }
        count_visible_from(r, r->first_visible_index, &selection_visible);
    }

    r->scroll_offset = 0;
}

/* ------------------------------------------------------------------ */
/* Item row rendering                                                  */
/* ------------------------------------------------------------------ */

static void render_item(settings_page_t *r, uint8_t *fb, int width, int height, int y, int content_left, int index,
                        bool selected, int row_h)
{
    const settings_page_item_t *item = &r->items[index];
    const int content_right = width - 20;
    const rawdraw_paint_style_t text_style = rawdraw_theme_style(THEME_TOKEN_TEXT_PRIMARY);
    const rawdraw_paint_style_t selected_style = rawdraw_theme_component(ROLE_SETTINGS_SELECTED);
    const rawdraw_color_t action_color =
        settings_page_token_ink_on_paper(strcmp(item->label, "关机") == 0 ? THEME_TOKEN_DANGER : THEME_TOKEN_ACCENT);
    const rawdraw_color_t fg_color = text_style.fg;
    const int row_center_y = y + row_h / 2;
    const int icon_x = content_left;
    const int label_x = icon_x + 16 + STYLE_SPACING_SM; /* 16 is icon width */
    const int label_y = rawdraw_layout_ink_centered_text_top_y(r->font, item->label, row_center_y, kTextOpticalNudgeY);

    if (selected) {
        /* Compact focus rail as solid ink (1bpp-friendly cursor). */
        rawdraw_fill_rect(fb, width, height, (rawdraw_rect_t){content_left - 8, row_center_y - 8, 3, 16},
                          selected_style.border);
    }

    settings_page_draw_vector_icon(fb, width, height, item->label, icon_x, row_center_y, fg_color);

    int label_right = content_right;

    if (item->type == SETTINGS_ITEM_CHECKBOX) {
        const int track_w = 52;
        const int track_h = 20;
        const int knob = 16;
        const int track_x = content_right - track_w;
        const int track_y = row_center_y - track_h / 2;
        label_right = track_x - STYLE_SPACING_LG;
        const rawdraw_paint_style_t switch_style =
            item->checked ? rawdraw_theme_style(THEME_TOKEN_ACCENT) : rawdraw_theme_style(THEME_TOKEN_DISABLED);
        rawdraw_draw_styled_round_rect(fb, width, height, (rawdraw_rect_t){track_x, track_y, track_w, track_h},
                                       STYLE_BORDER_RADIUS_PILL, &switch_style);
        const char *switch_text = item->checked ? "ON" : "OFF";
        const int text_w = rawdraw_measure_text_width(switch_text, r->value_font);
        const int text_x = item->checked ? (track_x + 7) : (track_x + track_w - text_w - 6);
        rawdraw_draw_text(
            fb, width, height, text_x,
            rawdraw_layout_ink_centered_text_top_y(r->value_font, switch_text, row_center_y, kValueOpticalNudgeY),
            switch_text, r->value_font, switch_style.fg);
        /* True circle knob: fill with paper, then outline. */
        const int knob_x = item->checked ? (track_x + track_w - knob - 2) : (track_x + 2);
        const int knob_cx = knob_x + knob / 2;
        const rawdraw_color_t paper = rawdraw_theme_style(THEME_TOKEN_BACKGROUND_PRIMARY).bg;
        rawdraw_draw_circle(fb, width, height, (rawdraw_point_t){knob_cx, row_center_y - 1}, knob / 2, paper);
        rawdraw_draw_circle_border(fb, width, height, (rawdraw_point_t){knob_cx, row_center_y - 1}, knob / 2, 1,
                                   text_style.fg);
    } else if (item->value[0] != '\0') {
        const int value_right = content_right;
        const int max_val_w = RD_MAX(0, value_right - (content_left + 88));
        char display_value[SETTINGS_PAGE_ITEM_VALUE_LEN];
        ui_text_fit_to_width(item->value, r->value_font, max_val_w, display_value, sizeof(display_value));
        const int value_w = rawdraw_measure_text_width(display_value, r->value_font);
        const int val_x = value_right - value_w;
        label_right = val_x - STYLE_SPACING_LG;
        if (display_value[0] != '\0') {
            rawdraw_draw_text(
                fb, width, height, val_x,
                rawdraw_layout_ink_centered_text_top_y(r->value_font, display_value, row_center_y, kValueOpticalNudgeY),
                display_value, r->value_font, text_style.fg);
        }
    } else if (item->type == SETTINGS_ITEM_ACTION) {
        const char *action_text = item->value[0] != '\0' ? item->value : "执行";
        const int action_w = rawdraw_measure_text_width(action_text, r->value_font);
        const int act_x = content_right - action_w;
        label_right = act_x - STYLE_SPACING_LG;
        rawdraw_draw_text(
            fb, width, height, act_x,
            rawdraw_layout_ink_centered_text_top_y(r->value_font, action_text, row_center_y, kValueOpticalNudgeY),
            action_text, r->value_font, action_color);
    }

    const int label_max_w = RD_MAX(0, label_right - label_x);
    char display_label[SETTINGS_PAGE_ITEM_LABEL_LEN];
    ui_text_fit_to_width(item->label, r->font, label_max_w, display_label, sizeof(display_label));
    if (display_label[0] != '\0') {
        rawdraw_draw_text(fb, width, height, label_x, label_y, display_label, r->font, fg_color);
    }
    const rawdraw_paint_style_t border_style = rawdraw_theme_style(THEME_TOKEN_BORDER);
    rawdraw_draw_hline(fb, width, height, y + row_h - 1, content_left, content_right, border_style.border);
}

/* ------------------------------------------------------------------ */
/* PageRenderer vtable                                                 */
/* ------------------------------------------------------------------ */

void settings_page_init(page_renderer_t *self, int width, int height)
{
    settings_page_t *r = (settings_page_t *)self;
    r->base.width = width;
    r->base.height = height;
    r->base.needs_full_refresh_flag = true;
    /* Preserve item_count and selected_index across page switches — items
     * are populated once during application_init via set_settings_items
     * and would be lost on re-init. */
    if (r->item_count == 0) {
        r->selected_index = 0;
    }
    r->scroll_offset = 0;
    r->first_visible_index = 0;
    r->showing_debug_info = false;
    r->debug_hint_until_us = 0;
    r->font = kSettingsFont;
    r->title_font = kSettingsTitleFont;
    r->icon_font = kSettingsIconFont;
    r->value_font = kSettingsValueFont;
    settings_page_show_category_hint(self, 0);
    if (r->firmware_version[0] == '\0') {
        snprintf(r->firmware_version, sizeof(r->firmware_version), "v%s", PROJECT_VER);
    }
}

void settings_page_show_category_hint(page_renderer_t *self, int duration_ms)
{
    settings_page_t *r = (settings_page_t *)self;
    const int64_t duration_us = duration_ms > 0 ? (int64_t)duration_ms * 1000 : kCategoryHintDurationUs;
    r->category_hint_until_us = esp_timer_get_time() + duration_us;
    r->base.needs_full_refresh_flag = true;
}

bool settings_page_is_category_hint_visible(const page_renderer_t *self)
{
    const settings_page_t *r = (const settings_page_t *)self;
    return esp_timer_get_time() < r->category_hint_until_us;
}

void settings_page_render(page_renderer_t *self, uint8_t *fb, int width, int height)
{
    settings_page_t *r = (settings_page_t *)self;
    if (!fb)
        return;

    ensure_selection_visible(r);

    const rawdraw_paint_style_t bg_style = rawdraw_theme_style(THEME_TOKEN_BACKGROUND_PRIMARY);
    const rawdraw_paint_style_t text_style = rawdraw_theme_style(THEME_TOKEN_TEXT_PRIMARY);
    const rawdraw_paint_style_t border_style = rawdraw_theme_style(THEME_TOKEN_BORDER);
    const rawdraw_paint_style_t selected_style = rawdraw_theme_component(ROLE_SETTINGS_SELECTED);
    const int body_top = STYLE_STATUS_BAR_HEIGHT;
    const int body_bottom = height - 3;
    const int content_x = kSettingsNavDividerX + 16;
    const int content_right = width - 20;
    const int row_h = kSettingsTableRowH;

    rawdraw_draw_styled_rect(fb, width, height, (rawdraw_rect_t){0, body_top, width, body_bottom - body_top},
                             &bg_style);
    rawdraw_draw_vline(fb, width, height, kSettingsNavDividerX, body_top + kSettingsContentTopGap, body_bottom - 1,
                       border_style.border);

    /* Sidebar: one pill per SECTION item. */
    int section_indices[SETTINGS_PAGE_MAX_ITEMS];
    int section_count = 0;
    for (int i = 0; i < r->item_count; ++i) {
        if (r->items[i].type == SETTINGS_ITEM_SECTION) {
            section_indices[section_count++] = i;
        }
    }
    if (section_count == 0)
        section_indices[section_count++] = -1;

    int current_section_pos = 0;
    for (int i = 0; i < section_count; ++i) {
        if (section_indices[i] <= r->selected_index)
            current_section_pos = i;
    }

    const char *current_section =
        (section_indices[current_section_pos] >= 0) ? r->items[section_indices[current_section_pos]].label : "系统";

    const int nav_top = body_top + kSettingsContentTopGap;
    for (int i = 0; i < section_count; ++i) {
        const int sy = nav_top + i * kSettingsNavItemH;
        const bool selected = (i == current_section_pos);
        const char *label = (section_indices[i] >= 0) ? r->items[section_indices[i]].label : "系统";
        const int nav_pill_x = 16;
        const int nav_pill_w = kSettingsNavDividerX - 26;
        const int nav_pill_h = 28;
        const int nav_pill_y = sy + (kSettingsNavItemH - nav_pill_h) / 2;
        const int icon_x = nav_pill_x + 7;
        const int icon_center_y = sy + kSettingsNavItemH / 2;
        const int label_x = nav_pill_x + 27;
        const int label_y = rawdraw_layout_ink_centered_text_top_y(r->font, label, icon_center_y, kTextOpticalNudgeY);
        if (selected) {
            rawdraw_draw_styled_round_rect(fb, width, height,
                                           (rawdraw_rect_t){nav_pill_x, nav_pill_y, nav_pill_w, nav_pill_h},
                                           STYLE_BORDER_RADIUS_MD, &selected_style);
        }
        const rawdraw_color_t nav_fg = selected ? selected_style.fg : text_style.fg;
        settings_page_draw_vector_icon(fb, width, height, label, icon_x, icon_center_y, nav_fg);
        rawdraw_draw_text(fb, width, height, RD_MAX(2, label_x), RD_MAX(body_top + 2, label_y), label, r->font, nav_fg);
    }

    /* Option rows of the current section. */
    const int section_start = section_indices[current_section_pos] + 1;
    const int section_end =
        (current_section_pos + 1 < section_count) ? section_indices[current_section_pos + 1] : r->item_count;

    int option_indices[SETTINGS_PAGE_MAX_ITEMS];
    int option_count = 0;
    for (int i = section_start; i < section_end; ++i) {
        if (r->items[i].type != SETTINGS_ITEM_SECTION) {
            option_indices[option_count++] = i;
        }
    }

    const bool about_section = (strcmp(current_section, "关于") == 0);

    if (about_section) {
        /* Static info rows rendered instead of option rows. */
        struct info_row_t {
            const char *label;
            const char *value;
        };
        char version_buf[SETTINGS_PAGE_VERSION_LEN];
        char serial_buf[SETTINGS_PAGE_VERSION_LEN];
        snprintf(version_buf, sizeof(version_buf), "%s", r->firmware_version[0] != '\0' ? r->firmware_version : "未知");
        snprintf(serial_buf, sizeof(serial_buf), "%s", r->mac_address[0] != '\0' ? r->mac_address : "未读取");
        const struct info_row_t rows[] = {
            {"设备名称", "notellm"},   {"型号", "Youn-Beta1.0"},
            {"固件版本", version_buf}, {"硬件版本", r->chip_model[0] != '\0' ? r->chip_model : "ESP32-S3"},
            {"序列号", serial_buf},    {"官方网站", "blog.lazyyoun.xyz"},
        };
        const int row_count = (int)(sizeof(rows) / sizeof(rows[0]));
        int y = kSettingsTableTop;
        for (int i = 0; i < row_count; ++i) {
            const int center_y = y + row_h / 2;
            const int label_x = content_x;
            rawdraw_draw_styled_text(
                fb, width, height, label_x,
                rawdraw_layout_ink_centered_text_top_y(r->font, rows[i].label, center_y, kTextOpticalNudgeY),
                rows[i].label, r->font, &text_style);
            char display_value[SETTINGS_PAGE_ITEM_VALUE_LEN];
            ui_text_fit_to_width(rows[i].value, r->value_font, RD_MAX(0, content_right - (label_x + 112)),
                                 display_value, sizeof(display_value));
            const int value_w = rawdraw_measure_text_width(display_value, r->value_font);
            rawdraw_draw_styled_text(
                fb, width, height, content_right - value_w,
                rawdraw_layout_ink_centered_text_top_y(r->value_font, display_value, center_y, kValueOpticalNudgeY),
                display_value, r->value_font, &text_style);
            y += row_h;
        }
    } else {
        int selected_pos = 0;
        for (int i = 0; i < option_count; ++i) {
            if (option_indices[i] == r->selected_index)
                selected_pos = i;
        }
        const int visible_count = RD_MIN(kVisibleOptionCount, option_count);
        int window_start = RD_MAX(0, selected_pos - visible_count / 2);
        if (window_start + visible_count > option_count) {
            window_start = RD_MAX(0, option_count - visible_count);
        }
        int y = kSettingsTableTop;
        const int available_h = RD_MAX(row_h, body_bottom - kSettingsTableTop - 2);
        const int option_row_h = RD_MIN(row_h, RD_MAX(28, available_h / RD_MAX(1, visible_count)));
        for (int i = 0; i < visible_count; ++i) {
            const int item_index = option_indices[window_start + i];
            render_item(r, fb, width, height, y, content_x, item_index, item_index == r->selected_index, option_row_h);
            y += option_row_h;
        }

        if (option_count > kVisibleOptionCount) {
            const int track_x = width - 10;
            const int track_y = kSettingsTableTop + 2;
            const int track_h = RD_MAX(24, option_row_h * visible_count - 4);
            rawdraw_draw_vline(fb, width, height, track_x, track_y, track_y + track_h, border_style.border);
            const int thumb_h = RD_MAX(10, track_h * visible_count / option_count);
            const int max_start = RD_MAX(1, option_count - visible_count);
            const int thumb_y = track_y + (track_h - thumb_h) * window_start / max_start;
            rawdraw_fill_rect(fb, width, height, (rawdraw_rect_t){track_x - 2, thumb_y, 4, thumb_h},
                              selected_style.border);
        }
    }

    /* === Dialog overlays (topmost last). === */
    if (r->showing_about_dialog) {
        settings_page_render_about_dialog(r, fb, width, height);
    }
    if (r->showing_volume_dialog) {
        settings_page_render_volume_dialog(r, fb, width, height);
    }
    if (r->showing_storage_dialog) {
        settings_page_render_storage_dialog(r, fb, width, height);
    }
    if (r->showing_server_dialog) {
        settings_page_render_server_dialog(r, fb, width, height);
    }
    if (r->showing_server_list_dialog) {
        settings_page_render_server_list_dialog(r, fb, width, height);
    }
    if (r->showing_theme_dialog) {
        settings_page_render_theme_dialog(r, fb, width, height);
    }
    if (r->showing_ota_dialog) {
        settings_page_render_ota_dialog(r, fb, width, height);
        if (r->showing_ota_confirm_dialog) {
            settings_page_render_ota_confirm_dialog(r, fb, width, height);
        }
    }

    /* === Debug info hint (transient badge, 3s auto-dismiss). === */
    const int64_t now = esp_timer_get_time();
    if (r->showing_debug_info && now < r->debug_hint_until_us) {
        const int hint_y = STYLE_STATUS_BAR_HEIGHT + 2;
        const int hint_h = r->font->line_height + STYLE_SPACING_XS * 2;
        const char *hint_text = "调试信息已显示";
        const int hint_w = rawdraw_measure_text_width(hint_text, r->font);
        const int hint_x = (width - hint_w) / 2;
        const rawdraw_paint_style_t hint_style = rawdraw_theme_style(THEME_TOKEN_BADGE);
        rawdraw_draw_styled_round_rect(
            fb, width, height,
            (rawdraw_rect_t){hint_x - STYLE_SPACING_SM, hint_y, hint_w + STYLE_SPACING_SM * 2, hint_h},
            STYLE_BORDER_RADIUS_SM, &hint_style);
        rawdraw_draw_text(fb, width, height, hint_x, hint_y + STYLE_SPACING_XS, hint_text, r->font, hint_style.fg);
    } else if (r->showing_debug_info) {
        r->showing_debug_info = false;
        r->base.needs_full_refresh_flag = true;
    }

    r->base.needs_full_refresh_flag = false;
}

/* ------------------------------------------------------------------ */
/* Input handling                                                      */
/* ------------------------------------------------------------------ */

static void update_volume_value(settings_page_t *r, int delta, bool commit)
{
    if (commit) {
        r->showing_volume_dialog = false;
    } else {
        r->volume_dialog_value = RD_CLAMP(r->volume_dialog_value + delta, 0, 100);
    }
    if (r->volume_dialog_handler) {
        r->volume_dialog_handler(r->volume_dialog_value, commit, r->volume_dialog_ctx);
    }
    r->base.needs_full_refresh_flag = true;
}

bool settings_page_handle_input(page_renderer_t *self, const ui_button_event_t *event)
{
    settings_page_t *r = (settings_page_t *)self;
    if (r->item_count == 0)
        return false;

    if (r->showing_volume_dialog) {
        switch (event->type) {
        case BTN_UP_CLICK:
            update_volume_value(r, 10, false);
            return true;
        case BTN_DOWN_CLICK:
            update_volume_value(r, -10, false);
            return true;
        case BTN_UP_LONG_PRESS:
            r->volume_dialog_value = 100;
            update_volume_value(r, 0, false);
            return true;
        case BTN_DOWN_LONG_PRESS:
            r->volume_dialog_value = 0;
            update_volume_value(r, 0, false);
            return true;
        case BTN_BOOT_CLICK:
            update_volume_value(r, 0, true);
            return true;
        default:
            return true;
        }
    }

    /* About dialog: intercept all input. */
    if (r->showing_about_dialog) {
        switch (event->type) {
        case BTN_BOOT_CLICK:
        case BTN_UP_CLICK:
        case BTN_UP_LONG_PRESS:
        case BTN_DOWN_CLICK:
        case BTN_DOWN_LONG_PRESS:
            r->showing_about_dialog = false;
            r->base.needs_full_refresh_flag = true;
            return true;
        default:
            return true; /* consume everything while open */
        }
    }

    /* Storage dialog: intercept all input. */
    if (r->showing_storage_dialog) {
        switch (event->type) {
        case BTN_BOOT_CLICK:
        case BTN_UP_CLICK:
        case BTN_UP_LONG_PRESS:
        case BTN_DOWN_CLICK:
        case BTN_DOWN_LONG_PRESS:
            r->showing_storage_dialog = false;
            r->base.needs_full_refresh_flag = true;
            return true;
        default:
            return true;
        }
    }

    if (r->showing_theme_dialog) {
        const int total = settings_page_theme_count();
        switch (event->type) {
        case BTN_UP_CLICK:
            if (r->theme_selected > 0) {
                r->theme_selected--;
                r->base.needs_full_refresh_flag = true;
            }
            return true;
        case BTN_DOWN_CLICK:
            if (r->theme_selected < total - 1) {
                r->theme_selected++;
                r->base.needs_full_refresh_flag = true;
            }
            return true;
        case BTN_BOOT_CLICK:
            if (r->theme_dialog_handler) {
                r->theme_dialog_handler(rawdraw_theme_at(r->theme_selected), r->theme_dialog_ctx);
            }
            r->showing_theme_dialog = false;
            r->base.needs_full_refresh_flag = true;
            return true;
        case BTN_BOOT_LONG_PRESS:
            r->showing_theme_dialog = false;
            r->base.needs_full_refresh_flag = true;
            return true;
        default:
            return true;
        }
    }

    /* Server list dialog: UP/DN scroll, BOOT select. */
    if (r->showing_server_list_dialog) {
        const int total = r->server_list_count;
        if (total == 0) {
            r->showing_server_list_dialog = false;
            r->base.needs_full_refresh_flag = true;
            return true;
        }
        switch (event->type) {
        case BTN_UP_CLICK:
            if (r->server_list_selected > 0) {
                r->server_list_selected--;
                if (r->server_list_selected < r->server_list_scroll_offset) {
                    r->server_list_scroll_offset = r->server_list_selected;
                }
                r->base.needs_full_refresh_flag = true;
            }
            return true;
        case BTN_DOWN_CLICK:
            if (r->server_list_selected < total - 1) {
                r->server_list_selected++;
                if (r->server_list_selected >= r->server_list_scroll_offset + kServerListVisibleRows) {
                    r->server_list_scroll_offset = r->server_list_selected - kServerListVisibleRows + 1;
                }
                r->base.needs_full_refresh_flag = true;
            }
            return true;
        case BTN_BOOT_CLICK:
            if (r->server_list_dialog_handler && r->server_list_count > 0) {
                r->server_list_dialog_handler(r->server_list_addresses[r->server_list_selected],
                                              r->server_list_dialog_ctx);
            }
            r->showing_server_list_dialog = false;
            r->base.needs_full_refresh_flag = true;
            return true;
        case BTN_BOOT_LONG_PRESS:
            r->showing_server_list_dialog = false;
            r->base.needs_full_refresh_flag = true;
            return true;
        default:
            return true;
        }
    }

    /* Server dialog: UP/DN switch local/remote, BOOT confirm. */
    if (r->showing_server_dialog) {
        switch (event->type) {
        case BTN_UP_CLICK:
        case BTN_DOWN_CLICK:
            r->server_selected = (r->server_selected == 0) ? 1 : 0;
            r->base.needs_full_refresh_flag = true;
            return true;
        case BTN_BOOT_CLICK:
            if (r->server_dialog_handler) {
                r->server_dialog_handler(r->server_selected, r->server_dialog_ctx);
            }
            r->showing_server_dialog = false;
            r->base.needs_full_refresh_flag = true;
            return true;
        default:
            return true;
        }
    }

    /* OTA dialog: UP/DN select version, BOOT opens confirm, long BOOT closes. */
    if (r->showing_ota_dialog) {
        if (r->showing_ota_confirm_dialog) {
            switch (event->type) {
            case BTN_UP_CLICK:
            case BTN_DOWN_CLICK:
                r->ota_confirm_selected = (r->ota_confirm_selected == 0) ? 1 : 0;
                r->base.needs_full_refresh_flag = true;
                return true;
            case BTN_BOOT_CLICK:
                r->showing_ota_confirm_dialog = false;
                if (r->ota_confirm_selected == 0 && r->ota_dialog_handler) {
                    r->ota_dialog_handler(0, true, false, r->ota_dialog_ctx);
                }
                r->base.needs_full_refresh_flag = true;
                return true;
            case BTN_BOOT_LONG_PRESS:
            case BTN_UP_LONG_PRESS:
            case BTN_DOWN_LONG_PRESS:
                r->showing_ota_confirm_dialog = false;
                r->base.needs_full_refresh_flag = true;
                return true;
            default:
                return true;
            }
        }

        switch (event->type) {
        case BTN_UP_CLICK:
            if (r->ota_dialog_handler) {
                r->ota_dialog_handler(-1, false, false, r->ota_dialog_ctx);
            }
            r->base.needs_full_refresh_flag = true;
            return true;
        case BTN_DOWN_CLICK:
            if (r->ota_dialog_handler) {
                r->ota_dialog_handler(1, false, false, r->ota_dialog_ctx);
            }
            r->base.needs_full_refresh_flag = true;
            return true;
        case BTN_BOOT_CLICK:
            /* Select a firmware: pop the confirm dialog instead of
                 * updating immediately. */
            if (r->ota_state == 2 && r->ota_version_count > 0 && r->ota_selected_index >= 0) {
                snprintf(r->ota_confirm_firmware_name, sizeof(r->ota_confirm_firmware_name), "%s",
                         r->ota_versions[r->ota_selected_index]);
                r->ota_confirm_selected = 0;
                r->showing_ota_confirm_dialog = true;
            } else if (r->ota_dialog_handler) {
                r->ota_dialog_handler(0, true, false, r->ota_dialog_ctx);
            }
            r->base.needs_full_refresh_flag = true;
            return true;
        case BTN_BOOT_LONG_PRESS:
        case BTN_UP_LONG_PRESS:
        case BTN_DOWN_LONG_PRESS:
            if (r->ota_dialog_handler) {
                r->ota_dialog_handler(0, false, true, r->ota_dialog_ctx);
            }
            r->base.needs_full_refresh_flag = true;
            return true;
        default:
            return true;
        }
    }

    switch (event->type) {
    case BTN_UP_CLICK:
        if (r->selected_index > 0) {
            r->selected_index = find_prev_selectable(r, r->selected_index);
        } else {
            r->selected_index = get_last_selectable_index(r);
        }
        ensure_selection_visible(r);
        r->base.needs_full_refresh_flag = true;
        return true;

    case BTN_DOWN_CLICK: {
        if (r->selected_index < r->item_count - 1) {
            r->selected_index = find_next_selectable(r, r->selected_index);
        } else {
            r->selected_index = get_first_selectable_index(r); /* wrap to first */
        }
        ensure_selection_visible(r);
        r->base.needs_full_refresh_flag = true;
        return true;
    }

    case BTN_UP_LONG_PRESS:
        /* Scroll to top. */
        r->scroll_offset = 0;
        r->selected_index = get_first_selectable_index(r);
        r->first_visible_index = r->selected_index;
        r->base.needs_full_refresh_flag = true;
        return true;

    case BTN_BOOT_CLICK: {
        /* Toggle checkbox, trigger action, or navigate. */
        if (r->selected_index >= 0 && r->selected_index < r->item_count) {
            settings_page_item_t *item = &r->items[r->selected_index];
            if (item->type == SETTINGS_ITEM_CHECKBOX) {
                item->checked = !item->checked;
                if (item->on_click) {
                    item->on_click(item->on_click_ctx);
                }
                r->base.needs_full_refresh_flag = true;
                return true;
            } else if (item->type == SETTINGS_ITEM_ACTION || item->on_click) {
                if (item->on_click) {
                    item->on_click(item->on_click_ctx);
                }
                return true;
            }
        }
        break;
    }

    case BTN_DOWN_LONG_PRESS: {
        /* Scroll to bottom. */
        r->selected_index = get_last_selectable_index(r);
        r->first_visible_index = r->selected_index;
        for (int shown = 1; shown < kVisibleOptionCount; ++shown) {
            int prev = find_prev_selectable(r, r->first_visible_index);
            if (prev == r->first_visible_index)
                break;
            r->first_visible_index = prev;
        }
        ensure_selection_visible(r);
        r->base.needs_full_refresh_flag = true;
        return true;
    }

    default:
        break;
    }

    return false;
}

/* ------------------------------------------------------------------ */
/* Declarative menu data interface                                     */
/* ------------------------------------------------------------------ */

void settings_page_set_items(page_renderer_t *self, const settings_page_item_t *items, int count)
{
    settings_page_t *r = (settings_page_t *)self;

    /* Remember the current selection label to preserve it across rebuilds. */
    char prev_label[SETTINGS_PAGE_ITEM_LABEL_LEN] = {0};
    if (r->selected_index >= 0 && r->selected_index < r->item_count) {
        snprintf(prev_label, sizeof(prev_label), "%s", r->items[r->selected_index].label);
    }

    r->item_count = 0;
    if (items && count > 0) {
        int n = RD_MIN(count, SETTINGS_PAGE_MAX_ITEMS);
        for (int i = 0; i < n; ++i) {
            r->items[i] = items[i];
            r->items[i].label[SETTINGS_PAGE_ITEM_LABEL_LEN - 1] = '\0';
            r->items[i].value[SETTINGS_PAGE_ITEM_VALUE_LEN - 1] = '\0';
        }
        r->item_count = n;
    }

    /* Try to find the previously selected item in the new list. */
    if (prev_label[0] != '\0') {
        bool found = false;
        for (int i = 0; i < r->item_count; ++i) {
            if (strcmp(r->items[i].label, prev_label) == 0 && r->items[i].type != SETTINGS_ITEM_SECTION) {
                r->selected_index = i;
                found = true;
                break;
            }
        }
        if (found && r->selected_index >= 0 && r->selected_index < r->item_count &&
            r->items[r->selected_index].type == SETTINGS_ITEM_SECTION) {
            r->selected_index = get_first_selectable_index(r);
        }
        if (!found) {
            r->selected_index = get_first_selectable_index(r);
        }
    } else {
        r->selected_index = get_first_selectable_index(r);
    }

    r->scroll_offset = 0;
    r->first_visible_index = r->selected_index;
    ensure_selection_visible(r);
    settings_page_show_category_hint(self, 0);
    r->base.needs_full_refresh_flag = true;
}

void settings_page_update_item(page_renderer_t *self, int index, const char *value)
{
    settings_page_t *r = (settings_page_t *)self;
    if (index >= 0 && index < r->item_count && value) {
        snprintf(r->items[index].value, sizeof(r->items[index].value), "%s", value);
        r->base.needs_full_refresh_flag = true;
    }
}

void settings_page_update_checked(page_renderer_t *self, int index, bool checked)
{
    settings_page_t *r = (settings_page_t *)self;
    if (index >= 0 && index < r->item_count) {
        r->items[index].checked = checked;
        r->base.needs_full_refresh_flag = true;
    }
}

int settings_page_get_item_count(const page_renderer_t *self)
{
    const settings_page_t *r = (const settings_page_t *)self;
    return r->item_count;
}

int settings_page_get_selected_index(const page_renderer_t *self)
{
    const settings_page_t *r = (const settings_page_t *)self;
    return r->selected_index;
}

/* ------------------------------------------------------------------ */
/* Debug info + device identity                                        */
/* ------------------------------------------------------------------ */

void settings_page_show_debug_info(page_renderer_t *self)
{
    settings_page_t *r = (settings_page_t *)self;
    r->showing_debug_info = true;
    r->debug_hint_until_us = esp_timer_get_time() + 3000000; /* 3 seconds */
    r->base.needs_full_refresh_flag = true;
}

void settings_page_set_firmware_version(page_renderer_t *self, const char *version)
{
    settings_page_t *r = (settings_page_t *)self;
    if (version) {
        snprintf(r->firmware_version, sizeof(r->firmware_version), "%s", version);
    }
}

void settings_page_set_device_info(page_renderer_t *self, const char *mac, const char *chip)
{
    settings_page_t *r = (settings_page_t *)self;
    if (mac) {
        snprintf(r->mac_address, sizeof(r->mac_address), "%s", mac);
    }
    if (chip) {
        snprintf(r->chip_model, sizeof(r->chip_model), "%s", chip);
    }
}

/* ------------------------------------------------------------------ */
/* Dialog show/hide + handlers                                         */
/* ------------------------------------------------------------------ */

void settings_page_show_volume_dialog(page_renderer_t *self, int volume)
{
    settings_page_t *r = (settings_page_t *)self;
    r->volume_dialog_value = RD_CLAMP(volume, 0, 100);
    r->showing_volume_dialog = true;
    r->base.needs_full_refresh_flag = true;
}

void settings_page_set_volume_dialog_handler(page_renderer_t *self, settings_page_volume_handler_t handler, void *ctx)
{
    settings_page_t *r = (settings_page_t *)self;
    r->volume_dialog_handler = handler;
    r->volume_dialog_ctx = ctx;
}

void settings_page_show_about_dialog(page_renderer_t *self)
{
    settings_page_t *r = (settings_page_t *)self;
    r->showing_about_dialog = true;
    r->base.needs_full_refresh_flag = true;
}

void settings_page_hide_about_dialog(page_renderer_t *self)
{
    settings_page_t *r = (settings_page_t *)self;
    r->showing_about_dialog = false;
    r->base.needs_full_refresh_flag = true;
}

bool settings_page_is_about_dialog_showing(const page_renderer_t *self)
{
    const settings_page_t *r = (const settings_page_t *)self;
    return r->showing_about_dialog;
}

void settings_page_show_storage_dialog(page_renderer_t *self, const char *used, const char *total, int photos, int txts)
{
    settings_page_t *r = (settings_page_t *)self;
    if (used) {
        snprintf(r->storage_used, sizeof(r->storage_used), "%s", used);
    }
    if (total) {
        snprintf(r->storage_total, sizeof(r->storage_total), "%s", total);
    }
    r->storage_photos = photos;
    r->storage_txts = txts;
    r->showing_storage_dialog = true;
    r->base.needs_full_refresh_flag = true;
}

void settings_page_hide_storage_dialog(page_renderer_t *self)
{
    settings_page_t *r = (settings_page_t *)self;
    r->showing_storage_dialog = false;
    r->base.needs_full_refresh_flag = true;
}

bool settings_page_is_storage_dialog_showing(const page_renderer_t *self)
{
    const settings_page_t *r = (const settings_page_t *)self;
    return r->showing_storage_dialog;
}

void settings_page_show_server_dialog(page_renderer_t *self, const char *current_addr, const char *local_addr,
                                      const char *remote_addr)
{
    settings_page_t *r = (settings_page_t *)self;
    if (current_addr) {
        snprintf(r->server_current_addr, sizeof(r->server_current_addr), "%s", current_addr);
    }
    if (local_addr) {
        snprintf(r->server_local_addr, sizeof(r->server_local_addr), "%s", local_addr);
    }
    if (remote_addr) {
        snprintf(r->server_remote_addr, sizeof(r->server_remote_addr), "%s", remote_addr);
    }
    r->server_selected = (current_addr && remote_addr && strcmp(current_addr, remote_addr) == 0) ? 1 : 0;
    r->showing_server_dialog = true;
    r->base.needs_full_refresh_flag = true;
}

void settings_page_hide_server_dialog(page_renderer_t *self)
{
    settings_page_t *r = (settings_page_t *)self;
    r->showing_server_dialog = false;
    r->base.needs_full_refresh_flag = true;
}

bool settings_page_is_server_dialog_showing(const page_renderer_t *self)
{
    const settings_page_t *r = (const settings_page_t *)self;
    return r->showing_server_dialog;
}

int settings_page_get_server_dialog_selection(const page_renderer_t *self)
{
    const settings_page_t *r = (const settings_page_t *)self;
    return r->server_selected;
}

void settings_page_set_server_dialog_handler(page_renderer_t *self, settings_page_server_handler_t handler, void *ctx)
{
    settings_page_t *r = (settings_page_t *)self;
    r->server_dialog_handler = handler;
    r->server_dialog_ctx = ctx;
}

void settings_page_show_server_list_dialog(page_renderer_t *self, const char *const *addresses, int count,
                                           const char *current_addr)
{
    settings_page_t *r = (settings_page_t *)self;
    r->server_list_count = 0;
    if (addresses && count > 0) {
        int n = RD_MIN(count, SETTINGS_PAGE_MAX_SERVER_ADDRS);
        for (int i = 0; i < n; ++i) {
            snprintf(r->server_list_addresses[i], sizeof(r->server_list_addresses[i]), "%s", addresses[i]);
        }
        r->server_list_count = n;
    }
    if (current_addr) {
        snprintf(r->server_list_current, sizeof(r->server_list_current), "%s", current_addr);
    }
    r->server_list_selected = 0;
    r->server_list_scroll_offset = 0;
    /* Find the current address index. */
    for (int i = 0; i < r->server_list_count; ++i) {
        if (strcmp(r->server_list_addresses[i], r->server_list_current) == 0) {
            r->server_list_selected = i;
            break;
        }
    }
    r->showing_server_list_dialog = true;
    r->base.needs_full_refresh_flag = true;
}

void settings_page_hide_server_list_dialog(page_renderer_t *self)
{
    settings_page_t *r = (settings_page_t *)self;
    r->showing_server_list_dialog = false;
    r->base.needs_full_refresh_flag = true;
}

bool settings_page_is_server_list_dialog_showing(const page_renderer_t *self)
{
    const settings_page_t *r = (const settings_page_t *)self;
    return r->showing_server_list_dialog;
}

/* Returns a pointer into the internal buffer; valid until the next
 * ShowServerListDialog call. Empty string when nothing is selected. */
const char *settings_page_get_server_list_selection(const page_renderer_t *self)
{
    const settings_page_t *r = (const settings_page_t *)self;
    if (r->server_list_selected >= 0 && r->server_list_selected < r->server_list_count) {
        return r->server_list_addresses[r->server_list_selected];
    }
    return "";
}

void settings_page_set_server_list_dialog_handler(page_renderer_t *self, settings_page_server_list_handler_t handler,
                                                  void *ctx)
{
    settings_page_t *r = (settings_page_t *)self;
    r->server_list_dialog_handler = handler;
    r->server_list_dialog_ctx = ctx;
}

void settings_page_show_theme_dialog(page_renderer_t *self, rawdraw_theme_id_t current_theme)
{
    settings_page_t *r = (settings_page_t *)self;
    r->theme_selected = 0;
    const int count = settings_page_theme_count();
    for (int i = 0; i < count; ++i) {
        if (settings_page_theme_at(i)->id == current_theme) {
            r->theme_selected = i;
            break;
        }
    }
    r->showing_theme_dialog = true;
    r->base.needs_full_refresh_flag = true;
}

void settings_page_hide_theme_dialog(page_renderer_t *self)
{
    settings_page_t *r = (settings_page_t *)self;
    r->showing_theme_dialog = false;
    r->base.needs_full_refresh_flag = true;
}

bool settings_page_is_theme_dialog_showing(const page_renderer_t *self)
{
    const settings_page_t *r = (const settings_page_t *)self;
    return r->showing_theme_dialog;
}

void settings_page_set_theme_dialog_handler(page_renderer_t *self, settings_page_theme_handler_t handler, void *ctx)
{
    settings_page_t *r = (settings_page_t *)self;
    r->theme_dialog_handler = handler;
    r->theme_dialog_ctx = ctx;
}

void settings_page_show_ota_dialog(page_renderer_t *self, const char *const *versions, int version_count,
                                   const char *current_version, int selected_index, int progress_percent,
                                   const char *status_text, int state)
{
    settings_page_t *r = (settings_page_t *)self;
    r->ota_version_count = 0;
    if (versions && version_count > 0) {
        int n = RD_MIN(version_count, SETTINGS_PAGE_MAX_OTA_VERSIONS);
        for (int i = 0; i < n; ++i) {
            snprintf(r->ota_versions[i], sizeof(r->ota_versions[i]), "%s", versions[i]);
        }
        r->ota_version_count = n;
    }
    if (current_version) {
        snprintf(r->ota_current_version, sizeof(r->ota_current_version), "%s", current_version);
    }
    r->ota_selected_index = RD_CLAMP(selected_index, 0, RD_MAX(0, r->ota_version_count - 1));
    r->ota_progress_percent = RD_CLAMP(progress_percent, 0, 100);
    if (status_text) {
        snprintf(r->ota_status_text, sizeof(r->ota_status_text), "%s", status_text);
    }
    r->ota_state = state;
    r->showing_ota_dialog = true;
    r->base.needs_full_refresh_flag = true;
}

void settings_page_hide_ota_dialog(page_renderer_t *self)
{
    settings_page_t *r = (settings_page_t *)self;
    r->showing_ota_dialog = false;
    r->showing_ota_confirm_dialog = false;
    r->base.needs_full_refresh_flag = true;
}

bool settings_page_is_ota_dialog_showing(const page_renderer_t *self)
{
    const settings_page_t *r = (const settings_page_t *)self;
    return r->showing_ota_dialog;
}

void settings_page_set_ota_dialog_handler(page_renderer_t *self, settings_page_ota_handler_t handler, void *ctx)
{
    settings_page_t *r = (settings_page_t *)self;
    r->ota_dialog_handler = handler;
    r->ota_dialog_ctx = ctx;
}

/* ------------------------------------------------------------------ */
/* vtable instance                                                     */
/* ------------------------------------------------------------------ */

EXT_RAM_BSS_ATTR settings_page_t s_settings_instance;

const page_renderer_ops_t settings_page_ops = {
    .init = settings_page_init,
    .render = settings_page_render,
    .handle_input = settings_page_handle_input,
    .get_dirty_rect = NULL,
    .needs_full_refresh = NULL,
    .mark_full_refresh = NULL,
    .clear_full_refresh_flag = NULL,
    .append_text = NULL,
    .begin_stream = NULL,
    .end_stream = NULL,
};

PAGE_REGISTER(UI_PAGE_SETTINGS, "设置", FA_SETTINGS_GEAR, true, 50, &settings_page_ops, &s_settings_instance.base);
