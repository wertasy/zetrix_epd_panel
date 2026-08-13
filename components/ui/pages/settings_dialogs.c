/**
 * @file settings_dialogs.c
 * @brief Settings modal dialogs — C port of the dialog renderers from
 *        C++ rawdraw::SettingsRenderer.
 *
 * Renders the volume, storage, server address, server address history list,
 * OTA update and OTA confirm dialogs on top of the base settings page.
 * All state is read/written through the shared settings_page_t.
 */
#include "settings_page.h"

#include "rawdraw_ext.h"
#include "style.h"
#include "layout.h"
#include "ui_text_util.h"

#include <stdio.h>
#include <string.h>

/* Shared layout constants (single source of truth, see settings_page.c). */
#define kTextOpticalNudgeY 0
#define kValueOpticalNudgeY 0
#define kVolumeDialogClearPad 0
#define kAboutRowHeight 24
#define kServerListVisibleRows 5
#define kOtaVisibleRows 4
#define kDialogClearPad 0

void settings_page_render_volume_dialog(settings_page_t *r, uint8_t *fb, int width, int height)
{
    const rawdraw_paint_style_t modal_style = rawdraw_theme_component(ROLE_MODAL);
    const rawdraw_paint_style_t shadow_style = rawdraw_theme_style(THEME_TOKEN_SHADOW);
    const rawdraw_paint_style_t progress_style = rawdraw_theme_component(ROLE_PROGRESS);
    const rawdraw_color_t text = rawdraw_theme_color_for(THEME_TOKEN_TEXT_PRIMARY);
    const rawdraw_color_t secondary = rawdraw_theme_color_for(THEME_TOKEN_TEXT_SECONDARY);
    const rawdraw_color_t border = rawdraw_theme_color_for(THEME_TOKEN_BORDER);
    const rawdraw_paint_style_t track_style = rawdraw_theme_style(THEME_TOKEN_BACKGROUND_PRIMARY);
    const rawdraw_color_t accent = settings_page_token_ink_on_paper(THEME_TOKEN_ACCENT);
    const rawdraw_color_t progress_fill = settings_page_token_ink_on_paper(THEME_TOKEN_PROGRESS_FILL);
    const int dialog_w = STYLE_DIALOG_W;
    const int dialog_h = STYLE_DIALOG_H_MD;
    const int dialog_x = (width - dialog_w) / 2;
    const int dialog_y = (height - dialog_h) / 2;
    const int inner_x = dialog_x + 18;
    const int inner_w = dialog_w - 36;

    settings_page_clear_dialog_region(fb, width, height, dialog_x, dialog_y, dialog_w, dialog_h, STYLE_BORDER_RADIUS_LG,
                                      kVolumeDialogClearPad);
    /* Same solid 2px offset shadow as the other dialogs: no white gap. */
    rawdraw_draw_styled_round_rect(fb, width, height, (rawdraw_rect_t){dialog_x + 2, dialog_y + 2, dialog_w, dialog_h},
                                   STYLE_BORDER_RADIUS_LG, &shadow_style);
    rawdraw_draw_styled_round_rect(fb, width, height, (rawdraw_rect_t){dialog_x, dialog_y, dialog_w, dialog_h},
                                   STYLE_BORDER_RADIUS_LG, &modal_style);

    const char *title = "音量调整";
    const int title_w = rawdraw_measure_text_width(title, r->font);
    rawdraw_draw_text(fb, width, height, dialog_x + (dialog_w - title_w) / 2,
                      rawdraw_layout_ink_centered_text_top_y(r->font, title, dialog_y + 24, kTextOpticalNudgeY), title,
                      r->font, text);
    rawdraw_draw_hline(fb, width, height, dialog_y + 42, dialog_x + 14, dialog_x + dialog_w - 14, border);

    char volume_buf[16];
    snprintf(volume_buf, sizeof(volume_buf), "%d%%", r->volume_dialog_value);
    const int value_w = rawdraw_measure_text_width(volume_buf, r->title_font);
    rawdraw_draw_text(fb, width, height, dialog_x + (dialog_w - value_w) / 2,
                      rawdraw_layout_ink_centered_text_top_y(r->title_font, volume_buf, dialog_y + 70, 0), volume_buf,
                      r->title_font, accent);

    const int speaker_x = inner_x + 12;
    const int speaker_y = dialog_y + 60;
    settings_page_draw_vector_icon(fb, width, height, "音量", speaker_x, speaker_y, accent);

    const int track_x = inner_x;
    const int track_y = dialog_y + 102;
    const int track_w = inner_w;
    const int track_h = 16;
    rawdraw_draw_styled_round_rect(fb, width, height, (rawdraw_rect_t){track_x, track_y, track_w, track_h},
                                   STYLE_BORDER_RADIUS_PILL, &track_style);
    rawdraw_draw_rect_border(fb, width, height, (rawdraw_rect_t){track_x, track_y, track_w, track_h}, 1,
                             progress_style.border);
    int fill_w = (track_w - 4) * r->volume_dialog_value / 100;
    if (fill_w > 0) {
        rawdraw_fill_rect(fb, width, height, (rawdraw_rect_t){track_x + 2, track_y + 2, fill_w, track_h - 4},
                          progress_fill);
    }

    for (int i = 0; i <= 4; ++i) {
        const int tick_x = track_x + 2 + (track_w - 4) * i / 4;
        rawdraw_draw_vline(fb, width, height, tick_x, track_y + track_h + 4, track_y + track_h + 8, border);
    }

    const int hint_center_y = dialog_y + dialog_h - 20;
    rawdraw_draw_text(
        fb, width, height, inner_x + 6,
        rawdraw_layout_ink_centered_text_top_y(r->font, "UP/DN 调整  BOOT 保存", hint_center_y, kTextOpticalNudgeY),
        "UP/DN 调整  BOOT 保存", r->font, secondary);
}

void settings_page_render_storage_dialog(settings_page_t *r, uint8_t *fb, int width, int height)
{
    const rawdraw_paint_style_t modal_style = rawdraw_theme_component(ROLE_MODAL);
    const rawdraw_paint_style_t shadow_style = rawdraw_theme_style(THEME_TOKEN_SHADOW);
    const rawdraw_paint_style_t progress_style = rawdraw_theme_component(ROLE_PROGRESS);
    const rawdraw_color_t text = rawdraw_theme_color_for(THEME_TOKEN_TEXT_PRIMARY);
    const rawdraw_color_t secondary = rawdraw_theme_color_for(THEME_TOKEN_TEXT_SECONDARY);
    const rawdraw_color_t border = rawdraw_theme_color_for(THEME_TOKEN_BORDER);
    const rawdraw_paint_style_t track_style = rawdraw_theme_style(THEME_TOKEN_BACKGROUND_PRIMARY);
    const rawdraw_color_t accent = settings_page_token_ink_on_paper(THEME_TOKEN_ACCENT);
    const rawdraw_color_t progress_fill = settings_page_token_ink_on_paper(THEME_TOKEN_PROGRESS_FILL);
    const int dialog_w = 316;
    const int dialog_h = 180;
    const int dialog_x = (width - dialog_w) / 2;
    const int dialog_y = STYLE_STATUS_BAR_HEIGHT + 30;
    const int content_right = dialog_x + dialog_w - 20;
    const int titlebar_h = 28;
    const int shadow_offset = 2;
    const int row_h = kAboutRowHeight;

    settings_page_clear_dialog_region(fb, width, height, dialog_x + 3, dialog_y + 3, dialog_w, dialog_h,
                                      STYLE_BORDER_RADIUS_MD, 2);
    rawdraw_draw_styled_round_rect(
        fb, width, height, (rawdraw_rect_t){dialog_x + shadow_offset, dialog_y + shadow_offset, dialog_w, dialog_h},
        STYLE_BORDER_RADIUS_MD, &shadow_style);
    rawdraw_draw_styled_round_rect(fb, width, height, (rawdraw_rect_t){dialog_x, dialog_y, dialog_w, dialog_h},
                                   STYLE_BORDER_RADIUS_MD, &modal_style);
    rawdraw_draw_hline(fb, width, height, dialog_y + titlebar_h, dialog_x + 1, dialog_x + dialog_w - 2, border);
    /* Close box. */
    rawdraw_draw_rect_border(fb, width, height, (rawdraw_rect_t){dialog_x + 8, dialog_y + 8, 12, 12}, 1, accent);
    rawdraw_draw_line(fb, width, height, (rawdraw_point_t){dialog_x + 10, dialog_y + 10},
                      (rawdraw_point_t){dialog_x + 18, dialog_y + 18}, accent);
    rawdraw_draw_line(fb, width, height, (rawdraw_point_t){dialog_x + 18, dialog_y + 10},
                      (rawdraw_point_t){dialog_x + 10, dialog_y + 18}, accent);

    const char *title = "存储空间";
    const int title_w = rawdraw_measure_text_width(title, r->font);
    rawdraw_draw_text(fb, width, height, dialog_x + (dialog_w - title_w) / 2,
                      rawdraw_layout_ink_centered_text_top_y_in_box(r->font, title, dialog_y, titlebar_h, 0), title,
                      r->font, text);

    /* Title-bar stripes (Macintosh style). */
    for (int yy = dialog_y + 6; yy < dialog_y + titlebar_h - 5; yy += 4) {
        rawdraw_draw_hline(fb, width, height, yy, dialog_x + 28, dialog_x + (dialog_w - title_w) / 2 - 8, border);
        rawdraw_draw_hline(fb, width, height, yy, dialog_x + (dialog_w + title_w) / 2 + 8, dialog_x + dialog_w - 12,
                           border);
    }

    /* Storage icon: simple box + usage bar. */
    const int icon_x = dialog_x + 22;
    const int icon_y = dialog_y + titlebar_h + 16;
    rawdraw_draw_rect_border(fb, width, height, (rawdraw_rect_t){icon_x, icon_y, 46, 38}, 2, accent);
    const int fill_pct = 60; /* default moderate fill for visual */
    const int bar_x = icon_x + 7;
    const int bar_w = 32;
    const int bar_h = 20;
    const int bar_y = icon_y + 7;
    rawdraw_draw_styled_rect(fb, width, height, (rawdraw_rect_t){bar_x, bar_y, bar_w, bar_h}, &track_style);
    rawdraw_draw_rect_border(fb, width, height, (rawdraw_rect_t){bar_x, bar_y, bar_w, bar_h}, 1, progress_style.border);
    int fill_w = (bar_w - 2) * fill_pct / 100;
    if (fill_w > 0) {
        rawdraw_fill_rect(fb, width, height, (rawdraw_rect_t){bar_x + 1, bar_y + 1, fill_w, bar_h - 2}, progress_fill);
    }

    /* Info rows. */
    char photos_buf[16];
    char txts_buf[16];
    snprintf(photos_buf, sizeof(photos_buf), "%d", r->storage_photos);
    snprintf(txts_buf, sizeof(txts_buf), "%d", r->storage_txts);
    struct info_row_t {
        const char *label;
        const char *value;
    };
    const struct info_row_t rows[] = {
        {"已用空间", r->storage_used},
        {"总空间", r->storage_total},
        {"图片数量", photos_buf},
        {"TXT数量", txts_buf},
    };
    const int row_count = (int)(sizeof(rows) / sizeof(rows[0]));
    const int rows_x = dialog_x + 88;
    int y = dialog_y + titlebar_h + 12;
    for (int i = 0; i < row_count; ++i) {
        const int center_y = y + row_h / 2;
        rawdraw_draw_text(fb, width, height, rows_x,
                          rawdraw_layout_ink_centered_text_top_y(r->font, rows[i].label, center_y, 0), rows[i].label,
                          r->font, text);
        const int value_left_min = rows_x + 84;
        char display_value[SETTINGS_PAGE_ITEM_VALUE_LEN];
        ui_text_fit_to_width(rows[i].value, r->value_font, RD_MAX(0, content_right - value_left_min), display_value,
                             sizeof(display_value));
        const int value_w = rawdraw_measure_text_width(display_value, r->value_font);
        rawdraw_draw_text(fb, width, height, content_right - value_w,
                          rawdraw_layout_ink_centered_text_top_y(r->value_font, display_value, center_y, 0),
                          display_value, r->value_font, secondary);
        rawdraw_draw_hline(fb, width, height, y + row_h - 1, rows_x, content_right, border);
        y += row_h;
    }
}

void settings_page_render_server_dialog(settings_page_t *r, uint8_t *fb, int width, int height)
{
    const rawdraw_paint_style_t modal_style = rawdraw_theme_component(ROLE_MODAL);
    const rawdraw_paint_style_t shadow_style = rawdraw_theme_style(THEME_TOKEN_SHADOW);
    const rawdraw_paint_style_t selected_style = rawdraw_theme_component(ROLE_SETTINGS_SELECTED);
    const rawdraw_color_t text = rawdraw_theme_color_for(THEME_TOKEN_TEXT_PRIMARY);
    const rawdraw_color_t secondary = rawdraw_theme_color_for(THEME_TOKEN_TEXT_SECONDARY);
    const rawdraw_color_t border = rawdraw_theme_color_for(THEME_TOKEN_BORDER);
    const rawdraw_color_t danger = settings_page_token_ink_on_paper(THEME_TOKEN_DANGER);
    const int dialog_w = 316;
    const int dialog_h = STYLE_DIALOG_H_LG;
    const int dialog_x = (width - dialog_w) / 2;
    const int dialog_y = STYLE_STATUS_BAR_HEIGHT + 30;
    const int content_right = dialog_x + dialog_w - 20;
    const int titlebar_h = 28;
    const int shadow_offset = 2;
    const int row_h = kAboutRowHeight;

    settings_page_clear_dialog_region(fb, width, height, dialog_x + 3, dialog_y + 3, dialog_w, dialog_h,
                                      STYLE_BORDER_RADIUS_MD, 2);
    rawdraw_draw_styled_round_rect(
        fb, width, height, (rawdraw_rect_t){dialog_x + shadow_offset, dialog_y + shadow_offset, dialog_w, dialog_h},
        STYLE_BORDER_RADIUS_MD, &shadow_style);
    rawdraw_draw_styled_round_rect(fb, width, height, (rawdraw_rect_t){dialog_x, dialog_y, dialog_w, dialog_h},
                                   STYLE_BORDER_RADIUS_MD, &modal_style);
    rawdraw_draw_hline(fb, width, height, dialog_y + titlebar_h, dialog_x + 1, dialog_x + dialog_w - 2, border);
    /* Close box (danger color). */
    rawdraw_draw_rect_border(fb, width, height, (rawdraw_rect_t){dialog_x + 8, dialog_y + 8, 12, 12}, 1, danger);
    rawdraw_draw_line(fb, width, height, (rawdraw_point_t){dialog_x + 10, dialog_y + 10},
                      (rawdraw_point_t){dialog_x + 18, dialog_y + 18}, danger);
    rawdraw_draw_line(fb, width, height, (rawdraw_point_t){dialog_x + 18, dialog_y + 10},
                      (rawdraw_point_t){dialog_x + 10, dialog_y + 18}, danger);

    const char *title = "服务地址";
    const int title_w = rawdraw_measure_text_width(title, r->font);
    rawdraw_draw_text(fb, width, height, dialog_x + (dialog_w - title_w) / 2,
                      rawdraw_layout_ink_centered_text_top_y_in_box(r->font, title, dialog_y, titlebar_h, 0), title,
                      r->font, text);

    for (int yy = dialog_y + 6; yy < dialog_y + titlebar_h - 5; yy += 4) {
        rawdraw_draw_hline(fb, width, height, yy, dialog_x + 28, dialog_x + (dialog_w - title_w) / 2 - 8, border);
        rawdraw_draw_hline(fb, width, height, yy, dialog_x + (dialog_w + title_w) / 2 + 8, dialog_x + dialog_w - 12,
                           border);
    }

    const int rows_x = dialog_x + 18;
    int y = dialog_y + titlebar_h + 10;

    /* Current connection label. */
    char current_label[SETTINGS_PAGE_ADDR_LEN + 16];
    snprintf(current_label, sizeof(current_label), "当前: %s",
             r->server_current_addr[0] != '\0' ? r->server_current_addr : "未连接");
    rawdraw_draw_text(fb, width, height, rows_x,
                      rawdraw_layout_ink_centered_text_top_y_in_box(r->font, current_label, y, row_h, 0), current_label,
                      r->font, secondary);
    y += row_h;
    rawdraw_draw_hline(fb, width, height, y - 4, rows_x, content_right, border);

    /* Local / remote option rows. */
    struct server_option_t {
        const char *label;
        const char *addr;
        int index;
    };
    const struct server_option_t options[] = {
        {"本地自发现", r->server_local_addr, 0},
        {"远程服务器", r->server_remote_addr, 1},
    };
    const int option_count = (int)(sizeof(options) / sizeof(options[0]));
    for (int i = 0; i < option_count; ++i) {
        const struct server_option_t *opt = &options[i];
        const int center_y = y + row_h / 2;
        const bool is_selected = (r->server_selected == opt->index);

        if (is_selected) {
            rawdraw_draw_styled_round_rect(fb, width, height,
                                           (rawdraw_rect_t){rows_x - 4, y, content_right - rows_x + 8, row_h},
                                           STYLE_BORDER_RADIUS_SM, &selected_style);
        }

        rawdraw_draw_text(fb, width, height, rows_x,
                          rawdraw_layout_ink_centered_text_top_y(r->font, opt->label, center_y, 0), opt->label, r->font,
                          is_selected ? selected_style.fg : text);

        char addr_display[SETTINGS_PAGE_ADDR_LEN];
        ui_text_fit_to_width(opt->addr[0] != '\0' ? opt->addr : "--", r->value_font,
                             RD_MAX(0, content_right - rows_x - 80), addr_display, sizeof(addr_display));
        const int addr_w = rawdraw_measure_text_width(addr_display, r->value_font);
        rawdraw_draw_text(fb, width, height, content_right - addr_w,
                          rawdraw_layout_ink_centered_text_top_y(r->value_font, addr_display, center_y, 0),
                          addr_display, r->value_font, is_selected ? selected_style.fg : secondary);
        y += row_h;
    }

    const int hint_center_y = dialog_y + dialog_h - 20;
    rawdraw_draw_text(fb, width, height, dialog_x + 30,
                      rawdraw_layout_ink_centered_text_top_y(r->font, "UP/DN 切换  BOOT 确认", hint_center_y, 0),
                      "UP/DN 切换  BOOT 确认", r->font, secondary);
}

void settings_page_render_server_list_dialog(settings_page_t *r, uint8_t *fb, int width, int height)
{
    const rawdraw_paint_style_t modal_style = rawdraw_theme_component(ROLE_MODAL);
    const rawdraw_paint_style_t shadow_style = rawdraw_theme_style(THEME_TOKEN_SHADOW);
    const rawdraw_paint_style_t selected_style = rawdraw_theme_component(ROLE_SETTINGS_SELECTED);
    const rawdraw_color_t text = rawdraw_theme_color_for(THEME_TOKEN_TEXT_PRIMARY);
    const rawdraw_color_t secondary = rawdraw_theme_color_for(THEME_TOKEN_TEXT_SECONDARY);
    const rawdraw_color_t border = rawdraw_theme_color_for(THEME_TOKEN_BORDER);
    const rawdraw_color_t danger = settings_page_token_ink_on_paper(THEME_TOKEN_DANGER);
    const rawdraw_color_t accent = settings_page_token_ink_on_paper(THEME_TOKEN_ACCENT);
    const int dialog_w = 316;
    const int dialog_h = 220;
    const int dialog_x = (width - dialog_w) / 2;
    const int dialog_y = STYLE_STATUS_BAR_HEIGHT + 30;
    const int content_right = dialog_x + dialog_w - 20;
    const int titlebar_h = 28;
    const int shadow_offset = 2;
    const int row_h = kAboutRowHeight;

    settings_page_clear_dialog_region(fb, width, height, dialog_x + 3, dialog_y + 3, dialog_w, dialog_h,
                                      STYLE_BORDER_RADIUS_MD, 2);
    rawdraw_draw_styled_round_rect(
        fb, width, height, (rawdraw_rect_t){dialog_x + shadow_offset, dialog_y + shadow_offset, dialog_w, dialog_h},
        STYLE_BORDER_RADIUS_MD, &shadow_style);
    rawdraw_draw_styled_round_rect(fb, width, height, (rawdraw_rect_t){dialog_x, dialog_y, dialog_w, dialog_h},
                                   STYLE_BORDER_RADIUS_MD, &modal_style);
    rawdraw_draw_hline(fb, width, height, dialog_y + titlebar_h, dialog_x + 1, dialog_x + dialog_w - 2, border);
    /* Close box (danger color). */
    rawdraw_draw_rect_border(fb, width, height, (rawdraw_rect_t){dialog_x + 8, dialog_y + 8, 12, 12}, 1, danger);
    rawdraw_draw_line(fb, width, height, (rawdraw_point_t){dialog_x + 10, dialog_y + 10},
                      (rawdraw_point_t){dialog_x + 18, dialog_y + 18}, danger);
    rawdraw_draw_line(fb, width, height, (rawdraw_point_t){dialog_x + 18, dialog_y + 10},
                      (rawdraw_point_t){dialog_x + 10, dialog_y + 18}, danger);

    const char *title = "服务地址历史";
    const int title_w = rawdraw_measure_text_width(title, r->font);
    rawdraw_draw_text(fb, width, height, dialog_x + (dialog_w - title_w) / 2,
                      rawdraw_layout_ink_centered_text_top_y_in_box(r->font, title, dialog_y, titlebar_h, 0), title,
                      r->font, text);

    for (int yy = dialog_y + 6; yy < dialog_y + titlebar_h - 5; yy += 4) {
        rawdraw_draw_hline(fb, width, height, yy, dialog_x + 28, dialog_x + (dialog_w - title_w) / 2 - 8, border);
        rawdraw_draw_hline(fb, width, height, yy, dialog_x + (dialog_w + title_w) / 2 + 8, dialog_x + dialog_w - 12,
                           border);
    }

    const int rows_x = dialog_x + 18;
    int y = dialog_y + titlebar_h + 10;
    const int total = r->server_list_count;

    if (total == 0) {
        rawdraw_draw_text(fb, width, height, rows_x,
                          rawdraw_layout_ink_centered_text_top_y(r->font, "无历史地址", y + row_h / 2, 0), "无历史地址",
                          r->font, secondary);
    } else {
        const int visible_rows = RD_MIN(kServerListVisibleRows, total);
        const int scroll_start = r->server_list_scroll_offset;

        for (int i = 0; i < visible_rows; ++i) {
            const int item_idx = scroll_start + i;
            if (item_idx >= total)
                break;

            const char *addr = r->server_list_addresses[item_idx];
            const bool is_selected = (item_idx == r->server_list_selected);
            const bool is_current = (strcmp(addr, r->server_list_current) == 0);
            const int center_y = y + row_h / 2;

            if (is_selected) {
                rawdraw_draw_styled_round_rect(fb, width, height,
                                               (rawdraw_rect_t){rows_x - 4, y, content_right - rows_x + 8, row_h},
                                               STYLE_BORDER_RADIUS_SM, &selected_style);
            }

            /* Index number. */
            char idx_buf[16];
            snprintf(idx_buf, sizeof(idx_buf), "%d.", item_idx + 1);
            rawdraw_draw_text(fb, width, height, rows_x,
                              rawdraw_layout_ink_centered_text_top_y(r->font, idx_buf, center_y, 0), idx_buf, r->font,
                              is_selected ? selected_style.fg : accent);

            /* Address value (fit to available width). */
            char addr_display[SETTINGS_PAGE_ADDR_LEN];
            ui_text_fit_to_width(addr, r->value_font, RD_MAX(0, content_right - rows_x - 40), addr_display,
                                 sizeof(addr_display));
            rawdraw_draw_text(fb, width, height, rows_x + 28,
                              rawdraw_layout_ink_centered_text_top_y(r->value_font, addr_display, center_y, 0),
                              addr_display, r->value_font, is_selected ? selected_style.fg : text);

            /* Current indicator. */
            if (is_current) {
                const char *cur_mark = "(当前)";
                rawdraw_draw_text(fb, width, height, content_right - rawdraw_measure_text_width(cur_mark, r->font) - 4,
                                  rawdraw_layout_ink_centered_text_top_y(r->font, cur_mark, center_y, 0), cur_mark,
                                  r->font, is_selected ? selected_style.fg : secondary);
            }

            y += row_h;
        }

        /* Scroll indicator (when more items than visible rows). */
        if (total > kServerListVisibleRows) {
            const int scroll_bar_x = content_right + 4;
            const int scroll_bar_h = kServerListVisibleRows * row_h;
            const int scroll_bar_y = dialog_y + titlebar_h + 10;
            rawdraw_draw_vline(fb, width, height, scroll_bar_x, scroll_bar_y, scroll_bar_y + scroll_bar_h, border);
            const int thumb_h = scroll_bar_h * kServerListVisibleRows / total;
            const int thumb_y = scroll_bar_y + (scroll_start * scroll_bar_h / total);
            rawdraw_fill_rect(fb, width, height, (rawdraw_rect_t){scroll_bar_x - 2, thumb_y, 4, thumb_h}, accent);
        }
    }

    const int hint_center_y = dialog_y + dialog_h - 20;
    rawdraw_draw_text(fb, width, height, dialog_x + 30,
                      rawdraw_layout_ink_centered_text_top_y(r->font, "UP/DN 滚动  BOOT 选择", hint_center_y, 0),
                      "UP/DN 滚动  BOOT 选择", r->font, secondary);
}

void settings_page_render_ota_dialog(settings_page_t *r, uint8_t *fb, int width, int height)
{
    const rawdraw_paint_style_t modal_style = rawdraw_theme_component(ROLE_MODAL);
    const rawdraw_paint_style_t shadow_style = rawdraw_theme_style(THEME_TOKEN_SHADOW);
    const rawdraw_paint_style_t selected_style = rawdraw_theme_component(ROLE_SETTINGS_SELECTED);
    const rawdraw_paint_style_t progress_style = rawdraw_theme_component(ROLE_PROGRESS);
    const rawdraw_color_t text = rawdraw_theme_color_for(THEME_TOKEN_TEXT_PRIMARY);
    const rawdraw_color_t secondary = rawdraw_theme_color_for(THEME_TOKEN_TEXT_SECONDARY);
    const rawdraw_color_t border = rawdraw_theme_color_for(THEME_TOKEN_BORDER);
    const rawdraw_paint_style_t track_style = rawdraw_theme_style(THEME_TOKEN_BACKGROUND_PRIMARY);
    const rawdraw_color_t danger = settings_page_token_ink_on_paper(THEME_TOKEN_DANGER);
    const rawdraw_color_t progress_fill = settings_page_token_ink_on_paper(THEME_TOKEN_PROGRESS_FILL);
    const int dialog_w = 316;
    const int dialog_h = 220;
    const int dialog_x = (width - dialog_w) / 2;
    const int dialog_y = STYLE_STATUS_BAR_HEIGHT + 30;
    const int content_right = dialog_x + dialog_w - 20;
    const int titlebar_h = 28;
    const int shadow_offset = 2;
    const int row_h = 28;

    settings_page_clear_dialog_region(fb, width, height, dialog_x + 3, dialog_y + 3, dialog_w, dialog_h,
                                      STYLE_BORDER_RADIUS_MD, 2);
    rawdraw_draw_styled_round_rect(
        fb, width, height, (rawdraw_rect_t){dialog_x + shadow_offset, dialog_y + shadow_offset, dialog_w, dialog_h},
        STYLE_BORDER_RADIUS_MD, &shadow_style);
    rawdraw_draw_styled_round_rect(fb, width, height, (rawdraw_rect_t){dialog_x, dialog_y, dialog_w, dialog_h},
                                   STYLE_BORDER_RADIUS_MD, &modal_style);
    rawdraw_draw_hline(fb, width, height, dialog_y + titlebar_h, dialog_x + 1, dialog_x + dialog_w - 2, border);

    rawdraw_draw_rect_border(fb, width, height, (rawdraw_rect_t){dialog_x + 8, dialog_y + 8, 12, 12}, 1, danger);
    rawdraw_draw_line(fb, width, height, (rawdraw_point_t){dialog_x + 10, dialog_y + 10},
                      (rawdraw_point_t){dialog_x + 18, dialog_y + 18}, danger);
    rawdraw_draw_line(fb, width, height, (rawdraw_point_t){dialog_x + 18, dialog_y + 10},
                      (rawdraw_point_t){dialog_x + 10, dialog_y + 18}, danger);

    const char *title = "固件更新";
    const int title_w = rawdraw_measure_text_width(title, r->font);
    rawdraw_draw_text(fb, width, height, dialog_x + (dialog_w - title_w) / 2,
                      rawdraw_layout_ink_centered_text_top_y_in_box(r->font, title, dialog_y, titlebar_h, 0), title,
                      r->font, text);

    for (int yy = dialog_y + 6; yy < dialog_y + titlebar_h - 5; yy += 4) {
        rawdraw_draw_hline(fb, width, height, yy, dialog_x + 28, dialog_x + (dialog_w - title_w) / 2 - 8, border);
        rawdraw_draw_hline(fb, width, height, yy, dialog_x + (dialog_w + title_w) / 2 + 8, dialog_x + dialog_w - 12,
                           border);
    }

    const int rows_x = dialog_x + 18;
    int y = dialog_y + titlebar_h + 10;

    char current_buf[SETTINGS_PAGE_OTA_FIRMWARE_LEN + 16];
    snprintf(current_buf, sizeof(current_buf), "当前: %s",
             r->ota_current_version[0] != '\0' ? r->ota_current_version : "--");
    rawdraw_draw_text(fb, width, height, rows_x,
                      rawdraw_layout_ink_centered_text_top_y(r->font, current_buf, y + row_h / 2, 0), current_buf,
                      r->font, secondary);
    y += row_h;
    rawdraw_draw_hline(fb, width, height, y - 4, rows_x, content_right, border);

    const bool selecting = (r->ota_state == 2);
    const bool downloading = (r->ota_state == 4 || r->ota_state == 5 || r->ota_state == 6);
    const bool failed = (r->ota_state == 7);

    if (selecting && r->ota_version_count > 0) {
        const int total = r->ota_version_count;
        const int visible_rows = RD_MIN(kOtaVisibleRows, total);
        int start = RD_MAX(0, r->ota_selected_index - visible_rows / 2);
        if (start + visible_rows > total)
            start = RD_MAX(0, total - visible_rows);
        for (int i = 0; i < visible_rows; ++i) {
            const int idx = start + i;
            const bool is_selected = (idx == r->ota_selected_index);
            const int center_y = y + row_h / 2;
            if (is_selected) {
                rawdraw_draw_styled_round_rect(fb, width, height,
                                               (rawdraw_rect_t){rows_x - 4, y, content_right - rows_x + 8, row_h},
                                               STYLE_BORDER_RADIUS_SM, &selected_style);
            }
            char label_display[SETTINGS_PAGE_OTA_FIRMWARE_LEN];
            ui_text_fit_to_width(r->ota_versions[idx], r->font, RD_MAX(0, content_right - rows_x - 8), label_display,
                                 sizeof(label_display));
            rawdraw_draw_text(fb, width, height, rows_x,
                              rawdraw_layout_ink_centered_text_top_y(r->font, label_display, center_y, 0),
                              label_display, r->font, is_selected ? selected_style.fg : text);
            y += row_h;
        }
    } else if (downloading) {
        const int bar_x = rows_x;
        const int bar_y = y + 34;
        const int bar_w = content_right - rows_x;
        const int bar_h = 16;
        char status_buf[SETTINGS_PAGE_OTA_STATUS_LEN];
        const char *status_src = r->ota_status_text[0] != '\0' ? r->ota_status_text : "正在更新...";
        ui_text_fit_to_width(status_src, r->font, RD_MAX(0, content_right - rows_x), status_buf, sizeof(status_buf));
        rawdraw_draw_text(fb, width, height, rows_x,
                          rawdraw_layout_ink_centered_text_top_y(r->font, status_buf, y + row_h / 2, 0), status_buf,
                          r->font, text);
        rawdraw_draw_styled_round_rect(fb, width, height, (rawdraw_rect_t){bar_x, bar_y, bar_w, bar_h},
                                       STYLE_BORDER_RADIUS_PILL, &track_style);
        rawdraw_draw_rect_border(fb, width, height, (rawdraw_rect_t){bar_x, bar_y, bar_w, bar_h}, 1,
                                 progress_style.border);
        const int fill_w = (bar_w - 4) * r->ota_progress_percent / 100;
        if (fill_w > 0) {
            rawdraw_fill_rect(fb, width, height, (rawdraw_rect_t){bar_x + 2, bar_y + 2, fill_w, bar_h - 4},
                              progress_fill);
        }
        char pct_buf[16];
        snprintf(pct_buf, sizeof(pct_buf), "%d%%", r->ota_progress_percent);
        const int pct_w = rawdraw_measure_text_width(pct_buf, r->font);
        rawdraw_draw_text(fb, width, height, dialog_x + (dialog_w - pct_w) / 2, bar_y + bar_h + 8, pct_buf, r->font,
                          secondary);
    } else {
        char status_buf[SETTINGS_PAGE_OTA_STATUS_LEN];
        const char *status_src =
            r->ota_status_text[0] != '\0' ? r->ota_status_text : (failed ? "更新失败" : "正在获取版本列表...");
        ui_text_fit_to_width(status_src, r->font, RD_MAX(0, content_right - rows_x), status_buf, sizeof(status_buf));
        rawdraw_draw_text(fb, width, height, rows_x,
                          rawdraw_layout_ink_centered_text_top_y(r->font, status_buf, y + row_h / 2, 0), status_buf,
                          r->font, failed ? danger : text);
    }

    const char *hint = selecting ? "UP/DN 选择  BOOT 更新  长按取消" : "长按取消  BOOT 关闭";
    char hint_display[64];
    ui_text_fit_to_width(hint, r->font, dialog_w - 40, hint_display, sizeof(hint_display));
    const int hint_center_y = dialog_y + dialog_h - 20;
    rawdraw_draw_text(fb, width, height, dialog_x + 20,
                      rawdraw_layout_ink_centered_text_top_y(r->font, hint_display, hint_center_y, 0), hint_display,
                      r->font, secondary);
}

void settings_page_render_ota_confirm_dialog(settings_page_t *r, uint8_t *fb, int width, int height)
{
    const rawdraw_paint_style_t bg_style = rawdraw_theme_style(THEME_TOKEN_BACKGROUND_PRIMARY);
    const rawdraw_paint_style_t modal_style = rawdraw_theme_component(ROLE_MODAL);
    const rawdraw_paint_style_t title_style = rawdraw_theme_style(THEME_TOKEN_BADGE);
    const rawdraw_paint_style_t selected_style = rawdraw_theme_component(ROLE_SETTINGS_SELECTED);
    const rawdraw_color_t text = rawdraw_theme_color_for(THEME_TOKEN_TEXT_PRIMARY);
    const rawdraw_color_t secondary = rawdraw_theme_color_for(THEME_TOKEN_TEXT_SECONDARY);
    /* OTA confirm: show firmware name, UP/DN picks confirm/cancel, BOOT runs. */
    const int dialog_w = 280;
    const int dialog_h = 160;
    const int dialog_x = (width - dialog_w) / 2;
    const int dialog_y = (height - dialog_h) / 2;
    const int titlebar_h = 36;
    const int row_h = 28;

    rawdraw_draw_styled_rect(fb, width, height,
                             (rawdraw_rect_t){dialog_x - kDialogClearPad, dialog_y - kDialogClearPad,
                                              dialog_w + kDialogClearPad * 2, dialog_h + kDialogClearPad * 2},
                             &bg_style);

    /* Dialog border. */
    rawdraw_draw_styled_round_rect(fb, width, height, (rawdraw_rect_t){dialog_x, dialog_y, dialog_w, dialog_h},
                                   STYLE_BORDER_RADIUS_MD, &modal_style);

    /* Title bar background. */
    rawdraw_draw_styled_round_rect(fb, width, height,
                                   (rawdraw_rect_t){dialog_x + 1, dialog_y + 1, dialog_w - 2, titlebar_h - 2},
                                   STYLE_BORDER_RADIUS_MD - 1, &title_style);

    const char *title = "确认更新?";
    const int title_w = rawdraw_measure_text_width(title, r->font);
    rawdraw_draw_text(fb, width, height, dialog_x + (dialog_w - title_w) / 2,
                      rawdraw_layout_ink_centered_text_top_y(r->font, title, dialog_y + titlebar_h / 2, 0), title,
                      r->font, title_style.fg);

    const int content_x = dialog_x + 20;
    int y = dialog_y + titlebar_h + 15;

    /* Firmware name. */
    char firmware_label[SETTINGS_PAGE_OTA_FIRMWARE_LEN + 8];
    snprintf(firmware_label, sizeof(firmware_label), "固件: %s", r->ota_confirm_firmware_name);
    char firmware_display[SETTINGS_PAGE_OTA_FIRMWARE_LEN + 8];
    ui_text_fit_to_width(firmware_label, r->font, dialog_w - 60, firmware_display, sizeof(firmware_display));
    rawdraw_draw_text(fb, width, height, content_x,
                      rawdraw_layout_ink_centered_text_top_y(r->font, firmware_display, y + row_h / 2, 0),
                      firmware_display, r->font, secondary);
    y += row_h + 8;

    /* Confirm / cancel options. */
    const char *options[2] = {"确认更新", "取消"};
    for (int i = 0; i < 2; ++i) {
        const bool is_selected = (i == r->ota_confirm_selected);
        const int opt_y = y + i * row_h;

        if (is_selected) {
            rawdraw_draw_styled_round_rect(
                fb, width, height,
                (rawdraw_rect_t){content_x - 4, opt_y + 2, dialog_w - (content_x - dialog_x) + 4, row_h - 4},
                STYLE_BORDER_RADIUS_SM, &selected_style);
        }

        rawdraw_draw_text(fb, width, height, content_x,
                          rawdraw_layout_ink_centered_text_top_y(r->font, options[i], opt_y + row_h / 2, 0), options[i],
                          r->font, is_selected ? selected_style.fg : text);
    }

    /* Hint. */
    const char *hint = "UP/DN 选择  BOOT 确认  长按返回";
    const int hint_w = rawdraw_measure_text_width(hint, r->font);
    const int hint_y = dialog_y + dialog_h - 24;
    rawdraw_draw_text(fb, width, height, dialog_x + (dialog_w - hint_w) / 2,
                      rawdraw_layout_ink_centered_text_top_y(r->font, hint, hint_y, 0), hint, r->font, secondary);
}
