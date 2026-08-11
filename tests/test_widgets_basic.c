/*
 * test_widgets_basic.c — host tests for Button / Panel / Card widgets.
 *
 * Builds on Linux with gcc (see verification command). Verifies init defaults,
 * state changes, layout math, input handling, and that render() actually writes
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

#include "../components/rawdraw/widgets/button.h"
#include "../components/rawdraw/widgets/panel.h"
#include "../components/rawdraw/widgets/card.h"
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
 * Button
 * ============================================================ */
/* click-counter callback used by the button tests */
static void btn_cb(void *ud);

static void test_button(void)
{
    printf("[button]\n");

    /* init defaults */
    widget_button_t btn;
    widget_button_init(&btn, 10, 20, 40, 50);
    CHECK(widget_button_get_bounds(&btn).x == 10);
    CHECK(widget_button_get_bounds(&btn).y == 20);
    CHECK(widget_button_get_bounds(&btn).w == 40);
    CHECK(widget_button_get_bounds(&btn).h == 50);
    CHECK(btn.radius == STYLE_BUTTON_RADIUS);
    CHECK(widget_button_is_pressed(&btn) == false);
    CHECK(widget_button_contains(&btn, 10, 20) == true); /* inclusive top-left */
    CHECK(widget_button_contains(&btn, 49, 69) == true); /* exclusive bottom-right edge */
    CHECK(widget_button_contains(&btn, 50, 70) == false);
    CHECK(widget_button_contains(&btn, 0, 0) == false);

    /* text buffer copy + truncation safety */
    widget_button_set_text(&btn, "OK");
    CHECK(strcmp(btn.text, "OK") == 0);
    widget_button_set_text(&btn, NULL);
    CHECK(btn.text[0] == '\0');

    /* set position/size */
    widget_button_set_position(&btn, 5, 6);
    widget_button_set_size(&btn, 7, 8);
    CHECK(rect_eq(widget_button_get_bounds(&btn), (rawdraw_rect_t){5, 6, 7, 8}));

    /* explicit pressed toggle */
    widget_button_set_pressed(&btn, true);
    CHECK(widget_button_is_pressed(&btn) == true);
    widget_button_set_pressed(&btn, false);
    CHECK(widget_button_is_pressed(&btn) == false);

    /* handle_press sets pressed + invokes callback with user_data */
    int counter = 0;
    widget_button_init(&btn, 10, 10, 40, 40);
    widget_button_set_callback(&btn, btn_cb, &counter);
    widget_button_handle_press(&btn);
    CHECK(widget_button_is_pressed(&btn) == true);
    CHECK(counter == 1);

    /* handle_input: BOOT click activates the button, other events do not */
    counter = 0;
    widget_button_set_pressed(&btn, false);
    button_event_t boot = {BTN_BOOT_CLICK};
    CHECK(widget_button_handle_input(&btn, &boot) == true);
    CHECK(counter == 1);
    CHECK(widget_button_is_pressed(&btn) == true);

    widget_button_set_pressed(&btn, false);
    button_event_t up = {BTN_UP_CLICK};
    CHECK(widget_button_handle_input(&btn, &up) == false);
    CHECK(widget_button_is_pressed(&btn) == false);

    /* NULL-safety */
    CHECK(widget_button_handle_input(NULL, &boot) == false);
    CHECK(widget_button_handle_input(&btn, NULL) == false);

    /* render: color inversion on press (deterministic, theme-independent).
     * set_colors(BLACK, WHITE, BLACK) forces the custom-color branch
     * (bg != WHITE), so normal -> bg BLACK, pressed -> bg WHITE. */
    widget_button_init(&btn, 10, 10, 40, 40);
    widget_button_set_colors(&btn, RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_BLACK);

    reset_fb();
    widget_button_set_pressed(&btn, false);
    widget_button_render(&btn, fb, FB_WIDTH, FB_HEIGHT);
    CHECK(pix(30, 30) == RAWDRAW_COLOR_BLACK);

    reset_fb();
    widget_button_set_pressed(&btn, true);
    widget_button_render(&btn, fb, FB_WIDTH, FB_HEIGHT);
    CHECK(pix(30, 30) == RAWDRAW_COLOR_WHITE);

    reset_fb();
    widget_button_set_pressed(&btn, false);
    widget_button_render(&btn, fb, FB_WIDTH, FB_HEIGHT);
    CHECK(pix(30, 30) == RAWDRAW_COLOR_BLACK);

    /* render with icon + text must not crash and must paint something */
    reset_fb();
    widget_button_init(&btn, 10, 10, 60, 60);
    widget_button_set_icon(&btn, "A");
    widget_button_set_icon_font(&btn, &mock_font);
    widget_button_set_text(&btn, "Go");
    widget_button_set_text_font(&btn, &mock_font);
    widget_button_render(&btn, fb, FB_WIDTH, FB_HEIGHT);
    /* something was drawn inside the button area */
    int drew = 0;
    for (int y = 10; y < 70 && !drew; y++)
        for (int x = 10; x < 70 && !drew; x++)
            if (pix(x, y) != RAWDRAW_COLOR_WHITE)
                drew = 1;
    CHECK(drew == 1);

    /* fully off-screen button renders nothing (no crash) */
    reset_fb();
    widget_button_init(&btn, -100, -100, 10, 10);
    widget_button_render(&btn, fb, FB_WIDTH, FB_HEIGHT);

    printf("  ok\n");
}

static void btn_cb(void *ud)
{
    (*(int *)ud)++;
}

/* ============================================================
 * Panel
 * ============================================================ */
static void test_panel(void)
{
    printf("[panel]\n");

    widget_panel_t p;
    widget_panel_init(&p, 10, 20, 100, 80, STYLE_PANEL_RADIUS);
    CHECK(rect_eq(widget_panel_get_bounds(&p), (rawdraw_rect_t){10, 20, 100, 80}));
    CHECK(p.radius == STYLE_PANEL_RADIUS);
    CHECK(p.padding == STYLE_PANEL_PADDING);
    CHECK(p.border_width == STYLE_PANEL_BORDER_WIDTH);
    CHECK(p.title_enabled == true);
    CHECK(p.bg_color == RAWDRAW_COLOR_WHITE);

    /* title height: auto-default (no font, no title) -> 0 */
    CHECK(widget_panel_calculate_title_height(&p) == 0);

    /* with title text but no font -> STYLE_PANEL_TITLE_HEIGHT */
    widget_panel_set_title(&p, "Settings");
    CHECK(widget_panel_calculate_title_height(&p) == STYLE_PANEL_TITLE_HEIGHT);

    /* with font -> line_height + 2*padding */
    widget_panel_set_title_font(&p, &mock_font);
    CHECK(widget_panel_calculate_title_height(&p) == (int)mock_font.line_height + STYLE_PANEL_PADDING * 2);

    /* explicit override */
    widget_panel_set_title_height(&p, 30);
    CHECK(widget_panel_calculate_title_height(&p) == 30);

    /* disabled -> 0 regardless */
    widget_panel_set_title_enabled(&p, false);
    CHECK(widget_panel_calculate_title_height(&p) == 0);
    widget_panel_set_title_enabled(&p, true);

    /* title bounds + content bounds (title on) */
    widget_panel_set_title_height(&p, 0);
    /* font still set -> th = 20 + 2*8 = 36 */
    int            th           = 36;
    rawdraw_rect_t expect_title = {10, 20, 100, th};
    CHECK(rect_eq(widget_panel_get_title_bounds(&p), expect_title));
    rawdraw_rect_t expect_content = {10 + STYLE_PANEL_PADDING, 20 + th + STYLE_PANEL_PADDING,
                                     100 - 2 * STYLE_PANEL_PADDING, 80 - th - 2 * STYLE_PANEL_PADDING};
    CHECK(rect_eq(widget_panel_get_content_bounds(&p), expect_content));

    /* title off -> content == full inner area */
    widget_panel_set_title_enabled(&p, false);
    rawdraw_rect_t expect_full = {10 + STYLE_PANEL_PADDING, 20 + STYLE_PANEL_PADDING, 100 - 2 * STYLE_PANEL_PADDING,
                                  80 - 2 * STYLE_PANEL_PADDING};
    CHECK(rect_eq(widget_panel_get_content_bounds(&p), expect_full));
    widget_panel_set_title_enabled(&p, true);

    /* render: black panel bg, white title bar -> title region WHITE, content BLACK */
    reset_fb();
    widget_panel_init(&p, 10, 10, 100, 100, 4);
    widget_panel_set_colors(&p, RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_BLACK); /* forces custom branch */
    widget_panel_set_title(&p, "T");
    widget_panel_set_title_font(&p, &mock_font);
    widget_panel_set_title_colors(&p, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_BLACK);
    /* th = 20 + 16 = 36; title bar y:[10,46); content top at y=10+36+8=54 */
    widget_panel_render(&p, fb, FB_WIDTH, FB_HEIGHT);
    CHECK(pix(60, 20) == RAWDRAW_COLOR_WHITE); /* inside title bar */
    CHECK(pix(60, 70) == RAWDRAW_COLOR_BLACK); /* inside content area */

    /* no title -> entire interior is bg */
    reset_fb();
    widget_panel_init(&p, 10, 10, 100, 100, 4);
    widget_panel_set_colors(&p, RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_BLACK);
    widget_panel_set_title_enabled(&p, false);
    widget_panel_render(&p, fb, FB_WIDTH, FB_HEIGHT);
    CHECK(pix(60, 60) == RAWDRAW_COLOR_BLACK);

    printf("  ok\n");
}

/* ============================================================
 * Card
 * ============================================================ */
static void test_card(void)
{
    printf("[card]\n");

    widget_card_t c;
    widget_card_init(&c, 10, 20, 120, 80, STYLE_CARD_RADIUS);
    CHECK(rect_eq(widget_card_get_bounds(&c), (rawdraw_rect_t){10, 20, 120, 80}));
    CHECK(c.radius == STYLE_CARD_RADIUS);
    CHECK(c.border_width == STYLE_CARD_BORDER_WIDTH);
    CHECK(c.padding == STYLE_CARD_PADDING);
    CHECK(c.shadow_enabled == false);
    CHECK(c.shadow_offset == STYLE_CARD_SHADOW_OFFSET);
    CHECK(c.shadow_color == RAWDRAW_COLOR_BLACK);

    /* title height default (no font) -> STYLE_CARD_TITLE_HEIGHT */
    widget_card_set_title(&c, "Card");
    CHECK(widget_card_calculate_title_height(&c) == STYLE_CARD_TITLE_HEIGHT);
    widget_card_set_title_font(&c, &mock_font);
    CHECK(widget_card_calculate_title_height(&c) == (int)mock_font.line_height + STYLE_CARD_PADDING * 2);
    widget_card_set_title_enabled(&c, false);
    CHECK(widget_card_calculate_title_height(&c) == 0);
    widget_card_set_title_enabled(&c, true);

    /* content bounds clamps negative dims to 0 (card-specific) */
    widget_card_init(&c, 0, 0, 20, 20, 8);
    widget_card_set_title(&c, "X"); /* no font -> th = STYLE_CARD_TITLE_HEIGHT (28) */
    widget_card_set_padding(&c, STYLE_CARD_PADDING);
    rawdraw_rect_t cb = widget_card_get_content_bounds(&c);
    /* th = STYLE_CARD_TITLE_HEIGHT (28), pad = 8 -> w = max(0, 20-16) = 4, h = max(0, 20-28-16) = 0 */
    CHECK(cb.w == 4);
    CHECK(cb.h == 0);

    /* render: shadow enabled draws an offset strip behind the card.
     * card bg BLACK (forced), shadow YELLOW -> body center BLACK,
     * shadow-only pixel (just outside body, inside shadow rect) YELLOW. */
    reset_fb();
    widget_card_init(&c, 10, 10, 120, 80, 8);
    widget_card_set_colors(&c, RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_BLACK);
    widget_card_set_shadow_enabled(&c, true);
    widget_card_set_shadow_color(&c, RAWDRAW_COLOR_YELLOW);
    /* shadow_offset = 2 -> shadow rect (12,12,120,80) covers x[12,132) y[12,92);
     * body covers x[10,130) y[10,90); (131,91) is shadow-only. */
    widget_card_render(&c, fb, FB_WIDTH, FB_HEIGHT);
    CHECK(pix(70, 50) == RAWDRAW_COLOR_BLACK); /* card body interior */
    CHECK(pix(131, 91) == RAWDRAW_COLOR_YELLOW); /* shadow strip */

    /* shadow disabled -> no shadow strip */
    reset_fb();
    widget_card_set_shadow_enabled(&c, false);
    widget_card_render(&c, fb, FB_WIDTH, FB_HEIGHT);
    CHECK(pix(131, 91) == RAWDRAW_COLOR_WHITE); /* background, untouched */

    /* oversized radius is clamped and still renders a filled interior */
    reset_fb();
    widget_card_init(&c, 10, 10, 120, 80, 999);
    widget_card_set_colors(&c, RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_BLACK);
    widget_card_render(&c, fb, FB_WIDTH, FB_HEIGHT);
    CHECK(pix(70, 50) == RAWDRAW_COLOR_BLACK);

    /* title bar separator + fill */
    reset_fb();
    widget_card_init(&c, 10, 10, 120, 80, 4);
    widget_card_set_colors(&c, RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_BLACK);
    widget_card_set_title(&c, "T");
    widget_card_set_title_font(&c, &mock_font);
    widget_card_set_title_colors(&c, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_BLACK);
    widget_card_render(&c, fb, FB_WIDTH, FB_HEIGHT);
    CHECK(pix(60, 20) == RAWDRAW_COLOR_WHITE); /* title bar */
    CHECK(pix(60, 70) == RAWDRAW_COLOR_BLACK); /* content */

    printf("  ok\n");
}

int main(void)
{
    printf("== widgets basic tests ==\n");
    test_button();
    test_panel();
    test_card();
    if (failures == 0) {
        printf("\nALL TESTS PASSED\n");
        return 0;
    }
    printf("\n%d CHECK(s) FAILED\n", failures);
    return 1;
}
