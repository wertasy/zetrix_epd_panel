/*
 * test_widgets_intermediate.c — host tests for ListItem / ProgressBar / ScrollView.
 *
 * Builds on Linux with gcc (see verification command). Verifies init defaults,
 * state changes, scroll math, input handling, and that render() actually writes
 * deterministic pixels into the 2bpp framebuffer.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "rawdraw.h"
#include "rawdraw_ext.h"
#include "style.h"
#include "font_engine.h"

#include "../components/rawdraw/widgets/list_item.h"
#include "../components/rawdraw/widgets/progress_bar.h"
#include "../components/rawdraw/widgets/scrollview.h"
#include "../components/ui/include/page_renderer.h"

/* ---- framebuffer geometry (2bpp packed: 4 pixels per byte) ---- */
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

/* ---- helpers ---- */
static rawdraw_color_t pix(int x, int y)
{
    return rawdraw_get_pixel(fb, FB_WIDTH, FB_HEIGHT, x, y);
}

static void reset_fb(void)
{
    rawdraw_clear(fb, FB_WIDTH, FB_HEIGHT, RAWDRAW_COLOR_WHITE);
}

static int failures = 0;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            printf("  FAIL line %d: %s\n", __LINE__, #cond);                                                           \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

static bool rect_eq(rawdraw_rect_t a, rawdraw_rect_t b)
{
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

/* ============================================================
 * ListItem
 * ============================================================ */
static void item_cb(void *ud);

static void test_list_item(void)
{
    printf("[list_item]\n");

    /* init defaults */
    widget_list_item_t item;
    widget_list_item_init(&item, 10, 20, 200, 36);
    CHECK(rect_eq(widget_list_item_get_bounds(&item), (rawdraw_rect_t){10, 20, 200, 36}));
    CHECK(item.padding == STYLE_LIST_ITEM_PADDING);
    CHECK(item.show_chevron == false);
    CHECK(item.show_separator == true);
    CHECK(widget_list_item_is_pressed(&item) == false);
    CHECK(item.bg_color == RAWDRAW_COLOR_WHITE);
    CHECK(item.label[0] == '\0');
    CHECK(item.value[0] == '\0');
    CHECK(item.icon_code[0] == '\0');

    /* contains: inclusive top-left, exclusive bottom-right */
    CHECK(widget_list_item_contains(&item, 10, 20) == true);
    CHECK(widget_list_item_contains(&item, 209, 55) == true);
    CHECK(widget_list_item_contains(&item, 210, 56) == false);
    CHECK(widget_list_item_contains(&item, 0, 0) == false);

    /* set label / value / icon */
    widget_list_item_set_label(&item, "Brightness");
    CHECK(strcmp(item.label, "Brightness") == 0);
    widget_list_item_set_label_font(&item, &mock_font);
    CHECK(item.label_font == &mock_font);
    widget_list_item_set_value(&item, "80%");
    CHECK(strcmp(item.value, "80%") == 0);
    widget_list_item_set_value_font(&item, &mock_font);
    widget_list_item_set_icon(&item, "A");
    CHECK(strcmp(item.icon_code, "A") == 0);
    widget_list_item_set_icon_font(&item, &mock_font);

    /* NULL label clears */
    widget_list_item_set_label(&item, NULL);
    CHECK(item.label[0] == '\0');

    /* show flags */
    widget_list_item_set_show_chevron(&item, true);
    CHECK(item.show_chevron == true);
    widget_list_item_set_show_chevron(&item, false);
    CHECK(item.show_chevron == false);
    widget_list_item_set_show_separator(&item, false);
    CHECK(item.show_separator == false);
    widget_list_item_set_show_separator(&item, true);
    CHECK(item.show_separator == true);

    /* pressed toggle */
    widget_list_item_set_pressed(&item, true);
    CHECK(widget_list_item_is_pressed(&item) == true);
    widget_list_item_set_pressed(&item, false);
    CHECK(widget_list_item_is_pressed(&item) == false);

    /* handle_tap sets pressed + invokes callback */
    int counter = 0;
    widget_list_item_init(&item, 10, 10, 200, 36);
    widget_list_item_set_callback(&item, item_cb, &counter);
    widget_list_item_handle_tap(&item);
    CHECK(widget_list_item_is_pressed(&item) == true);
    CHECK(counter == 1);

    /* handle_input: BOOT click activates, other events do not */
    counter = 0;
    widget_list_item_set_pressed(&item, false);
    button_event_t boot = {BTN_BOOT_CLICK};
    CHECK(widget_list_item_handle_input(&item, &boot) == true);
    CHECK(counter == 1);
    CHECK(widget_list_item_is_pressed(&item) == true);

    widget_list_item_set_pressed(&item, false);
    button_event_t up = {BTN_UP_CLICK};
    CHECK(widget_list_item_handle_input(&item, &up) == false);
    CHECK(widget_list_item_is_pressed(&item) == false);

    /* NULL safety */
    CHECK(widget_list_item_handle_input(NULL, &boot) == false);
    CHECK(widget_list_item_handle_input(&item, NULL) == false);

    /* render with custom colors (bg BLACK): background fill must be BLACK */
    reset_fb();
    widget_list_item_init(&item, 10, 10, 200, 36);
    widget_list_item_set_colors(&item, RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_WHITE,
                                RAWDRAW_COLOR_WHITE);
    widget_list_item_render(&item, fb, FB_WIDTH, FB_HEIGHT);
    CHECK(pix(100, 25) == RAWDRAW_COLOR_BLACK); /* interior */

    /* render: separator line drawn at bottom row */
    reset_fb();
    widget_list_item_init(&item, 10, 10, 200, 36);
    widget_list_item_set_colors(&item, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_BLACK,
                                RAWDRAW_COLOR_BLACK);
    widget_list_item_set_show_separator(&item, true);
    widget_list_item_render(&item, fb, FB_WIDTH, FB_HEIGHT);
    /* separator at y = 10 + 36 - 1 = 45 */
    CHECK(pix(50, 45) == RAWDRAW_COLOR_BLACK);
    /* just above separator should still be WHITE (bg) */
    CHECK(pix(50, 44) == RAWDRAW_COLOR_WHITE);

    /* render: separator disabled -> no separator */
    reset_fb();
    widget_list_item_set_show_separator(&item, false);
    widget_list_item_render(&item, fb, FB_WIDTH, FB_HEIGHT);
    CHECK(pix(50, 45) == RAWDRAW_COLOR_WHITE);

    /* render with label + value + chevron must not crash and paint something */
    reset_fb();
    widget_list_item_init(&item, 10, 10, 200, 36);
    widget_list_item_set_label(&item, "Test");
    widget_list_item_set_label_font(&item, &mock_font);
    widget_list_item_set_value(&item, "42");
    widget_list_item_set_value_font(&item, &mock_font);
    widget_list_item_set_show_chevron(&item, true);
    widget_list_item_set_colors(&item, RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_WHITE,
                                RAWDRAW_COLOR_WHITE);
    widget_list_item_render(&item, fb, FB_WIDTH, FB_HEIGHT);
    int drew = 0;
    for (int y = 10; y < 46 && !drew; y++)
        for (int x = 10; x < 210 && !drew; x++)
            if (pix(x, y) != RAWDRAW_COLOR_WHITE)
                drew = 1;
    CHECK(drew == 1);

    /* fully off-screen item renders nothing (no crash) */
    reset_fb();
    widget_list_item_init(&item, -100, -100, 10, 10);
    widget_list_item_render(&item, fb, FB_WIDTH, FB_HEIGHT);

    printf("  ok\n");
}

static void item_cb(void *ud)
{
    (*(int *)ud)++;
}

/* ============================================================
 * ProgressBar
 * ============================================================ */
static void test_progress_bar(void)
{
    printf("[progress_bar]\n");

    /* init defaults */
    widget_progress_bar_t bar;
    widget_progress_bar_init(&bar, 10, 20, 100, 8);
    CHECK(rect_eq(widget_progress_bar_get_bounds(&bar), (rawdraw_rect_t){10, 20, 100, 8}));
    CHECK(widget_progress_bar_get_value(&bar) == 0);
    CHECK(bar.radius == STYLE_PROGRESS_RADIUS);
    CHECK(bar.bg_color == RAWDRAW_COLOR_WHITE);
    CHECK(bar.fg_color == RAWDRAW_COLOR_BLACK);
    CHECK(bar.label[0] == '\0');

    /* value clamping */
    widget_progress_bar_set_value(&bar, 50);
    CHECK(widget_progress_bar_get_value(&bar) == 50);
    widget_progress_bar_set_value(&bar, -10);
    CHECK(widget_progress_bar_get_value(&bar) == 0);
    widget_progress_bar_set_value(&bar, 150);
    CHECK(widget_progress_bar_get_value(&bar) == 100);

    /* set label / radius */
    widget_progress_bar_set_label(&bar, "75%");
    CHECK(strcmp(bar.label, "75%") == 0);
    widget_progress_bar_set_label(&bar, NULL);
    CHECK(bar.label[0] == '\0');
    widget_progress_bar_set_radius(&bar, 2);
    CHECK(bar.radius == 2);

    /* set colors */
    widget_progress_bar_set_bg_color(&bar, RAWDRAW_COLOR_YELLOW);
    widget_progress_bar_set_fg_color(&bar, RAWDRAW_COLOR_RED);
    CHECK(bar.bg_color == RAWDRAW_COLOR_YELLOW);
    /* render: use non-default colors (bg!=WHITE) to force custom-color branch.
     * rawdraw_draw_progress fills full rect with bg, then overlays left portion
     * with fg.  So: 0% -> all bg, 100% -> all fg, 50% -> left fg, right bg. */
    reset_fb();
    widget_progress_bar_init(&bar, 10, 10, 100, 20);
    widget_progress_bar_set_bg_color(&bar, RAWDRAW_COLOR_YELLOW);
    widget_progress_bar_set_fg_color(&bar, RAWDRAW_COLOR_BLACK);
    widget_progress_bar_set_value(&bar, 0);
    widget_progress_bar_render(&bar, fb, FB_WIDTH, FB_HEIGHT);
    CHECK(pix(60, 20) == RAWDRAW_COLOR_YELLOW); /* entire bar = bg */

    /* 100% -> entire bar is fg */
    reset_fb();
    widget_progress_bar_set_value(&bar, 100);
    widget_progress_bar_render(&bar, fb, FB_WIDTH, FB_HEIGHT);
    CHECK(pix(60, 20) == RAWDRAW_COLOR_BLACK);

    /* 50% -> left half fg (BLACK), right half bg (YELLOW) */
    reset_fb();
    widget_progress_bar_set_value(&bar, 50);
    widget_progress_bar_render(&bar, fb, FB_WIDTH, FB_HEIGHT);
    CHECK(pix(20, 20) == RAWDRAW_COLOR_BLACK); /* left side = fg */
    CHECK(pix(90, 20) == RAWDRAW_COLOR_YELLOW); /* right side = bg */
    reset_fb();
    widget_progress_bar_init(&bar, 10, 10, 100, 20);
    widget_progress_bar_set_label(&bar, "50%");
    widget_progress_bar_set_label_font(&bar, &mock_font);
    widget_progress_bar_set_value(&bar, 50);
    widget_progress_bar_render(&bar, fb, FB_WIDTH, FB_HEIGHT);

    /* off-screen bar renders nothing (no crash) */
    reset_fb();
    widget_progress_bar_init(&bar, -200, -200, 10, 10);
    widget_progress_bar_render(&bar, fb, FB_WIDTH, FB_HEIGHT);

    printf("  ok\n");
}

/* ============================================================
 * CircularGauge
 * ============================================================ */
static void test_circular_gauge(void)
{
    printf("[circular_gauge]\n");

    widget_circular_gauge_t gauge;
    widget_circular_gauge_init(&gauge, 200, 150, 60, 6);
    CHECK(gauge.cx == 200);
    CHECK(gauge.cy == 150);
    CHECK(gauge.radius == 60);
    CHECK(gauge.thickness == 6);
    CHECK(widget_circular_gauge_get_value(&gauge) == 0);
    CHECK(gauge.bg_color == RAWDRAW_COLOR_WHITE);
    CHECK(gauge.fg_color == RAWDRAW_COLOR_BLACK);

    /* bounds: 2*radius centered at (cx,cy) */
    CHECK(rect_eq(widget_circular_gauge_get_bounds(&gauge), (rawdraw_rect_t){140, 90, 120, 120}));

    /* value clamping */
    widget_circular_gauge_set_value(&gauge, 75);
    CHECK(widget_circular_gauge_get_value(&gauge) == 75);
    widget_circular_gauge_set_value(&gauge, -5);
    CHECK(widget_circular_gauge_get_value(&gauge) == 0);
    widget_circular_gauge_set_value(&gauge, 200);
    CHECK(widget_circular_gauge_get_value(&gauge) == 100);

    /* setters */
    widget_circular_gauge_set_center(&gauge, 100, 100);
    CHECK(gauge.cx == 100);
    CHECK(gauge.cy == 100);
    widget_circular_gauge_set_radius(&gauge, 40);
    CHECK(gauge.radius == 40);
    widget_circular_gauge_set_thickness(&gauge, 4);
    CHECK(gauge.thickness == 4);
    widget_circular_gauge_set_label(&gauge, "50%");
    CHECK(strcmp(gauge.label, "50%") == 0);
    widget_circular_gauge_set_label_font(&gauge, &mock_font);

    /* render with non-default custom colors: 0% -> entire ring is bg.
     * Use bg=YELLOW (not WHITE) to force the custom-color branch. */
    reset_fb();
    widget_circular_gauge_init(&gauge, 200, 150, 30, 6);
    widget_circular_gauge_set_bg_color(&gauge, RAWDRAW_COLOR_YELLOW);
    widget_circular_gauge_set_fg_color(&gauge, RAWDRAW_COLOR_BLACK);
    widget_circular_gauge_set_value(&gauge, 0);
    widget_circular_gauge_render(&gauge, fb, FB_WIDTH, FB_HEIGHT);
    /* pixel on ring at 12-o'clock (top): dist_sq = 30^2 = 900, on ring */
    CHECK(pix(200, 150 - 30) == RAWDRAW_COLOR_YELLOW); /* bg only, 0% fill */

    /* render 100% -> entire ring is fg */
    reset_fb();
    widget_circular_gauge_set_value(&gauge, 100);
    widget_circular_gauge_render(&gauge, fb, FB_WIDTH, FB_HEIGHT);
    CHECK(pix(200, 150 - 30) == RAWDRAW_COLOR_BLACK);

    /* render 25% -> top of ring is fg (fill starts at 12-o'clock) */
    reset_fb();
    widget_circular_gauge_set_value(&gauge, 25);
    widget_circular_gauge_render(&gauge, fb, FB_WIDTH, FB_HEIGHT);
    CHECK(pix(200, 150 - 30) == RAWDRAW_COLOR_BLACK);
    /* left side (9-o'clock, 270 degrees) should be bg when only 25% filled */
    CHECK(pix(200 - 30, 150) == RAWDRAW_COLOR_YELLOW);

    /* render with label must not crash */
    reset_fb();
    widget_circular_gauge_set_label(&gauge, "25%");
    widget_circular_gauge_render(&gauge, fb, FB_WIDTH, FB_HEIGHT);

    printf("  ok\n");
}

/* ============================================================
 * ScrollView
 * ============================================================ */

/* content callback that fills the clip area with a given color */
static rawdraw_color_t g_fill_color;
static int             g_cb_invocations;
static void content_fill_cb(uint8_t *fb, int width, int height, rawdraw_rect_t visible, rawdraw_rect_t clip, void *ud)
{
    (void)visible;
    (void)ud;
    rawdraw_fill_rect(fb, width, height, clip, g_fill_color);
    g_cb_invocations++;
}

static void test_scrollview(void)
{
    printf("[scrollview]\n");

    /* init defaults */
    widget_scrollview_t sv;
    widget_scrollview_init(&sv, 0, 0, 100, 100, 200);
    CHECK(rect_eq(widget_scrollview_get_bounds(&sv), (rawdraw_rect_t){0, 0, 100, 100}));
    CHECK(sv.content_height == 200);
    CHECK(sv.scroll_offset == 0);
    CHECK(sv.scrollbar_width == STYLE_SCROLLBAR_WIDTH);
    CHECK(sv.scrollbar_enabled == true);

    /* max scroll offset */
    CHECK(widget_scrollview_get_max_scroll_offset(&sv) == 100);

    /* content <= visible -> max offset = 0 */
    widget_scrollview_set_content_height(&sv, 50);
    CHECK(widget_scrollview_get_max_scroll_offset(&sv) == 0);

    widget_scrollview_set_content_height(&sv, 200);
    CHECK(widget_scrollview_get_max_scroll_offset(&sv) == 100);

    /* scroll offset clamping */
    widget_scrollview_set_scroll_offset(&sv, 50);
    CHECK(widget_scrollview_get_scroll_offset(&sv) == 50);
    widget_scrollview_set_scroll_offset(&sv, -10);
    CHECK(widget_scrollview_get_scroll_offset(&sv) == 0);
    widget_scrollview_set_scroll_offset(&sv, 500);
    CHECK(widget_scrollview_get_scroll_offset(&sv) == 100);

    /* scroll_to_end */
    widget_scrollview_set_scroll_offset(&sv, 0);
    widget_scrollview_scroll_to_end(&sv);
    CHECK(widget_scrollview_get_scroll_offset(&sv) == 100);

    /* scroll_by */
    widget_scrollview_set_scroll_offset(&sv, 0);
    widget_scrollview_scroll_by(&sv, 30);
    CHECK(widget_scrollview_get_scroll_offset(&sv) == 30);
    widget_scrollview_scroll_by(&sv, -10);
    CHECK(widget_scrollview_get_scroll_offset(&sv) == 20);
    widget_scrollview_scroll_by(&sv, 1000); /* clamp to max */
    CHECK(widget_scrollview_get_scroll_offset(&sv) == 100);
    widget_scrollview_scroll_by(&sv, -1000); /* clamp to 0 */
    CHECK(widget_scrollview_get_scroll_offset(&sv) == 0);

    /* can_scroll_up / can_scroll_down */
    widget_scrollview_set_scroll_offset(&sv, 0);
    CHECK(widget_scrollview_can_scroll_up(&sv) == false);
    CHECK(widget_scrollview_can_scroll_down(&sv) == true);
    widget_scrollview_set_scroll_offset(&sv, 50);
    CHECK(widget_scrollview_can_scroll_up(&sv) == true);
    CHECK(widget_scrollview_can_scroll_down(&sv) == true);
    widget_scrollview_scroll_to_end(&sv);
    CHECK(widget_scrollview_can_scroll_up(&sv) == true);
    CHECK(widget_scrollview_can_scroll_down(&sv) == false);

    /* content <= visible -> cannot scroll either way */
    widget_scrollview_set_content_height(&sv, 50);
    CHECK(widget_scrollview_can_scroll_up(&sv) == false);
    CHECK(widget_scrollview_can_scroll_down(&sv) == false);

    /* set_content_height re-clamps offset */
    widget_scrollview_set_content_height(&sv, 200);
    widget_scrollview_set_scroll_offset(&sv, 80);
    widget_scrollview_set_content_height(&sv, 100); /* max becomes 0 */
    CHECK(widget_scrollview_get_scroll_offset(&sv) == 0);

    /* set_bounds re-clamps offset */
    widget_scrollview_set_content_height(&sv, 200);
    widget_scrollview_set_scroll_offset(&sv, 80);
    widget_scrollview_set_bounds(&sv, 0, 0, 100, 200); /* max becomes 0 */
    CHECK(widget_scrollview_get_scroll_offset(&sv) == 0);

    /* visible content rect */
    widget_scrollview_set_bounds(&sv, 0, 0, 100, 100);
    widget_scrollview_set_content_height(&sv, 200);
    widget_scrollview_set_scroll_offset(&sv, 50);
    rawdraw_rect_t vis = widget_scrollview_get_visible_content_rect(&sv);
    CHECK(vis.x == 0);
    CHECK(vis.y == 50);
    CHECK(vis.w == 100 - STYLE_SCROLLBAR_WIDTH);
    CHECK(vis.h == 100);

    /* render with content callback: fills clip area */
    reset_fb();
    widget_scrollview_set_bounds(&sv, 10, 10, 100, 100);
    widget_scrollview_set_content_height(&sv, 200);
    widget_scrollview_set_scroll_offset(&sv, 0);
    g_fill_color     = RAWDRAW_COLOR_BLACK;
    g_cb_invocations = 0;
    widget_scrollview_render(&sv, fb, FB_WIDTH, FB_HEIGHT, content_fill_cb, NULL);
    CHECK(g_cb_invocations == 1);
    CHECK(pix(50, 50) == RAWDRAW_COLOR_BLACK); /* inside clip area */

    /* render with NULL callback (scrollbar only, no crash) */
    reset_fb();
    widget_scrollview_render(&sv, fb, FB_WIDTH, FB_HEIGHT, NULL, NULL);

    /* content fits -> no scrollbar drawn, callback still invoked */
    reset_fb();
    widget_scrollview_set_content_height(&sv, 50);
    g_fill_color     = RAWDRAW_COLOR_RED;
    g_cb_invocations = 0;
    widget_scrollview_render(&sv, fb, FB_WIDTH, FB_HEIGHT, content_fill_cb, NULL);
    CHECK(g_cb_invocations == 1);

    /* scrollbar disabled -> no scrollbar even when content overflows */
    reset_fb();
    widget_scrollview_set_content_height(&sv, 200);
    widget_scrollview_set_scrollbar_enabled(&sv, false);
    g_fill_color = RAWDRAW_COLOR_YELLOW;
    widget_scrollview_render(&sv, fb, FB_WIDTH, FB_HEIGHT, content_fill_cb, NULL);
    widget_scrollview_set_scrollbar_enabled(&sv, true);

    /* zero-area bounds -> render is a no-op */
    reset_fb();
    widget_scrollview_set_bounds(&sv, 0, 0, 0, 0);
    g_cb_invocations = 0;
    widget_scrollview_render(&sv, fb, FB_WIDTH, FB_HEIGHT, content_fill_cb, NULL);
    CHECK(g_cb_invocations == 0);

    printf("  ok\n");
}

/* ============================================================
 * Standalone circular progress primitive
 * ============================================================ */
static void test_circular_progress_standalone(void)
{
    printf("[circular_progress_standalone]\n");

    /* 0% -> bg ring only */
    reset_fb();
    rawdraw_point_t center = {200, 150};
    rawdraw_draw_circular_progress(fb, FB_WIDTH, FB_HEIGHT, center, 30, 6, 0, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_BLACK);
    CHECK(pix(200, 150 - 30) == RAWDRAW_COLOR_WHITE);

    /* 100% -> fg ring */
    reset_fb();
    rawdraw_draw_circular_progress(fb, FB_WIDTH, FB_HEIGHT, center, 30, 6, 100, RAWDRAW_COLOR_WHITE,
                                   RAWDRAW_COLOR_BLACK);
    CHECK(pix(200, 150 - 30) == RAWDRAW_COLOR_BLACK);

    /* 50% -> top is fg, left side (9-o'clock) is bg.
     * At 50%, foreground covers 12-o'clock clockwise through 6-o'clock
     * (the right half).  The 6-o'clock boundary is inclusive in fg,
     * so we test 9-o'clock (clearly in the left/bg half). */
    reset_fb();
    rawdraw_draw_circular_progress(fb, FB_WIDTH, FB_HEIGHT, center, 30, 6, 50, RAWDRAW_COLOR_WHITE,
                                   RAWDRAW_COLOR_BLACK);
    CHECK(pix(200, 150 - 30) == RAWDRAW_COLOR_BLACK); /* 12-o'clock = fg */
    CHECK(pix(200 - 30, 150) == RAWDRAW_COLOR_WHITE); /* 9-o'clock = bg  */

    /* with label (no crash) */
    reset_fb();
    rawdraw_draw_circular_progress_with_label(fb, FB_WIDTH, FB_HEIGHT, center, 30, 6, 75, "75%", &mock_font);

    /* invalid radius/thickness -> no-op */
    reset_fb();
    rawdraw_draw_circular_progress(fb, FB_WIDTH, FB_HEIGHT, center, 0, 6, 50, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_BLACK);
    rawdraw_draw_circular_progress(fb, FB_WIDTH, FB_HEIGHT, center, 30, 0, 50, RAWDRAW_COLOR_WHITE,
                                   RAWDRAW_COLOR_BLACK);

    printf("  ok\n");
}

int main(void)
{
    printf("== widgets intermediate tests ==\n");
    test_list_item();
    test_progress_bar();
    test_circular_gauge();
    test_scrollview();
    test_circular_progress_standalone();
    if (failures == 0) {
        printf("\nALL TESTS PASSED\n");
        return 0;
    }
    printf("\n%d CHECK(s) FAILED\n", failures);
    return 1;
}
