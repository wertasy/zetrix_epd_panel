/**
 * @file ui_quick_switch.c
 * @brief Quick-switch overlay rendering — extracted from ui_manager.c (Phase 2.2).
 */
#include "ui_manager_internal.h"
#include "fa_settings.h"
#include "theme.h"
#include "layout.h"

#include <string.h>

rawdraw_rect_t ui_quick_switch_get_bounds(const struct ui_manager *mgr)
{
    const int overlay_w = 224;
    const int overlay_h = 204;
    return (rawdraw_rect_t){(mgr->width - overlay_w) / 2, STYLE_STATUS_BAR_HEIGHT + 26, overlay_w, overlay_h};
}

void ui_quick_switch_draw_overlay(struct ui_manager *mgr, uint8_t *fb, int width, int height)
{
    const rawdraw_rect_t overlay = ui_quick_switch_get_bounds(mgr);
    const int overlay_x = overlay.x;
    const int overlay_y = overlay.y;
    const int overlay_w = overlay.w;
    const int overlay_h = overlay.h;
    const int titlebar_h = 28;
    const int shadow_offset = 2;
    const int item_h = 24;
    const int item_gap = 3;
    const lv_font_t *font = &SourceHanSansSC_Regular_slim;
    const lv_font_t *title_font = &SourceHanSansSC_Regular_slim;
    const rawdraw_paint_style_t modal_style = rawdraw_theme_component(ROLE_MODAL);
    const rawdraw_paint_style_t selected_style = rawdraw_theme_component(ROLE_QUICK_SWITCH_ROW);
    const rawdraw_paint_style_t text_style = rawdraw_theme_style(THEME_TOKEN_TEXT_PRIMARY);
    const rawdraw_paint_style_t border_style = rawdraw_theme_style(THEME_TOKEN_BORDER);
    const rawdraw_paint_style_t shadow_style = rawdraw_theme_style(THEME_TOKEN_SHADOW);

    rawdraw_draw_styled_round_rect(
        fb, width, height, (rawdraw_rect_t){overlay_x + shadow_offset, overlay_y + shadow_offset, overlay_w, overlay_h},
        STYLE_BORDER_RADIUS_MD, &shadow_style);
    rawdraw_draw_styled_round_rect(fb, width, height, (rawdraw_rect_t){overlay_x, overlay_y, overlay_w, overlay_h},
                                   STYLE_BORDER_RADIUS_MD, &modal_style);
    const char *title = "快速切换";
    const int title_w = rawdraw_measure_text_width(title, title_font);
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
        const bool selected = i == mgr->quick_switch_index;
        const int row_x = overlay_x + 12;
        const int row_w = overlay_w - 24 - 10;
        const rawdraw_paint_style_t row_style = selected ? selected_style : modal_style;
        rawdraw_draw_styled_round_rect(fb, width, height, (rawdraw_rect_t){row_x, y, row_w, item_h},
                                       STYLE_BORDER_RADIUS_MD, &row_style);
        if (selected) {
            rawdraw_fill_rect(fb, width, height, (rawdraw_rect_t){row_x + 5, y + 6, 3, item_h - 12}, row_style.border);
        }
        const lv_font_t *icon_font = &fa_settings_16;
        const int icon_gap = 6;
        const int text_start_x = row_x + 18;
        const rawdraw_color_t text_color = row_style.fg;

        if (mgr->quick_items[i]->icon && mgr->quick_items[i]->icon[0] != '\0') {
            const int icon_x = text_start_x;
            const int icon_y =
                rawdraw_layout_ink_centered_text_top_y_in_box(icon_font, mgr->quick_items[i]->icon, y, item_h, 0);
            rawdraw_draw_text(fb, width, height, icon_x, icon_y, mgr->quick_items[i]->icon, icon_font, text_color);
            const int icon_w = rawdraw_measure_text_width(mgr->quick_items[i]->icon, icon_font);
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
        const int sb_w = 4;
        const int sb_x = overlay_x + overlay_w - 14;
        const int sb_top = overlay_y + titlebar_h + 10;
        const int sb_bottom = overlay_y + overlay_h - 28;
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
