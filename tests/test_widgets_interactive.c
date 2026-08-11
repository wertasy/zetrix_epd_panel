/*
 * test_widgets_interactive.c — host tests for Slider / Toggle / StatusBar / FooterBar.
 *
 * Builds on Linux with gcc (see verification command). Verifies init defaults,
 * value clamping, drag/input handling, callback invocation, auto-hide timing,
 * and that render() writes deterministic pixels into the 2bpp framebuffer.
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

#include "../components/rawdraw/widgets/slider.h"
#include "../components/rawdraw/widgets/toggle.h"
#include "../components/rawdraw/widgets/status_bar.h"
#include "../components/rawdraw/widgets/footer_bar.h"
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

/* count non-white pixels in a rect */
static int count_drawn(int rx, int ry, int rw, int rh)
{
    int n = 0;
    for (int y = ry; y < ry + rh && y < FB_HEIGHT; y++)
        for (int x = rx; x < rx + rw && x < FB_WIDTH; x++)
            if (pix(x, y) != RAWDRAW_COLOR_WHITE)
                n++;
    return n;
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
 * Slider
 * ============================================================ */

static int  slider_cb_val   = -999;
static int  slider_cb_count = 0;
static void slider_cb(int value, void *ud)
{
    (void)ud;
    slider_cb_val = value;
    slider_cb_count++;
}

static void test_slider(void)
{
    printf("[slider]\n");

    /* init defaults */
    widget_slider_t s;
    widget_slider_init(&s, 20, 100, 200, STYLE_SLIDER_HEIGHT, 0, 100);
    CHECK(s.value == 0);
    CHECK(s.min_val == 0);
    CHECK(s.max_val == 100);
    CHECK(strcmp(s.min_label, "0") == 0);
    CHECK(strcmp(s.max_label, "100") == 0);
    CHECK(s.value_label[0] == '\0');
    CHECK(widget_slider_get_value_percent(&s) == 0);

    /* set_value clamping */
    widget_slider_set_value(&s, 50);
    CHECK(widget_slider_get_value(&s) == 50);
    CHECK(widget_slider_get_value_percent(&s) == 50);
    widget_slider_set_value(&s, 200);
    CHECK(widget_slider_get_value(&s) == 100);
    widget_slider_set_value(&s, -10);
    CHECK(widget_slider_get_value(&s) == 0);

    /* set_range re-clamps value and refreshes labels */
    widget_slider_set_value(&s, 50);
    widget_slider_set_range(&s, 10, 20);
    CHECK(widget_slider_get_value(&s) == 20); /* clamped to new max */
    CHECK(strcmp(s.min_label, "10") == 0);
    CHECK(strcmp(s.max_label, "20") == 0);

    /* reset range for drag tests */
    widget_slider_set_range(&s, 0, 100);
    widget_slider_set_value(&s, 0);

    /* x_to_value mapping */
    CHECK(widget_slider_x_to_value(&s, s.x) == 0);
    CHECK(widget_slider_x_to_value(&s, s.x + s.w) == 100);
    CHECK(widget_slider_x_to_value(&s, s.x + s.w / 2) == 50);

    /* handle_drag moves value and invokes callback */
    slider_cb_count = 0;
    widget_slider_set_callback(&s, slider_cb, NULL);
    bool changed = widget_slider_handle_drag(&s, s.x + s.w / 2); /* -> 50 */
    CHECK(changed == true);
    CHECK(widget_slider_get_value(&s) == 50);
    CHECK(slider_cb_count == 1);
    CHECK(slider_cb_val == 50);

    /* drag to same value should not invoke callback */
    slider_cb_count = 0;
    changed         = widget_slider_handle_drag(&s, s.x + s.w / 2);
    CHECK(changed == false);
    CHECK(slider_cb_count == 0);

    /* handle_input: down increases, up decreases */
    slider_cb_count = 0;
    widget_slider_set_value(&s, 0);
    button_event_t ev = {0};
    ev.type           = BTN_DOWN_CLICK;
    changed           = widget_slider_handle_input(&s, &ev);
    CHECK(changed == true);
    CHECK(widget_slider_get_value(&s) == 10);
    CHECK(slider_cb_count == 1);

    ev.type = BTN_UP_CLICK;
    changed = widget_slider_handle_input(&s, &ev);
    CHECK(changed == true);
    CHECK(widget_slider_get_value(&s) == 0);

    /* contains */
    CHECK(widget_slider_contains(&s, s.x, s.y) == true);
    CHECK(widget_slider_contains(&s, s.x + s.w - 1, s.y + s.h - 1) == true);
    CHECK(widget_slider_contains(&s, s.x - 1, s.y) == false);
    CHECK(widget_slider_contains(&s, s.x, s.y + s.h) == false);

    /* geometry */
    rawdraw_rect_t bounds = widget_slider_get_bounds(&s);
    CHECK(bounds.x == 20 && bounds.y == 100 && bounds.w == 200 && bounds.h == STYLE_SLIDER_HEIGHT);

    rawdraw_rect_t track = widget_slider_get_track_bounds(&s);
    CHECK(track.x == s.x && track.w == s.w);
    CHECK(track.h >= 4);

    rawdraw_point_t thumb = widget_slider_get_thumb_center(&s);
    CHECK(thumb.x >= s.x && thumb.x <= s.x + s.w);
    CHECK(thumb.y == s.y + s.h / 2);

    /* render writes pixels */
    reset_fb();
    widget_slider_set_font(&s, &mock_font);
    widget_slider_set_value(&s, 50);
    widget_slider_render(&s, fb, FB_WIDTH, FB_HEIGHT);
    /* track center should have non-white pixels after render */
    CHECK(pix(s.x + 10, thumb.y) != RAWDRAW_COLOR_WHITE || pix(s.x + s.w / 2, thumb.y) != RAWDRAW_COLOR_WHITE);

    /* render with zero size is a no-op (no crash) */
    widget_slider_t zero;
    widget_slider_init(&zero, 0, 0, 0, 0, 0, 10);
    widget_slider_render(&zero, fb, FB_WIDTH, FB_HEIGHT);

    /* custom labels */
    widget_slider_set_labels(&s, "Low", "High", "50%");
    CHECK(strcmp(s.min_label, "Low") == 0);
    CHECK(strcmp(s.max_label, "High") == 0);
    CHECK(strcmp(s.value_label, "50%") == 0);

    printf("  ok\n");
}

/* ============================================================
 * Toggle
 * ============================================================ */

static int  toggle_cb_val   = -1;
static int  toggle_cb_count = 0;
static void toggle_cb(bool on, void *ud)
{
    (void)ud;
    toggle_cb_val = on ? 1 : 0;
    toggle_cb_count++;
}

static void test_toggle(void)
{
    printf("[toggle]\n");

    /* init defaults */
    widget_toggle_t t;
    widget_toggle_init(&t, 50, 50, STYLE_TOGGLE_WIDTH, STYLE_TOGGLE_HEIGHT);
    CHECK(t.state == false);
    CHECK(t.label[0] == '\0');
    CHECK(widget_toggle_get_state(&t) == false);

    /* set_state */
    widget_toggle_set_state(&t, true);
    CHECK(widget_toggle_get_state(&t) == true);
    widget_toggle_set_state(&t, false);
    CHECK(widget_toggle_get_state(&t) == false);

    /* handle_tap toggles + callback */
    toggle_cb_count = 0;
    widget_toggle_set_callback(&t, toggle_cb, NULL);
    widget_toggle_handle_tap(&t);
    CHECK(widget_toggle_get_state(&t) == true);
    CHECK(toggle_cb_count == 1);
    CHECK(toggle_cb_val == 1);

    widget_toggle_handle_tap(&t);
    CHECK(widget_toggle_get_state(&t) == false);
    CHECK(toggle_cb_count == 2);
    CHECK(toggle_cb_val == 0);

    /* handle_input with BTN_BOOT_CLICK */
    toggle_cb_count   = 0;
    button_event_t ev = {0};
    ev.type           = BTN_BOOT_CLICK;
    bool handled      = widget_toggle_handle_input(&t, &ev);
    CHECK(handled == true);
    CHECK(widget_toggle_get_state(&t) == true);
    CHECK(toggle_cb_count == 1);

    /* non-boot event is ignored */
    ev.type = BTN_UP_CLICK;
    handled = widget_toggle_handle_input(&t, &ev);
    CHECK(handled == false);

    /* contains */
    CHECK(widget_toggle_contains(&t, t.x, t.y) == true);
    CHECK(widget_toggle_contains(&t, t.x + t.w - 1, t.y + t.h - 1) == true);
    CHECK(widget_toggle_contains(&t, t.x - 1, t.y) == false);

    /* track bounds */
    rawdraw_rect_t track = widget_toggle_get_track_bounds(&t);
    CHECK(rect_eq(track, (rawdraw_rect_t){50, 50, STYLE_TOGGLE_WIDTH, STYLE_TOGGLE_HEIGHT}));

    /* thumb center: off -> left, on -> right */
    widget_toggle_set_state(&t, false);
    rawdraw_point_t thumb_off = widget_toggle_get_thumb_center(&t);
    widget_toggle_set_state(&t, true);
    rawdraw_point_t thumb_on = widget_toggle_get_thumb_center(&t);
    CHECK(thumb_off.x < thumb_on.x);
    CHECK(thumb_off.y == thumb_on.y);
    CHECK(thumb_off.y == t.y + t.h / 2);

    /* get_bounds with label */
    widget_toggle_set_font(&t, &mock_font);
    widget_toggle_set_label(&t, "WiFi");
    rawdraw_rect_t bnds = widget_toggle_get_bounds(&t, FB_WIDTH);
    CHECK(bnds.w > t.w); /* label adds width */

    /* get_bounds without label */
    widget_toggle_set_label(&t, "");
    bnds = widget_toggle_get_bounds(&t, FB_WIDTH);
    CHECK(bnds.w == t.w);

    /* render writes pixels */
    reset_fb();
    widget_toggle_set_state(&t, true);
    widget_toggle_render(&t, fb, FB_WIDTH, FB_HEIGHT);
    /* the track area should have some non-white pixels */
    CHECK(count_drawn(t.x, t.y, t.w, t.h) > 0);

    /* render off state */
    reset_fb();
    widget_toggle_set_state(&t, false);
    widget_toggle_render(&t, fb, FB_WIDTH, FB_HEIGHT);

    /* render with zero size is a no-op */
    widget_toggle_t zero;
    widget_toggle_init(&zero, 0, 0, 0, 0);
    widget_toggle_render(&zero, fb, FB_WIDTH, FB_HEIGHT);

    printf("  ok\n");
}

/* ============================================================
 * StatusBar
 * ============================================================ */

static void test_status_bar(void)
{
    printf("[status_bar]\n");

    widget_status_bar_t sb;
    widget_status_bar_init(&sb, &mock_font);

    /* init defaults */
    CHECK(sb.visible == false);
    CHECK(widget_status_bar_is_visible(&sb) == false);
    CHECK(sb.text[0] == '\0');

    /* show */
    widget_status_bar_show(&sb, "Saved", WIDGET_STATUS_BAR_AUTO_HIDE_MS, 1000000);
    CHECK(widget_status_bar_is_visible(&sb) == true);
    CHECK(strcmp(sb.text, "Saved") == 0);
    CHECK(sb.show_time_us == 1000000);
    CHECK(sb.auto_hide_ms == WIDGET_STATUS_BAR_AUTO_HIDE_MS);

    /* auto-hide: not expired yet (1s < 3s) */
    CHECK(widget_status_bar_should_auto_hide(&sb, 1000000 + 1000000) == false);

    /* auto-hide: expired (4s > 3s) */
    CHECK(widget_status_bar_should_auto_hide(&sb, 1000000 + 4000000) == true);

    /* auto-hide disabled (auto_hide_ms = 0) */
    widget_status_bar_show(&sb, "Pinned", 0, 1000000);
    CHECK(widget_status_bar_should_auto_hide(&sb, 1000000 + 999999999) == false);

    /* hide */
    widget_status_bar_hide(&sb);
    CHECK(widget_status_bar_is_visible(&sb) == false);
    CHECK(sb.text[0] == '\0');

    /* get_bounds at bottom of screen */
    widget_status_bar_show(&sb, "Test", 0, 0);
    rawdraw_rect_t bnds = widget_status_bar_get_bounds(&sb, FB_WIDTH, FB_HEIGHT);
    CHECK(bnds.x == 0);
    CHECK(bnds.y == FB_HEIGHT - WIDGET_STATUS_BAR_HEIGHT);
    CHECK(bnds.w == FB_WIDTH);
    CHECK(bnds.h == WIDGET_STATUS_BAR_HEIGHT);

    /* render returns true when visible */
    reset_fb();
    CHECK(widget_status_bar_render(&sb, fb, FB_WIDTH, FB_HEIGHT) == true);
    /* bottom bar area should have non-white pixels */
    CHECK(count_drawn(0, FB_HEIGHT - WIDGET_STATUS_BAR_HEIGHT, FB_WIDTH, WIDGET_STATUS_BAR_HEIGHT) > 0);

    /* render returns false when hidden */
    widget_status_bar_hide(&sb);
    reset_fb();
    CHECK(widget_status_bar_render(&sb, fb, FB_WIDTH, FB_HEIGHT) == false);

    /* long text truncation (no overflow) */
    widget_status_bar_show(&sb,
                           "This is a very long status text that exceeds the 64 char buffer limit "
                           "and should be safely truncated without any buffer overflow issues here",
                           0, 0);
    CHECK(strlen(sb.text) < WIDGET_STATUS_BAR_TEXT_LEN);

    printf("  ok\n");
}

/* ============================================================
 * FooterBar
 * ============================================================ */

static void test_footer_bar(void)
{
    printf("[footer_bar]\n");

    widget_footer_bar_t fbar;
    widget_footer_bar_init(&fbar, FB_WIDTH, FB_HEIGHT);
    widget_footer_bar_set_font(&fbar, &mock_font);

    /* init defaults */
    rawdraw_rect_t bnds = widget_footer_bar_get_bounds(&fbar);
    CHECK(bnds.x == 0);
    CHECK(bnds.y == FB_HEIGHT - STYLE_FOOTER_BAR_HEIGHT);
    CHECK(bnds.w == FB_WIDTH);
    CHECK(bnds.h == STYLE_FOOTER_BAR_HEIGHT);
    CHECK(fbar.inverted == false);
    CHECK(fbar.left_text[0] == '\0');

    /* set_text */
    widget_footer_bar_set_text(&fbar, "Back", "Select", "Next");
    CHECK(strcmp(fbar.left_text, "Back") == 0);
    CHECK(strcmp(fbar.center_text, "Select") == 0);
    CHECK(strcmp(fbar.right_text, "Next") == 0);

    /* set_text with NULLs clears */
    widget_footer_bar_set_text(&fbar, "Left", NULL, NULL);
    CHECK(strcmp(fbar.left_text, "Left") == 0);
    CHECK(fbar.center_text[0] == '\0');
    CHECK(fbar.right_text[0] == '\0');

    /* set_bounds */
    widget_footer_bar_set_bounds(&fbar, 300, 200);
    bnds = widget_footer_bar_get_bounds(&fbar);
    CHECK(bnds.y == 200 - STYLE_FOOTER_BAR_HEIGHT);
    CHECK(bnds.w == 300);
    widget_footer_bar_set_bounds(&fbar, FB_WIDTH, FB_HEIGHT);

    /* inverted flag */
    widget_footer_bar_set_inverted(&fbar, true);
    CHECK(fbar.inverted == true);
    widget_footer_bar_set_inverted(&fbar, false);
    CHECK(fbar.inverted == false);

    /* render writes pixels */
    reset_fb();
    widget_footer_bar_set_text(&fbar, "Menu", "OK", "Home");
    widget_footer_bar_render(&fbar, fb, FB_WIDTH, FB_HEIGHT);
    /* footer bar area should have border pixels */
    int footer_top = FB_HEIGHT - STYLE_FOOTER_BAR_HEIGHT;
    CHECK(pix(FB_WIDTH / 2, footer_top + 1) != RAWDRAW_COLOR_WHITE ||
          pix(FB_WIDTH / 2, footer_top + STYLE_FOOTER_BAR_HEIGHT / 2) != RAWDRAW_COLOR_WHITE);

    /* render with no font is a no-op */
    widget_footer_bar_t nf;
    widget_footer_bar_init(&nf, FB_WIDTH, FB_HEIGHT);
    reset_fb();
    widget_footer_bar_render(&nf, fb, FB_WIDTH, FB_HEIGHT);
    /* nothing should be drawn (all white) */
    CHECK(pix(FB_WIDTH / 2, FB_HEIGHT - STYLE_FOOTER_BAR_HEIGHT / 2) == RAWDRAW_COLOR_WHITE);

    /* long text truncation */
    widget_footer_bar_set_text(&fbar,
                               "This text is way too long to fit in the 64 character buffer and must be "
                               "safely truncated to prevent any kind of buffer overflow on the device",
                               "", "");
    CHECK(strlen(fbar.left_text) < WIDGET_FOOTER_BAR_TEXT_LEN);

    printf("  ok\n");
}

/* ============================================================
 * Main
 * ============================================================ */

int main(void)
{
    printf("== widgets interactive tests ==\n");
    test_slider();
    test_toggle();
    test_status_bar();
    test_footer_bar();
    if (failures == 0) {
        printf("\nALL TESTS PASSED\n");
        return 0;
    }
    printf("\n%d CHECK(s) FAILED\n", failures);
    return 1;
}
