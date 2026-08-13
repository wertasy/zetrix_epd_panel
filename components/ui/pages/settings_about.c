/**
 * @file settings_about.c
 * @brief About dialog — C port of SettingsRenderer::RenderAboutDialog.
 *
 * Macintosh-style modal with a close box, title-bar stripes, an app icon
 * block and device info rows (name / model / firmware / hardware / serial /
 * website). State comes from the shared settings_page_t.
 */
#include "settings_page.h"

#include "rawdraw_ext.h"
#include "style.h"
#include "layout.h"
#include "ui_text_util.h"

#include <stdio.h>
#include <string.h>

#define kAboutRowHeight 24
#define kAboutTitlebarH 28

void settings_page_render_about_dialog(settings_page_t *r, uint8_t *fb, int width, int height)
{
    const rawdraw_paint_style_t modal_style = rawdraw_theme_component(ROLE_MODAL);
    const rawdraw_paint_style_t shadow_style = rawdraw_theme_style(THEME_TOKEN_SHADOW);
    const rawdraw_color_t text = rawdraw_theme_color_for(THEME_TOKEN_TEXT_PRIMARY);
    const rawdraw_color_t secondary = rawdraw_theme_color_for(THEME_TOKEN_TEXT_SECONDARY);
    const rawdraw_color_t border = rawdraw_theme_color_for(THEME_TOKEN_BORDER);
    const rawdraw_color_t accent = settings_page_token_ink_on_paper(THEME_TOKEN_ACCENT);
    const int dialog_w = 316;
    const int dialog_h = STYLE_DIALOG_H_LG;
    const int dialog_x = (width - dialog_w) / 2;
    const int dialog_y = STYLE_STATUS_BAR_HEIGHT + 30;
    const int content_right = dialog_x + dialog_w - 20;
    const int titlebar_h = kAboutTitlebarH;
    const int shadow_offset = 2;

    settings_page_clear_dialog_region(fb, width, height, dialog_x + 3, dialog_y + 3, dialog_w, dialog_h,
                                      STYLE_BORDER_RADIUS_MD, 2);
    /* Solid offset shadow: black base first, white dialog covers it; only
     * the right and bottom 2px stay visible (no white gap). */
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

    const char *title = "About notellm";
    const int title_w = rawdraw_measure_text_width(title, r->font);
    rawdraw_draw_text(fb, width, height, dialog_x + (dialog_w - title_w) / 2,
                      rawdraw_layout_ink_centered_text_top_y_in_box(r->font, title, dialog_y, titlebar_h, 0), title,
                      r->font, text);

    /* Macintosh-inspired title-bar stripes. */
    for (int yy = dialog_y + 6; yy < dialog_y + titlebar_h - 5; yy += 4) {
        rawdraw_draw_hline(fb, width, height, yy, dialog_x + 28, dialog_x + (dialog_w - title_w) / 2 - 8, border);
        rawdraw_draw_hline(fb, width, height, yy, dialog_x + (dialog_w + title_w) / 2 + 8, dialog_x + dialog_w - 12,
                           border);
    }

    /* App icon block. */
    const int icon_x = dialog_x + 16;
    const int icon_y = dialog_y + titlebar_h + 20;
    rawdraw_draw_rect_border(fb, width, height, (rawdraw_rect_t){icon_x, icon_y, 46, 38}, 2, accent);
    rawdraw_draw_rect_border(fb, width, height, (rawdraw_rect_t){icon_x + 7, icon_y + 7, 32, 20}, 1, accent);
    rawdraw_fill_rect(fb, width, height, (rawdraw_rect_t){icon_x + 18, icon_y + 30, 10, 2}, accent);

    /* Info rows. */
    char version_buf[SETTINGS_PAGE_VERSION_LEN];
    char serial_buf[SETTINGS_PAGE_VERSION_LEN];
    snprintf(version_buf, sizeof(version_buf), "%s", r->firmware_version[0] != '\0' ? r->firmware_version : "未知");
    snprintf(serial_buf, sizeof(serial_buf), "%s", r->mac_address[0] != '\0' ? r->mac_address : "未读取");
    struct info_row_t {
        const char *label;
        const char *value;
    };
    const struct info_row_t rows[] = {
        {"设备名称", "zectrix"},   {"型号", "Wert-Beta1.0"},
        {"固件版本", version_buf}, {"硬件版本", r->chip_model[0] != '\0' ? r->chip_model : "ESP32-S3"},
        {"序列号", serial_buf},    {"官方网站", "canhui.wang"},
    };
    const int row_count = (int)(sizeof(rows) / sizeof(rows[0]));
    const int row_h = kAboutRowHeight;
    int y = dialog_y + titlebar_h + 12;
    const int rows_x = dialog_x + 70;
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
