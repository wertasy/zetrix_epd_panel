/**
 * @file settings_themes.c
 * @brief Theme selection data table + theme picker dialog — C port of the
 *        theme enumeration used by C++ rawdraw::SettingsRenderer.
 *
 * The theme table is built once (lazily) from the rawdraw theme registry
 * (rawdraw_theme_count/at/display_name), so it always mirrors the actual
 * theme definitions instead of duplicating display names.
 */
#include "settings_page.h"

#include "rawdraw_ext.h"
#include "style.h"
#include "layout.h"

#include <string.h>

#define MAX_THEME_ENTRIES 16

static settings_theme_entry_t s_theme_table[MAX_THEME_ENTRIES];
static int                    s_theme_count = -1;

static void ensure_theme_table(void)
{
    if (s_theme_count >= 0)
        return;
    s_theme_count = 0;
    const int n   = rawdraw_theme_count();
    for (int i = 0; i < n && i < MAX_THEME_ENTRIES; ++i) {
        const rawdraw_theme_id_t id       = rawdraw_theme_at(i);
        s_theme_table[s_theme_count].id   = id;
        s_theme_table[s_theme_count].name = rawdraw_theme_display_name(id);
        s_theme_count++;
    }
}

int settings_page_theme_count(void)
{
    ensure_theme_table();
    return s_theme_count;
}

const settings_theme_entry_t *settings_page_theme_at(int index)
{
    ensure_theme_table();
    if (index >= 0 && index < s_theme_count) {
        return &s_theme_table[index];
    }
    return NULL;
}

void settings_page_render_theme_dialog(settings_page_t *r, uint8_t *fb, int width, int height)
{
    const rawdraw_paint_style_t modal_style    = rawdraw_theme_component(ROLE_MODAL);
    const rawdraw_paint_style_t text_style     = rawdraw_theme_style(THEME_TOKEN_TEXT_PRIMARY);
    const rawdraw_paint_style_t selected_style = rawdraw_theme_component(ROLE_SETTINGS_SELECTED);
    const rawdraw_paint_style_t shadow_style   = rawdraw_theme_style(THEME_TOKEN_SHADOW);
    const rawdraw_paint_style_t border_style   = rawdraw_theme_style(THEME_TOKEN_BORDER);

    const int dialog_w   = STYLE_DIALOG_W;
    const int dialog_h   = 218;
    const int dialog_x   = (width - dialog_w) / 2;
    const int dialog_y   = STYLE_STATUS_BAR_HEIGHT + 32;
    const int titlebar_h = 28;
    const int row_h      = 25;

    settings_page_clear_dialog_region(fb, width, height, dialog_x + 3, dialog_y + 3, dialog_w, dialog_h,
                                      STYLE_BORDER_RADIUS_MD, 2);
    rawdraw_draw_styled_round_rect(fb, width, height, (rawdraw_rect_t){dialog_x + 2, dialog_y + 2, dialog_w, dialog_h},
                                   STYLE_BORDER_RADIUS_MD, &shadow_style);
    rawdraw_draw_styled_round_rect(fb, width, height, (rawdraw_rect_t){dialog_x, dialog_y, dialog_w, dialog_h},
                                   STYLE_BORDER_RADIUS_MD, &modal_style);
    rawdraw_draw_hline(fb, width, height, dialog_y + titlebar_h, dialog_x + 1, dialog_x + dialog_w - 2,
                       border_style.border);

    const char *title   = "选择主题";
    const int   title_w = rawdraw_measure_text_width(title, r->font);
    rawdraw_draw_styled_text(fb, width, height, dialog_x + (dialog_w - title_w) / 2,
                             rawdraw_layout_ink_centered_text_top_y_in_box(r->font, title, dialog_y, titlebar_h, 0),
                             title, r->font, &text_style);

    const int                total      = settings_page_theme_count();
    const rawdraw_theme_id_t current_id = rawdraw_theme_current_id();
    int                      y          = dialog_y + titlebar_h + 8;
    for (int i = 0; i < total; ++i) {
        const settings_theme_entry_t *entry = settings_page_theme_at(i);
        if (!entry)
            break;
        const rawdraw_theme_definition_t *th       = rawdraw_theme_get(entry->id);
        const bool                        selected = (i == r->theme_selected);
        const bool                        current  = (entry->id == current_id);
        const int                         row_x    = dialog_x + 18;
        const int                         row_w    = dialog_w - 36;
        const int                         center_y = y + row_h / 2;
        if (selected) {
            rawdraw_draw_styled_round_rect(fb, width, height, (rawdraw_rect_t){row_x - 4, y, row_w + 8, row_h},
                                           STYLE_BORDER_RADIUS_SM, &selected_style);
        }

        const rawdraw_paint_style_t row_text = selected ? selected_style : text_style;
        rawdraw_draw_styled_text(fb, width, height, row_x,
                                 rawdraw_layout_ink_centered_text_top_y(r->font, entry->name, center_y, 0), entry->name,
                                 r->font, &row_text);

        /* Accent + danger swatches for the theme. */
        const int                   swatch_x = dialog_x + dialog_w - 62;
        const rawdraw_paint_style_t accent   = th->tokens[THEME_TOKEN_ACCENT];
        rawdraw_draw_styled_rect(fb, width, height, (rawdraw_rect_t){swatch_x, y + 6, 16, 13}, &accent);
        rawdraw_draw_rect_border(fb, width, height, (rawdraw_rect_t){swatch_x, y + 6, 16, 13}, 1, border_style.border);
        const rawdraw_paint_style_t danger = th->tokens[THEME_TOKEN_DANGER];
        rawdraw_draw_styled_rect(fb, width, height, (rawdraw_rect_t){swatch_x + 20, y + 6, 16, 13}, &danger);
        rawdraw_draw_rect_border(fb, width, height, (rawdraw_rect_t){swatch_x + 20, y + 6, 16, 13}, 1,
                                 border_style.border);

        if (current) {
            const char *mark   = "当前";
            const int   mark_w = rawdraw_measure_text_width(mark, r->value_font);
            rawdraw_draw_styled_text(fb, width, height, swatch_x - mark_w - 8,
                                     rawdraw_layout_ink_centered_text_top_y(r->value_font, mark, center_y, 0), mark,
                                     r->value_font, &row_text);
        }
        y += row_h;
    }

    const char *hint = "UP/DN 选择  BOOT 应用";
    rawdraw_draw_styled_text(fb, width, height, dialog_x + 24,
                             rawdraw_layout_ink_centered_text_top_y(r->font, hint, dialog_y + dialog_h - 18, 0), hint,
                             r->font, &text_style);
}
