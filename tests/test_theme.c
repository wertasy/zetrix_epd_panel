#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "theme.h"
#include "rawdraw_ext.h"

// Define dummy fonts for the declarations in font_engine.h
const lv_font_t SourceHanSansSC_Regular_slim;
const lv_font_t SourceHanSansSC_Medium_slim;
const lv_font_t font_zectrix_16_1;
const lv_font_t font_zectrix_48_1;
const lv_font_t weather_icons_16;
const lv_font_t weather_icons_48;

// Define a mock font for testing
static bool mock_get_glyph_dsc(const struct _lv_font_t *font, lv_font_glyph_dsc_t *dsc_out, uint32_t letter,
                               uint32_t letter_next)
{
    dsc_out->resolved_font = font;
    dsc_out->adv_w         = 8;
    dsc_out->box_w         = 6;
    dsc_out->box_h         = 6;
    dsc_out->ofs_x         = 1;
    dsc_out->ofs_y         = 1;
    dsc_out->stride        = 1;
    dsc_out->format        = LV_FONT_GLYPH_FORMAT_A1;
    return true;
}

static const uint8_t mock_bitmap[] = {
    0xFC, // 11111100
    0x84, // 10000100
    0x84, // 10000100
    0x84, // 10000100
    0x84, // 10000100
    0xFC // 11111100
};

static const void *mock_get_glyph_bitmap(lv_font_glyph_dsc_t *g_dsc, struct _lv_draw_buf_t *draw_buf)
{
    return mock_bitmap;
}

const lv_font_t mock_font = {.get_glyph_dsc    = mock_get_glyph_dsc,
                             .get_glyph_bitmap = mock_get_glyph_bitmap,
                             .release_glyph    = NULL,
                             .line_height      = 8,
                             .base_line        = 1};

#define FB_WIDTH 400
#define FB_HEIGHT 300
#define FB_SIZE (((FB_WIDTH * 2 + 7) >> 3) * FB_HEIGHT)

static uint8_t fb[FB_SIZE];

void test_theme_metadata(void)
{
    printf("Running test_theme_metadata...\n");
    // Verify theme count
    int count = rawdraw_theme_count();
    assert(count == 6);

    // Verify rawdraw_theme_at and keys/names
    for (int i = 0; i < count; i++) {
        rawdraw_theme_id_t id   = rawdraw_theme_at(i);
        const char        *key  = rawdraw_theme_key(id);
        const char        *name = rawdraw_theme_display_name(id);
        assert(key != NULL);
        assert(name != NULL);

        // Find theme from key
        rawdraw_theme_id_t found = rawdraw_theme_from_key(key, THEME_INDUSTRIAL);
        assert(found == id);
    }

    // Verify fallback from key
    rawdraw_theme_id_t fallback = rawdraw_theme_from_key("invalid_key", THEME_CANDY_POP);
    assert(fallback == THEME_CANDY_POP);

    // Verify current theme initial value
    assert(rawdraw_theme_current_id() == THEME_INDUSTRIAL);

    // Set theme and verify current changed
    assert(rawdraw_theme_set(THEME_BRIGHT_LEMON) == true);
    assert(rawdraw_theme_current_id() == THEME_BRIGHT_LEMON);
    assert(strcmp(rawdraw_theme_current()->display_name, "Bright Lemon") == 0);

    assert(rawdraw_theme_set_by_key("candy_pop") == true);
    assert(rawdraw_theme_current_id() == THEME_CANDY_POP);

    // Restore to INDUSTRIAL
    assert(rawdraw_theme_set(THEME_INDUSTRIAL) == true);
    printf("test_theme_metadata passed!\n");
}

void test_paint_and_styles(void)
{
    printf("Running test_paint_and_styles...\n");
    // Verify rawdraw_make_paint
    rawdraw_paint_style_t paint = rawdraw_make_paint(RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_RED,
                                                     DITHER_PEACH, 3, REFRESH_AVOID_FREQUENT);
    assert(paint.fg == RAWDRAW_COLOR_BLACK);
    assert(paint.bg == RAWDRAW_COLOR_WHITE);
    assert(paint.border == RAWDRAW_COLOR_RED);
    assert(paint.dither == DITHER_PEACH);
    assert(paint.border_width == 3);
    assert(paint.invert_text == false);
    assert(paint.refresh_cost == REFRESH_AVOID_FREQUENT);

    // Verify theme style retrieval
    rawdraw_paint_style_t text_style = rawdraw_theme_style(THEME_TOKEN_TEXT_PRIMARY);
    assert(text_style.fg == RAWDRAW_COLOR_BLACK);

    // Verify theme component retrieval
    rawdraw_paint_style_t comp_style = rawdraw_theme_component(ROLE_BUTTON_SELECTED);
    rawdraw_paint_style_t expected   = rawdraw_theme_style(THEME_TOKEN_SELECTED);
    assert(comp_style.fg == expected.fg);
    assert(comp_style.bg == expected.bg);

    // Verify color for token
    rawdraw_color_t color = rawdraw_theme_color_for(THEME_TOKEN_ACCENT);
    assert(color == rawdraw_theme_style(THEME_TOKEN_ACCENT).fg);
    printf("test_paint_and_styles passed!\n");
}

void test_dithering(void)
{
    printf("Running test_dithering...\n");
    memset(fb, 0x55, FB_SIZE); // Fill with white

    // Draw a rect with dither
    rawdraw_paint_style_t style = rawdraw_make_paint(RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_BLACK,
                                                     DITHER_GRAY, 0, REFRESH_STATIC_SAFE);
    rawdraw_rect_t        r     = {10, 10, 10, 10};
    rawdraw_draw_styled_rect(fb, FB_WIDTH, FB_HEIGHT, r, &style);

    // DITHER_GRAY checks: return ((x & 1) == 0) && ((y & 1) == 0) -> on ? fg : bg
    for (int y = 10; y < 20; y++) {
        for (int x = 10; x < 20; x++) {
            rawdraw_color_t expected;
            if (((x & 1) == 0) && ((y & 1) == 0)) {
                expected = RAWDRAW_COLOR_BLACK;
            } else {
                expected = RAWDRAW_COLOR_WHITE;
            }
            rawdraw_color_t actual = rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, x, y);
            assert(actual == expected);
        }
    }

#if !CONFIG_ZECTRIX_EPD_PANEL_1BPP
    // Let's test DITHER_ORANGE: return ((x & 3) != 0) || ((y & 3) != 0) -> on ? YELLOW : RED
    rawdraw_paint_style_t style_orange = rawdraw_make_paint(RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_WHITE,
                                                            RAWDRAW_COLOR_BLACK, DITHER_ORANGE, 0, REFRESH_STATIC_SAFE);
    memset(fb, 0x55, FB_SIZE);
    rawdraw_draw_styled_rect(fb, FB_WIDTH, FB_HEIGHT, r, &style_orange);
    for (int y = 10; y < 20; y++) {
        for (int x = 10; x < 20; x++) {
            bool            on       = ((x & 3) != 0) || ((y & 3) != 0);
            rawdraw_color_t expected = on ? RAWDRAW_COLOR_YELLOW : RAWDRAW_COLOR_RED;
            rawdraw_color_t actual   = rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, x, y);
            assert(actual == expected);
        }
    }
#endif

    printf("test_dithering passed!\n");
}

void test_styled_drawings(void)
{
    printf("Running test_styled_drawings...\n");

    // Clear FB
    memset(fb, 0x55, FB_SIZE);

    rawdraw_paint_style_t style = rawdraw_make_paint(RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_RED,
                                                     DITHER_NONE, 2, REFRESH_STATIC_SAFE);

    // 1. Draw styled round rect
    rawdraw_rect_t r_round = {20, 20, 50, 40};
    rawdraw_draw_styled_round_rect(fb, FB_WIDTH, FB_HEIGHT, r_round, 5, &style);

    // Check border pixel (border width is 2, border color is RED)
    rawdraw_color_t edge_pixel = rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, 25, 20);
#if CONFIG_ZECTRIX_EPD_PANEL_1BPP
    assert(edge_pixel == RAWDRAW_COLOR_BLACK);
#else
    assert(edge_pixel == RAWDRAW_COLOR_RED);
#endif

    // 2. Draw styled border
    rawdraw_rect_t r_border = {100, 20, 50, 40};
    rawdraw_draw_styled_border(fb, FB_WIDTH, FB_HEIGHT, r_border, &style);
#if CONFIG_ZECTRIX_EPD_PANEL_1BPP
    assert(rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, 100, 20) == RAWDRAW_COLOR_BLACK);
#else
    assert(rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, 100, 20) == RAWDRAW_COLOR_RED);
#endif

    // 3. Draw styled text
    rawdraw_draw_styled_text(fb, FB_WIDTH, FB_HEIGHT, 10, 100, "Hello", &mock_font, &style);

    // 4. Draw styled icon
    rawdraw_draw_styled_icon(fb, FB_WIDTH, FB_HEIGHT, 100, 100, "A", &mock_font, &style);

    // 5. Draw styled progress
    rawdraw_rect_t r_progress = {10, 200, 200, 15};
    rawdraw_draw_styled_progress(fb, FB_WIDTH, FB_HEIGHT, r_progress, 75, &style, 3);

    // Progress fill area should be drawn with fg (BLACK), background with bg (WHITE)
    // Verify progress fill
    assert(rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, 15, 205) == RAWDRAW_COLOR_BLACK);
    // Verify progress unfilled background
    assert(rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, 180, 205) == RAWDRAW_COLOR_WHITE);

    printf("test_styled_drawings passed!\n");
}

void test_dithered_round_rect_bleed(void)
{
    printf("Running test_dithered_round_rect_bleed...\n");
    // Clear FB to WHITE
    memset(fb, 0x55, FB_SIZE);

    // Make a paint with border width 2, border color BLACK, bg color WHITE, fg color BLACK, dither DITHER_GRAY
    rawdraw_paint_style_t style = rawdraw_make_paint(RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_BLACK,
                                                     DITHER_GRAY, 2, REFRESH_STATIC_SAFE);

    // Draw styled round rect at x=10, y=10, w=20, h=20, radius=5
    rawdraw_rect_t r = {10, 10, 20, 20};
    rawdraw_draw_styled_round_rect(fb, FB_WIDTH, FB_HEIGHT, r, 5, &style);

    // Check pixel at (12, 13)
    // This pixel is on the border corner, so it must be RAWDRAW_COLOR_BLACK.
    // Under the old code, it got overwritten with RAWDRAW_COLOR_WHITE.
    rawdraw_color_t pixel_12_13 = rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, 12, 13);
    assert(pixel_12_13 == RAWDRAW_COLOR_BLACK);

    printf("test_dithered_round_rect_bleed passed!\n");
}

void test_theme_1bpp_normalization(void)
{
#if CONFIG_ZECTRIX_EPD_PANEL_1BPP
    printf("Running test_theme_1bpp_normalization...\n");
    // Switch to CONSOLE theme
    assert(rawdraw_theme_set(THEME_CONSOLE) == true);

    // Check TEXT_PRIMARY: fg should be WHITE, bg should be BLACK
    rawdraw_paint_style_t primary = rawdraw_theme_style(THEME_TOKEN_TEXT_PRIMARY);
    assert(primary.bg == RAWDRAW_COLOR_BLACK);
    assert(primary.fg == RAWDRAW_COLOR_WHITE);

    // Check TEXT_SECONDARY: fg should be WHITE (mapped from YELLOW), bg should be BLACK
    rawdraw_paint_style_t secondary = rawdraw_theme_style(THEME_TOKEN_TEXT_SECONDARY);
    assert(secondary.bg == RAWDRAW_COLOR_BLACK);
    assert(secondary.fg == RAWDRAW_COLOR_WHITE);

    // Check ACCENT: fg should be BLACK, bg should be WHITE (mapped from YELLOW)
    rawdraw_paint_style_t accent = rawdraw_theme_style(THEME_TOKEN_ACCENT);
    assert(accent.bg == RAWDRAW_COLOR_WHITE);
    assert(accent.fg == RAWDRAW_COLOR_BLACK);

    // Switch back to INDUSTRIAL theme
    assert(rawdraw_theme_set(THEME_INDUSTRIAL) == true);
    printf("test_theme_1bpp_normalization passed!\n");
#else
    printf("Skipping test_theme_1bpp_normalization (CONFIG_ZECTRIX_EPD_PANEL_1BPP not defined)\n");
#endif
}

int main(void)
{
    printf("Starting theme tests...\n");
    test_theme_metadata();
    test_paint_and_styles();
    test_dithering();
    test_styled_drawings();
    test_dithered_round_rect_bleed();
    test_theme_1bpp_normalization();
    printf("All theme tests passed successfully!\n");
    return 0;
}
