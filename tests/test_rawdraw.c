#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "rawdraw.h"
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

void test_pixel_operations(void)
{
    printf("Testing pixel operations...\n");

    // Clear to white
    rawdraw_clear(fb, FB_WIDTH, FB_HEIGHT, RAWDRAW_COLOR_WHITE);

    // Verify all pixels are white
    for (int y = 0; y < FB_HEIGHT; y++) {
        for (int x = 0; x < FB_WIDTH; x++) {
            assert(rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, x, y) == RAWDRAW_COLOR_WHITE);
        }
    }

    // Set some pixels
    rawdraw_set_pixel(fb, FB_WIDTH, FB_HEIGHT, 0, 0, RAWDRAW_COLOR_BLACK);
    rawdraw_set_pixel(fb, FB_WIDTH, FB_HEIGHT, 10, 20, RAWDRAW_COLOR_RED);
    rawdraw_set_pixel(fb, FB_WIDTH, FB_HEIGHT, FB_WIDTH - 1, FB_HEIGHT - 1, RAWDRAW_COLOR_YELLOW);

    // Verify
    assert(rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, 0, 0) == RAWDRAW_COLOR_BLACK);
    assert(rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, 10, 20) == RAWDRAW_COLOR_RED);
    assert(rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, FB_WIDTH - 1, FB_HEIGHT - 1) == RAWDRAW_COLOR_YELLOW);

    printf("Pixel operations passed!\n");
}

void test_basic_geometric_drawing(void)
{
    printf("Testing basic geometric drawing...\n");

    rawdraw_clear(fb, FB_WIDTH, FB_HEIGHT, RAWDRAW_COLOR_WHITE);

    // Horizontal line
    rawdraw_draw_hline(fb, FB_WIDTH, FB_HEIGHT, 50, 10, 20, RAWDRAW_COLOR_BLACK);
    for (int x = 10; x <= 20; x++) {
        assert(rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, x, 50) == RAWDRAW_COLOR_BLACK);
    }
    assert(rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, 9, 50) == RAWDRAW_COLOR_WHITE);
    assert(rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, 21, 50) == RAWDRAW_COLOR_WHITE);

    // Vertical line
    rawdraw_draw_vline(fb, FB_WIDTH, FB_HEIGHT, 30, 40, 50, RAWDRAW_COLOR_RED);
    for (int y = 40; y <= 50; y++) {
        assert(rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, 30, y) == RAWDRAW_COLOR_RED);
    }
    assert(rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, 30, 39) == RAWDRAW_COLOR_WHITE);
    assert(rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, 30, 51) == RAWDRAW_COLOR_WHITE);

    // Diagonal line
    rawdraw_point_t p1 = {10, 10};
    rawdraw_point_t p2 = {15, 15};
    rawdraw_draw_line(fb, FB_WIDTH, FB_HEIGHT, p1, p2, RAWDRAW_COLOR_YELLOW);
    for (int i = 0; i <= 5; i++) {
        assert(rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, 10 + i, 10 + i) == RAWDRAW_COLOR_YELLOW);
    }

    // Filled Rectangle
    rawdraw_rect_t rect = {100, 100, 20, 10};
    rawdraw_fill_rect(fb, FB_WIDTH, FB_HEIGHT, rect, RAWDRAW_COLOR_BLACK);
    for (int y = 100; y < 110; y++) {
        for (int x = 100; x < 120; x++) {
            assert(rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, x, y) == RAWDRAW_COLOR_BLACK);
        }
    }
    // Border Rectangle
    rawdraw_clear(fb, FB_WIDTH, FB_HEIGHT, RAWDRAW_COLOR_WHITE);
    rawdraw_rect_t border_rect = {50, 50, 10, 10};
    rawdraw_draw_rect_border(fb, FB_WIDTH, FB_HEIGHT, border_rect, 2, RAWDRAW_COLOR_RED);
    // Outer border should be colored
    for (int x = 50; x < 60; x++) {
        assert(rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, x, 50) == RAWDRAW_COLOR_RED);
        assert(rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, x, 51) == RAWDRAW_COLOR_RED);
        assert(rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, x, 58) == RAWDRAW_COLOR_RED);
        assert(rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, x, 59) == RAWDRAW_COLOR_RED);
    }
    // Inner area should be white
    assert(rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, 55, 55) == RAWDRAW_COLOR_WHITE);

    printf("Basic geometric drawing passed!\n");
}

void test_circle_drawing(void)
{
    printf("Testing circle drawing...\n");

    rawdraw_clear(fb, FB_WIDTH, FB_HEIGHT, RAWDRAW_COLOR_WHITE);

    rawdraw_point_t center = {200, 150};
    int             radius = 10;
    rawdraw_draw_circle(fb, FB_WIDTH, FB_HEIGHT, center, radius, RAWDRAW_COLOR_RED);

    // Check center pixel
    assert(rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, 200, 150) == RAWDRAW_COLOR_RED);
    // Check extreme points
    assert(rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, 200 + radius, 150) == RAWDRAW_COLOR_RED);
    assert(rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, 200 - radius, 150) == RAWDRAW_COLOR_RED);
    assert(rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, 200, 150 + radius) == RAWDRAW_COLOR_RED);
    assert(rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, 200, 150 - radius) == RAWDRAW_COLOR_RED);
    // Check outside circle
    assert(rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, 200 + radius + 2, 150) == RAWDRAW_COLOR_WHITE);

    // Circle border
    rawdraw_clear(fb, FB_WIDTH, FB_HEIGHT, RAWDRAW_COLOR_WHITE);
    rawdraw_draw_circle_border(fb, FB_WIDTH, FB_HEIGHT, center, radius, 2, RAWDRAW_COLOR_YELLOW);
    // Boundary check
    assert(rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, 200 + radius, 150) == RAWDRAW_COLOR_YELLOW);
    assert(rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, 200 + radius - 1, 150) == RAWDRAW_COLOR_YELLOW);
    // Center should be white
    assert(rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, 200, 150) == RAWDRAW_COLOR_WHITE);

    printf("Circle drawing passed!\n");
}

void test_rounded_rectangle(void)
{
    printf("Testing rounded rectangles...\n");

    rawdraw_clear(fb, FB_WIDTH, FB_HEIGHT, RAWDRAW_COLOR_WHITE);

    rawdraw_rect_t r = {10, 10, 30, 20};
    // Draw using standard rawdraw_draw_round_rect
    rawdraw_draw_round_rect(fb, FB_WIDTH, FB_HEIGHT, r.x, r.y, r.w, r.h, 5, RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_BLACK,
                            0);

    // Check inner pixel
    assert(rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, 25, 20) == RAWDRAW_COLOR_BLACK);
    // Check corner pixel (should be rounded off/white)
    assert(rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, 10, 10) == RAWDRAW_COLOR_WHITE);

    // Round rect border
    rawdraw_clear(fb, FB_WIDTH, FB_HEIGHT, RAWDRAW_COLOR_WHITE);
    rawdraw_draw_round_rect_border(fb, FB_WIDTH, FB_HEIGHT, r, 5, 2, RAWDRAW_COLOR_RED);
    assert(rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, 25, 10) == RAWDRAW_COLOR_RED); // Top border
    assert(rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, 25, 20) == RAWDRAW_COLOR_WHITE); // Inner

    printf("Rounded rectangles passed!\n");
}

void test_region_operations(void)
{
    printf("Testing region operations...\n");

    rawdraw_clear(fb, FB_WIDTH, FB_HEIGHT, RAWDRAW_COLOR_WHITE);

    rawdraw_rect_t r1 = {0, 0, 10, 10};
    rawdraw_fill_rect(fb, FB_WIDTH, FB_HEIGHT, r1, RAWDRAW_COLOR_BLACK);

    // Invert
    rawdraw_invert_region(fb, FB_WIDTH, FB_HEIGHT, r1);
    for (int y = 0; y < 10; y++) {
        for (int x = 0; x < 10; x++) {
            assert(rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, x, y) == RAWDRAW_COLOR_WHITE);
        }
    }

    // Copy region
    uint8_t dst_fb[FB_SIZE];
    rawdraw_clear(dst_fb, FB_WIDTH, FB_HEIGHT, RAWDRAW_COLOR_WHITE);
    rawdraw_clear(fb, FB_WIDTH, FB_HEIGHT, RAWDRAW_COLOR_WHITE);
    rawdraw_fill_rect(fb, FB_WIDTH, FB_HEIGHT, r1, RAWDRAW_COLOR_RED);

    rawdraw_copy_region(fb, dst_fb, FB_WIDTH, FB_HEIGHT, r1, 20, 20);
    for (int y = 20; y < 30; y++) {
        for (int x = 20; x < 30; x++) {
            assert(rawdraw_get_pixel(dst_fb, FB_WIDTH, FB_HEIGHT, x, y) == RAWDRAW_COLOR_RED);
        }
    }
    printf("Region operations passed!\n");
}
void test_blit_rotated_90(void)
{
    printf("Testing blit_rotated_90...\n");

    /* Case 1: 4x4 source (width divisible by 4 -> batched group loop).
     * Each source column x gets a distinct color, so rotation is verifiable. */
    enum {
        S1W = 4,
        S1H = 4
    };
    uint8_t                      src1[((S1W * 2 + 7) >> 3) * S1H];
    static const rawdraw_color_t col[4] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_RED, RAWDRAW_COLOR_YELLOW,
                                           RAWDRAW_COLOR_WHITE};
    rawdraw_clear(src1, S1W, S1H, RAWDRAW_COLOR_BLACK);
    for (int y = 0; y < S1H; y++)
        for (int x = 0; x < S1W; x++)
            rawdraw_set_pixel(src1, S1W, S1H, x, y, col[x]);

    rawdraw_clear(fb, FB_WIDTH, FB_HEIGHT, RAWDRAW_COLOR_WHITE);
    rawdraw_blit_rotated_90(src1, S1W, S1H, fb, FB_WIDTH, FB_HEIGHT, 10, 20);

    /* dst(10 + (4-1-y), 20 + x) == src1(x,y) == col[x] */
    for (int y = 0; y < S1H; y++)
        for (int x = 0; x < S1W; x++)
            assert(rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, 10 + (S1H - 1 - y), 20 + x) == col[x]);

    /* Case 2: 5x3 source (width NOT divisible by 4 -> remainder loop). */
    enum {
        S2W = 5,
        S2H = 3
    };
    uint8_t src2[((S2W * 2 + 7) >> 3) * S2H];
    rawdraw_clear(src2, S2W, S2H, RAWDRAW_COLOR_WHITE);
    for (int y = 0; y < S2H; y++)
        for (int x = 0; x < S2W; x++)
            rawdraw_set_pixel(src2, S2W, S2H, x, y, (rawdraw_color_t)((x + y) & 0x03));

    rawdraw_clear(fb, FB_WIDTH, FB_HEIGHT, RAWDRAW_COLOR_WHITE);
    rawdraw_blit_rotated_90(src2, S2W, S2H, fb, FB_WIDTH, FB_HEIGHT, 5, 7);

    for (int y = 0; y < S2H; y++)
        for (int x = 0; x < S2W; x++)
            assert(rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, 5 + (S2H - 1 - y), 7 + x) ==
                   (rawdraw_color_t)((x + y) & 0x03));

    /* Case 3: horizontal clipping (dst_x near right edge). A 4x4 source at
     * FB_WIDTH-2 means only source rows with dx in [0,FB_WIDTH) land on-screen. */
    rawdraw_clear(fb, FB_WIDTH, FB_HEIGHT, RAWDRAW_COLOR_WHITE);
    rawdraw_blit_rotated_90(src1, S1W, S1H, fb, FB_WIDTH, FB_HEIGHT, FB_WIDTH - 2, 0);
    for (int sy = 0; sy < S1H; sy++) {
        int dx = (FB_WIDTH - 2) + (S1H - 1 - sy);
        if (dx < 0 || dx >= FB_WIDTH)
            continue;
        for (int sx = 0; sx < S1W; sx++)
            assert(rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, dx, sx) == col[sx]);
    }
    /* Off-screen columns must not bleed into the cleared area to their left. */
    assert(rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, FB_WIDTH - 3, 0) == RAWDRAW_COLOR_WHITE);

    /* Case 4: vertical clipping (dst_y near bottom edge). */
    rawdraw_clear(fb, FB_WIDTH, FB_HEIGHT, RAWDRAW_COLOR_WHITE);
    rawdraw_blit_rotated_90(src1, S1W, S1H, fb, FB_WIDTH, FB_HEIGHT, 0, FB_HEIGHT - 2);
    for (int sy = 0; sy < S1H; sy++) {
        int dx = S1H - 1 - sy;
        for (int sx = 0; sx < 2; sx++) /* only sx in {0,1} land (dy 298,299) */
            assert(rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, dx, (FB_HEIGHT - 2) + sx) == col[sx]);
    }

    printf("blit_rotated_90 passed!\n");
}

void test_progress_bars(void)
{
    printf("Testing progress bars...\n");

    rawdraw_clear(fb, FB_WIDTH, FB_HEIGHT, RAWDRAW_COLOR_WHITE);

    rawdraw_rect_t r = {50, 50, 100, 20};
    rawdraw_draw_progress(fb, FB_WIDTH, FB_HEIGHT, r, 50, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_BLACK, 5);

    // Left half should have progress (BLACK)
    assert(rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, 60, 60) == RAWDRAW_COLOR_BLACK);
    // Right half should be empty (WHITE)
    assert(rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, 140, 60) == RAWDRAW_COLOR_WHITE);

    // Progress with label
    rawdraw_clear(fb, FB_WIDTH, FB_HEIGHT, RAWDRAW_COLOR_WHITE);
    rawdraw_draw_progress_with_label(fb, FB_WIDTH, FB_HEIGHT, 50, 50, 100, 20, 50, "50%", &mock_font);
    // Center of label
    // Since label is centered, it will render text at x ~ 92
    // Let's verify text got drawn
    bool text_drawn = false;
    for (int y = 50; y < 70; y++) {
        for (int x = 50; x < 150; x++) {
            if (rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, x, y) == RAWDRAW_COLOR_WHITE) {
                // If it is white on the left half (which is filled with black progress), it must be text!
                if (x < 100) {
                    text_drawn = true;
                    break;
                }
            }
        }
    }
    assert(text_drawn);

    printf("Progress bars passed!\n");
}

void test_text_utilities(void)
{
    printf("Testing text utilities...\n");

    // Text measurement
    int width = rawdraw_measure_text_width("Hello", &mock_font);
    // Standard mock font has adv_w = 8.
    // ASCII check: ch >= 0x20 && ch <= 0x7E -> prop_adv = g.box_w + g.ofs_x + letter_spacing.
    // box_w = 6, ofs_x = 1, letter_spacing = (8 + 8)/16 = 1.
    // prop_adv = 6 + 1 + 1 = 8.
    // 5 chars * 8 = 40.
    assert(width == 40);

    int height = rawdraw_measure_text_height(&mock_font);
    assert(height == 8);

    rawdraw_rect_t bounds = rawdraw_measure_text_bounds("Hello\nWorld", &mock_font, 0);
    assert(bounds.w == 40);
    assert(bounds.h == 16);

    // Test empty and NULL text
    rawdraw_rect_t bounds_null = rawdraw_measure_text_bounds(NULL, &mock_font, 0);
    assert(bounds_null.x == 0 && bounds_null.y == 0 && bounds_null.w == 0 && bounds_null.h == 0);
    rawdraw_rect_t bounds_empty = rawdraw_measure_text_bounds("", &mock_font, 0);
    assert(bounds_empty.x == 0 && bounds_empty.y == 0 && bounds_empty.w == 0 && bounds_empty.h == 0);
    // Draw text
    rawdraw_clear(fb, FB_WIDTH, FB_HEIGHT, RAWDRAW_COLOR_WHITE);
    rawdraw_draw_text(fb, FB_WIDTH, FB_HEIGHT, 10, 10, "Hi", &mock_font, RAWDRAW_COLOR_BLACK);
    // Verify some pixels are drawn
    bool pixel_drawn = false;
    for (int y = 10; y < 20; y++) {
        for (int x = 10; x < 30; x++) {
            if (rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, x, y) == RAWDRAW_COLOR_BLACK) {
                pixel_drawn = true;
                break;
            }
        }
    }
    assert(pixel_drawn);

    printf("Text utilities passed!\n");
}

void test_stripe_rect(void)
{
    printf("Testing stripe rect...\n");

    rawdraw_clear(fb, FB_WIDTH, FB_HEIGHT, RAWDRAW_COLOR_WHITE);

    rawdraw_rect_t r = {20, 20, 10, 10};
    rawdraw_draw_stripe_rect(fb, FB_WIDTH, FB_HEIGHT, r);

    // Check alternating lines
    for (int y = 20; y < 30; y++) {
        rawdraw_color_t color = ((y - 20) & 1) ? RAWDRAW_COLOR_WHITE : RAWDRAW_COLOR_BLACK;
        for (int x = 20; x < 30; x++) {
            assert(rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, x, y) == color);
        }
    }

    printf("Stripe rect passed!\n");
}

int main(void)
{
    printf("Starting rawdraw unit tests...\n");

    test_pixel_operations();
    test_basic_geometric_drawing();
    test_circle_drawing();
    test_rounded_rectangle();
    test_region_operations();
    test_blit_rotated_90();
    test_progress_bars();
    test_text_utilities();
    test_stripe_rect();

    printf("All rawdraw tests successfully completed!\n");
    return 0;
}
