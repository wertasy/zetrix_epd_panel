/*
 * test_widgets_calendar.c — host tests for Calendar widget.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "rawdraw.h"
#include "rawdraw_ext.h"
#include "style.h"
#include "theme.h"
#include "font_engine.h"

#include "../components/rawdraw/widgets/calendar.h"
#include "../components/ui/include/page_renderer.h"

/* ---- framebuffer geometry ---- */
#define FB_WIDTH 400
#define FB_HEIGHT 300
#define BYTES_PER_ROW (((FB_WIDTH * 2) + 7) / 8)
#define FB_SIZE (BYTES_PER_ROW * FB_HEIGHT)

static uint8_t fb[FB_SIZE];

/* Define the extern font symbols declared in font_engine.h (linker safety). */
const lv_font_t SourceHanSansSC_Regular_slim;
const lv_font_t SourceHanSansSC_Medium_slim;
const lv_font_t font_zectrix_16_1;
const lv_font_t font_zectrix_48_1;
const lv_font_t weather_icons_16;
const lv_font_t weather_icons_48;

/* ---- mock font (8x16 glyphs, advance 12) ---- */
static bool mock_get_glyph_dsc(const struct _lv_font_t *font, lv_font_glyph_dsc_t *dsc_out, uint32_t letter,
                               uint32_t letter_next)
{
    (void)font;
    (void)letter;
    (void)letter_next;
    dsc_out->adv_w  = 12;
    dsc_out->box_w  = 8;
    dsc_out->box_h  = 16;
    dsc_out->ofs_x  = 0;
    dsc_out->ofs_y  = 0;
    dsc_out->format = LV_FONT_GLYPH_FORMAT_A1;
    return true;
}

static const uint8_t mock_bitmap[] = {0xFF, 0xFF, 0xFF, 0xFF};

static const void *mock_get_glyph_bitmap(lv_font_glyph_dsc_t *g_dsc, struct _lv_draw_buf_t *draw_buf)
{
    (void)g_dsc;
    (void)draw_buf;
    return mock_bitmap;
}

static const lv_font_t mock_font = {
    .get_glyph_dsc    = mock_get_glyph_dsc,
    .get_glyph_bitmap = mock_get_glyph_bitmap,
    .line_height      = 20,
    .base_line        = 4,
};

static int failures = 0;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            printf("  FAIL line %d: %s\n", __LINE__, #cond);                                                           \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

static void test_calendar_lunar(void)
{
    printf("[calendar_lunar]\n");

    /* test some known dates */
    widget_calendar_lunar_date_t d1 = widget_calendar_to_lunar_date(2026, 2, 17); /* Chinese New Year 2026 is Feb 17 */
    CHECK(d1.lunar_year == 2026);
    CHECK(d1.lunar_month == 1);
    CHECK(d1.lunar_day == 1);

    widget_calendar_lunar_date_t d2 = widget_calendar_to_lunar_date(2026, 1, 1);
    /* Before CNY 2026, belongs to lunar 2025 */
    CHECK(d2.lunar_year == 2025);

    char yr[16];
    widget_calendar_get_lunar_year_name(2026, yr, sizeof(yr));
    CHECK(strcmp(yr, "丙午") == 0); /* 2026 is Bing Wu (丙午) */

    char yr2[16];
    widget_calendar_get_lunar_year_name(d2.lunar_year, yr2, sizeof(yr2));
    CHECK(strcmp(yr2, "乙巳") == 0); /* 2025 is Yi Si (乙巳) */

    /* Test Month Names */
    CHECK(strcmp(widget_calendar_get_lunar_month_name(1), "正月") == 0);
    CHECK(strcmp(widget_calendar_get_lunar_month_name(12), "腊月") == 0);

    /* Test Day Names */
    CHECK(strcmp(widget_calendar_get_lunar_day_name(1), "初一") == 0);
    CHECK(strcmp(widget_calendar_get_lunar_day_name(30), "三十") == 0);

    /* Test Solar terms */
    CHECK(strcmp(widget_calendar_get_solar_term(2, 4), "立春") == 0);
    CHECK(strcmp(widget_calendar_get_solar_term(12, 22), "冬至") == 0);
}

static void test_calendar_state(void)
{
    printf("[calendar_state]\n");

    widget_calendar_t c;
    widget_calendar_init(&c, 0, 0, 400, 240);

    CHECK(c.year > 2000);
    CHECK(c.month >= 1 && c.month <= 12);
    CHECK(c.show_lunar == false);
    CHECK(c.show_overflow == false);
    CHECK(c.show_header == true);

    widget_calendar_set_date(&c, 2026, 4);
    CHECK(c.year == 2026);
    CHECK(c.month == 4);

    /* Navigation */
    widget_calendar_next_month(&c);
    CHECK(c.year == 2026 && c.month == 5);

    widget_calendar_prev_month(&c);
    CHECK(c.year == 2026 && c.month == 4);

    widget_calendar_set_date(&c, 2026, 12);
    widget_calendar_next_month(&c);
    CHECK(c.year == 2027 && c.month == 1);

    widget_calendar_prev_month(&c);
    CHECK(c.year == 2026 && c.month == 12);
}

static void test_calendar_selection(void)
{
    printf("[calendar_selection]\n");

    widget_calendar_t c;
    widget_calendar_init(&c, 0, 0, 400, 240);
    widget_calendar_set_date(&c, 2026, 4);

    CHECK(widget_calendar_in_selection_mode(&c) == false);

    widget_calendar_enter_selection_mode(&c);
    CHECK(widget_calendar_in_selection_mode(&c) == true);
    CHECK(widget_calendar_get_cursor_row(&c) >= 0);
    CHECK(widget_calendar_get_cursor_col(&c) >= 0);

    /* Move around */
    widget_calendar_navigate_selection(&c, 1); /* down */
    widget_calendar_navigate_selection(&c, -1); /* up */

    /* Month navigation should exit selection mode */
    widget_calendar_enter_selection_mode(&c);
    CHECK(widget_calendar_in_selection_mode(&c) == true);
    widget_calendar_next_month(&c);
    CHECK(widget_calendar_in_selection_mode(&c) == false);

    widget_calendar_enter_selection_mode(&c);
    CHECK(widget_calendar_in_selection_mode(&c) == true);
    widget_calendar_prev_month(&c);
    CHECK(widget_calendar_in_selection_mode(&c) == false);

    /* jump_to_today should exit selection mode and request full refresh */
    widget_calendar_set_date(&c, c.today_year, c.today_month);
    widget_calendar_enter_selection_mode(&c);
    CHECK(widget_calendar_in_selection_mode(&c) == true);
    c.needs_full_refresh = false;
    widget_calendar_jump_to_today(&c);
    CHECK(widget_calendar_in_selection_mode(&c) == false);
    CHECK(c.needs_full_refresh == true);

    /* direction == 0 guard test */
    widget_calendar_enter_selection_mode(&c);
    int row_before = widget_calendar_get_cursor_row(&c);
    int col_before = widget_calendar_get_cursor_col(&c);
    widget_calendar_navigate_selection(&c, 0);
    CHECK(widget_calendar_get_cursor_row(&c) == row_before);
    CHECK(widget_calendar_get_cursor_col(&c) == col_before);

    /* confirm selection */
    widget_calendar_confirm_selection(&c);
    CHECK(widget_calendar_in_selection_mode(&c) == false);
    CHECK(widget_calendar_get_selected_day(&c) > 0);

    /* test overflow cell handling (previous month) */
    widget_calendar_set_show_overflow_days(&c, true);
    widget_calendar_set_date(&c, 2026, 4);
    widget_calendar_enter_selection_mode(&c);
    c.sel_row = 0;
    c.sel_col = 2; /* Tuesday, March 31 */
    widget_calendar_confirm_selection(&c);
    CHECK(c.year == 2026);
    CHECK(c.month == 3);
    CHECK(widget_calendar_get_selected_day(&c) == 31);

    /* test overflow cell handling (next month) */
    widget_calendar_set_date(&c, 2026, 4);
    widget_calendar_enter_selection_mode(&c);
    c.sel_row = 4;
    c.sel_col = 6; /* Saturday, May 2 */
    widget_calendar_confirm_selection(&c);
    CHECK(c.year == 2026);
    CHECK(c.month == 5);
    CHECK(widget_calendar_get_selected_day(&c) == 2);
}

static void test_calendar_render(void)
{
    printf("[calendar_render]\n");

    widget_calendar_t c;
    widget_calendar_init(&c, 0, 0, 400, 240);
    widget_calendar_set_fonts(&c, &mock_font, &mock_font, &mock_font);

    rawdraw_clear(fb, FB_WIDTH, FB_HEIGHT, RAWDRAW_COLOR_WHITE);
    widget_calendar_render(&c, fb, FB_WIDTH, FB_HEIGHT);

    /* Verify we drew something non-white */
    int count = 0;
    for (int i = 0; i < FB_SIZE; i++) {
        if (fb[i] != 0x55)
            count++; /* 0x55 is packed RAWDRAW_COLOR_WHITE (all bits 01) */
    }
    CHECK(count > 0);
}

int main(void)
{
    printf("== calendar widget tests ==\n");

    test_calendar_lunar();
    test_calendar_state();
    test_calendar_selection();
    test_calendar_render();

    if (failures > 0) {
        printf("\n%d CHECK(s) FAILED\n", failures);
        return 1;
    }
    printf("\nALL TESTS PASSED\n");
    return 0;
}
