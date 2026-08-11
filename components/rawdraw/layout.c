#include "layout.h"
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>

int rawdraw_layout_calc_baseline_y(const lv_font_t *font, int center_y, int visual_offset)
{
    if (!font) {
        return center_y + visual_offset;
    }
    int box_top = center_y - (int)(font->line_height / 2);
    return box_top + (int)font->line_height - (int)font->base_line + visual_offset;
}

int rawdraw_layout_top_y_from_baseline(const lv_font_t *font, int baseline_y)
{
    if (!font) {
        return baseline_y;
    }
    return baseline_y - (int)font->line_height + (int)font->base_line;
}

int rawdraw_layout_center_text_top_y(const lv_font_t *font, int box_top, int box_height, int visual_offset)
{
    if (!font) {
        return box_top + visual_offset;
    }
    return box_top + (box_height - (int)font->line_height) / 2 + visual_offset;
}

rawdraw_text_ink_bounds_t rawdraw_layout_measure_text_ink_bounds(const lv_font_t *font, const char *text)
{
    rawdraw_text_ink_bounds_t bounds = {.valid = false, .top = 0, .bottom = 0, .height = 0};
    if (!font || !text) {
        return bounds;
    }

    int         line_index = 0;
    int         min_y      = INT_MAX;
    int         max_y      = INT_MIN;
    const char *p          = text;

    while (*p) {
        uint32_t ch = utf8_next(&p);
        if (ch == 0) {
            break;
        }

        if (ch == '\n') {
            line_index++;
            continue;
        }

        if (ch < 32) {
            continue;
        }

        lv_font_glyph_dsc_t g = {0};
        g.resolved_font       = font;
        if (!lv_font_get_glyph_dsc(font, &g, ch, 0)) {
            continue;
        }

        if (g.box_w > 0 && g.box_h > 0) {
            int line_offset_y    = line_index * (int)font->line_height;
            int glyph_top_rel    = line_offset_y - (int)g.ofs_y - (int)g.box_h;
            int glyph_bottom_rel = line_offset_y - (int)g.ofs_y;

            if (glyph_top_rel < min_y) {
                min_y = glyph_top_rel;
            }
            if (glyph_bottom_rel > max_y) {
                max_y = glyph_bottom_rel;
            }
        }
    }

    if (min_y != INT_MAX && max_y != INT_MIN) {
        bounds.valid  = true;
        bounds.top    = min_y;
        bounds.bottom = max_y;
        bounds.height = max_y - min_y;
    }

    return bounds;
}

int rawdraw_layout_ink_centered_text_top_y(const lv_font_t *font, const char *text, int center_y, int visual_offset)
{
    if (!font) {
        return center_y + visual_offset;
    }

    rawdraw_text_ink_bounds_t bounds = rawdraw_layout_measure_text_ink_bounds(font, text);
    int                       baseline_y;
    if (bounds.valid) {
        baseline_y = center_y + visual_offset - (bounds.top + bounds.bottom) / 2;
    } else {
        // Fallback: standard centering of font line_height
        baseline_y = rawdraw_layout_calc_baseline_y(font, center_y, visual_offset);
    }
    return rawdraw_layout_top_y_from_baseline(font, baseline_y);
}

int rawdraw_layout_ink_centered_text_top_y_in_box(const lv_font_t *font, const char *text, int box_top, int box_height,
                                                  int visual_offset)
{
    int center_y = box_top + box_height / 2;
    return rawdraw_layout_ink_centered_text_top_y(font, text, center_y, visual_offset);
}
