#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <limits.h>
#include "layout.h"
#include "rawdraw.h"

// Define dummy fonts for the declarations in font_engine.h
const lv_font_t SourceHanSansSC_Regular_slim;
const lv_font_t SourceHanSansSC_Medium_slim;
const lv_font_t font_zectrix_16_1;
const lv_font_t font_zectrix_48_1;
const lv_font_t weather_icons_16;
const lv_font_t weather_icons_48;

// Define a mock font for layout testing
static bool mock_get_glyph_dsc(const struct _lv_font_t *font, lv_font_glyph_dsc_t *dsc_out, uint32_t letter,
                               uint32_t letter_next)
{
    dsc_out->resolved_font = font;
    (void)letter_next;
    if (letter == 'A') {
        dsc_out->adv_w = 10;
        dsc_out->box_w = 8;
        dsc_out->box_h = 12;
        dsc_out->ofs_x = 1;
        dsc_out->ofs_y = 2; // relative to baseline
        return true;
    } else if (letter == 'g') {
        dsc_out->adv_w = 10;
        dsc_out->box_w = 8;
        dsc_out->box_h = 12;
        dsc_out->ofs_x = 1;
        dsc_out->ofs_y = -4; // goes 4px below baseline
        return true;
    } else if (letter == ' ') {
        dsc_out->adv_w = 5;
        dsc_out->box_w = 0;
        dsc_out->box_h = 0;
        dsc_out->ofs_x = 0;
        dsc_out->ofs_y = 0;
        return true;
    } else if (letter == 'X') {
        dsc_out->adv_w = 8;
        dsc_out->box_w = 6;
        dsc_out->box_h = 0;
        dsc_out->ofs_x = 1;
        dsc_out->ofs_y = 1;
        return true;
    } else if (letter == '?') {
        // Unknown character
        return false;
    }

    // Default fallback
    dsc_out->adv_w = 8;
    dsc_out->box_w = 6;
    dsc_out->box_h = 8;
    dsc_out->ofs_x = 1;
    dsc_out->ofs_y = 1;
    return true;
}

const lv_font_t mock_font = {.get_glyph_dsc    = mock_get_glyph_dsc,
                             .get_glyph_bitmap = NULL,
                             .release_glyph    = NULL,
                             .line_height      = 20,
                             .base_line        = 4};

void test_baseline_calculations(void)
{
    printf("Testing baseline and top Y calculations...\n");

    // 1. rawdraw_layout_calc_baseline_y
    // box_top = center_y - (line_height / 2) = 100 - 10 = 90
    // baseline = box_top + line_height - base_line + visual_offset = 90 + 20 - 4 + 1 = 107
    int baseline = rawdraw_layout_calc_baseline_y(&mock_font, 100, 1);
    assert(baseline == 107);

    // 2. rawdraw_layout_top_y_from_baseline
    // top_y = baseline - line_height + base_line = 107 - 20 + 4 = 91
    int top_y = rawdraw_layout_top_y_from_baseline(&mock_font, 107);
    assert(top_y == 91);

    // 3. rawdraw_layout_center_text_top_y
    // top_y = box_top + (box_height - line_height) / 2 + visual_offset = 50 + (40 - 20)/2 - 2 = 58
    int center_top = rawdraw_layout_center_text_top_y(&mock_font, 50, 40, -2);
    assert(center_top == 58);

    printf("Baseline and top Y calculations passed!\n");
}

void test_measure_text_ink_bounds(void)
{
    printf("Testing measure text ink bounds...\n");

    // Test single character 'A'
    // line_offset_y = 0. glyph_top_rel = -2 - 12 = -14. glyph_bottom_rel = -2.
    rawdraw_text_ink_bounds_t b1 = rawdraw_layout_measure_text_ink_bounds(&mock_font, "A");
    assert(b1.valid == true);
    assert(b1.top == -14);
    assert(b1.bottom == -2);
    assert(b1.height == 12);

    // Test single character 'g'
    // line_offset_y = 0. glyph_top_rel = 4 - 12 = -8. glyph_bottom_rel = 4.
    rawdraw_text_ink_bounds_t b2 = rawdraw_layout_measure_text_ink_bounds(&mock_font, "g");
    assert(b2.valid == true);
    assert(b2.top == -8);
    assert(b2.bottom == 4);
    assert(b2.height == 12);

    // Test combination "Ag"
    // min_y = -14, max_y = 4.
    rawdraw_text_ink_bounds_t b3 = rawdraw_layout_measure_text_ink_bounds(&mock_font, "Ag");
    assert(b3.valid == true);
    assert(b3.top == -14);
    assert(b3.bottom == 4);
    assert(b3.height == 18);

    // Test multi-line "A\ng"
    // Line 0 'A': rel_top = -14, rel_bottom = -2
    // Line 1 'g': rel_top = 20 - (-4) - 12 = 12, rel_bottom = 20 - (-4) = 24
    // min_y = -14, max_y = 24. height = 38
    rawdraw_text_ink_bounds_t b4 = rawdraw_layout_measure_text_ink_bounds(&mock_font, "A\ng");
    assert(b4.valid == true);
    assert(b4.top == -14);
    assert(b4.bottom == 24);
    assert(b4.height == 38);

    // Test empty, spaces, or invalid characters
    rawdraw_text_ink_bounds_t b_empty = rawdraw_layout_measure_text_ink_bounds(&mock_font, "");
    assert(b_empty.valid == false);

    rawdraw_text_ink_bounds_t b_spaces = rawdraw_layout_measure_text_ink_bounds(&mock_font, "   ");
    assert(b_spaces.valid == false);

    rawdraw_text_ink_bounds_t b_unknown = rawdraw_layout_measure_text_ink_bounds(&mock_font, "???");
    assert(b_unknown.valid == false);

    printf("Measure text ink bounds passed!\n");
}

void test_ink_centered_text(void)
{
    printf("Testing ink centered text positioning...\n");

    // 1. rawdraw_layout_ink_centered_text_top_y
    // Text: "A", bounds: top = -14, bottom = -2
    // center_y = 100, visual_offset = 0
    // baseline_y = 100 - (-14 + -2) / 2 = 100 - (-8) = 108
    // top_y = 108 - 20 + 4 = 92
    int top_y1 = rawdraw_layout_ink_centered_text_top_y(&mock_font, "A", 100, 0);
    assert(top_y1 == 92);

    // Text: "g", bounds: top = -8, bottom = 4
    // center_y = 100, visual_offset = 2
    // baseline_y = 100 + 2 - (-8 + 4) / 2 = 102 - (-2) = 104
    // top_y = 104 - 20 + 4 = 88
    int top_y2 = rawdraw_layout_ink_centered_text_top_y(&mock_font, "g", 100, 2);
    assert(top_y2 == 88);

    // Fallback: empty text
    // center_y = 100, visual_offset = 0
    // baseline_y = rawdraw_layout_calc_baseline_y = (100 - 10) + 20 - 4 + 0 = 106
    // top_y = 106 - 20 + 4 = 90
    int top_y_fallback = rawdraw_layout_ink_centered_text_top_y(&mock_font, "", 100, 0);
    assert(top_y_fallback == 90);

    // 2. rawdraw_layout_ink_centered_text_top_y_in_box
    // box_top = 80, box_height = 40, visual_offset = 0
    // center_y = 80 + 20 = 100. Text "A" -> top_y = 92
    int top_y_box = rawdraw_layout_ink_centered_text_top_y_in_box(&mock_font, "A", 80, 40, 0);
    assert(top_y_box == 92);

    printf("Ink centered text positioning passed!\n");
}

int main(void)
{
    printf("=== RUNNING LAWDRAW LAYOUT TESTS ===\n");
    test_baseline_calculations();
    test_measure_text_ink_bounds();
    test_ink_centered_text();
    printf("=== ALL LAWDRAW LAYOUT TESTS PASSED ===\n");
    return 0;
}
