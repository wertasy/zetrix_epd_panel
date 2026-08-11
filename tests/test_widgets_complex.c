/*
 * test_widgets_complex.c — host tests for Modal, Bubble, WeatherCard, and VoiceWakeup widgets.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "widgets/modal.h"
#include "widgets/bubble.h"
#include "widgets/weather_card.h"
#include "widgets/voice_wakeup.h"
#include "../components/ui/include/page_renderer.h"
#include "rawdraw_ext.h"

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
 * Modal Tests
 * ============================================================ */
static void test_modal(void)
{
    printf("Testing Modal...\n");
    widget_modal_t modal;
    widget_modal_init(&modal);
    widget_modal_set_title_font(&modal, &mock_font);

    // Initial values
    CHECK(modal.radius == STYLE_BORDER_RADIUS_LG);
    CHECK(modal.border_width == STYLE_BORDER_THIN);
    CHECK(modal.title[0] == '\0');
    CHECK(modal.footer[0] == '\0');

    // Bounds configuration
    rawdraw_rect_t b = {10, 20, 300, 200};
    widget_modal_set_bounds(&modal, b);
    CHECK(rect_eq(widget_modal_get_bounds(&modal), b));

    widget_modal_set_bounds_xy(&modal, 15, 25, 280, 180);
    rawdraw_rect_t expected = {15, 25, 280, 180};
    CHECK(rect_eq(widget_modal_get_bounds(&modal), expected));

    widget_modal_center_in_screen(&modal, FB_WIDTH, FB_HEIGHT, 30);
    rawdraw_rect_t centered = widget_modal_get_bounds(&modal);
    CHECK(centered.x == 30);
    CHECK(centered.y == 38);
    CHECK(centered.w == FB_WIDTH - 60);
    CHECK(centered.h == FB_HEIGHT - 76);

    // Title / Footer
    widget_modal_set_title(&modal, "Title Test");
    widget_modal_set_footer(&modal, "Footer Test");
    CHECK(strcmp(modal.title, "Title Test") == 0);
    CHECK(strcmp(modal.footer, "Footer Test") == 0);

    // Title bounds
    rawdraw_rect_t title_b = widget_modal_get_title_bounds(&modal);
    CHECK(title_b.x == centered.x);
    CHECK(title_b.y == centered.y);
    CHECK(title_b.w == centered.w);
    CHECK(title_b.h == STYLE_MODAL_TITLE_HEIGHT);

    // Footer bounds
    rawdraw_rect_t footer_b = widget_modal_get_footer_bounds(&modal);
    CHECK(footer_b.x == centered.x);
    CHECK(footer_b.y == centered.y + centered.h - STYLE_MODAL_FOOTER_HEIGHT);
    CHECK(footer_b.w == centered.w);
    CHECK(footer_b.h == STYLE_MODAL_FOOTER_HEIGHT);

    // Content bounds (should be padded and exclude title/footer)
    rawdraw_rect_t content_b = widget_modal_get_content_bounds(&modal);
    CHECK(content_b.x == centered.x + STYLE_CARD_PADDING);
    CHECK(content_b.y == centered.y + STYLE_MODAL_TITLE_HEIGHT + STYLE_CARD_PADDING);
    CHECK(content_b.w == centered.w - STYLE_CARD_PADDING * 2);
    CHECK(content_b.h == centered.h - STYLE_MODAL_TITLE_HEIGHT - STYLE_MODAL_FOOTER_HEIGHT - STYLE_CARD_PADDING * 2);

    // Set styling parameters
    widget_modal_set_radius(&modal, 15);
    widget_modal_set_border_width(&modal, 3);
    CHECK(modal.radius == 15);
    CHECK(modal.border_width == 3);

    // Draw check (no-crash validation)
    reset_fb();
    widget_modal_render(&modal, fb, FB_WIDTH, FB_HEIGHT);
    printf("Modal rendering completed.\n");
}

/* ============================================================
 * Bubble Tests
 * ============================================================ */
static void test_bubble(void)
{
    printf("Testing Bubble...\n");
    widget_bubble_t bubble;
    widget_bubble_init(&bubble, WIDGET_BUBBLE_ALIGN_LEFT, 10, 200, 8);
    widget_bubble_set_font(&bubble, &mock_font);

    CHECK(bubble.align == WIDGET_BUBBLE_ALIGN_LEFT);
    CHECK(bubble.margin == 10);
    CHECK(bubble.max_width == 200);
    CHECK(bubble.radius == 8);
    CHECK(bubble.font == &mock_font);
    CHECK(!widget_bubble_has_content(&bubble));

    // Set text
    widget_bubble_set_text(&bubble, "Hello World");
    CHECK(widget_bubble_has_content(&bubble));
    CHECK(strcmp(widget_bubble_get_text(&bubble), "Hello World") == 0);

    // Append text
    widget_bubble_append_text(&bubble, "! Hello Again.");
    CHECK(strcmp(widget_bubble_get_text(&bubble), "Hello World! Hello Again.") == 0);

    // Layout dimensions
    widget_bubble_set_y(&bubble, 50);
    int w = widget_bubble_calculate_width(&bubble);
    int h = widget_bubble_calculate_height(&bubble);
    CHECK(w > 0);
    CHECK(h > 0);

    // Bounds alignment
    rawdraw_rect_t bounds_left = widget_bubble_get_bounds(&bubble, FB_WIDTH);
    CHECK(bounds_left.x == 10);
    CHECK(bounds_left.y == 50);
    CHECK(bounds_left.w == w);
    CHECK(bounds_left.h == h);

    widget_bubble_set_align(&bubble, WIDGET_BUBBLE_ALIGN_RIGHT);
    rawdraw_rect_t bounds_right = widget_bubble_get_bounds(&bubble, FB_WIDTH);
    CHECK(bounds_right.x == FB_WIDTH - 10 - w);

    widget_bubble_set_align(&bubble, WIDGET_BUBBLE_ALIGN_CENTER);
    rawdraw_rect_t bounds_center = widget_bubble_get_bounds(&bubble, FB_WIDTH);
    CHECK(bounds_center.x == (FB_WIDTH - w) / 2);

    // Wrapping check (long text should increase height)
    widget_bubble_set_align(&bubble, WIDGET_BUBBLE_ALIGN_LEFT);
    widget_bubble_set_text(&bubble,
                           "This is a very long string that should wrap to multiple lines when bounds are checked.");
    int h_long = widget_bubble_calculate_height(&bubble);
    CHECK(h_long > h);

    // Clear
    widget_bubble_clear(&bubble);
    CHECK(!widget_bubble_has_content(&bubble));

    // Custom colors configuration
    widget_bubble_set_colors(&bubble, RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_RED, 2);
    CHECK(bubble.fill_color == RAWDRAW_COLOR_BLACK);
    CHECK(bubble.text_color == RAWDRAW_COLOR_WHITE);
    CHECK(bubble.border_color == RAWDRAW_COLOR_RED);
    CHECK(bubble.border_width == 2);
    CHECK(bubble.custom_colors == true);

    // Draw check (no-crash validation)
    widget_bubble_set_text(&bubble, "Wrapped text");
    reset_fb();
    widget_bubble_render(&bubble, fb, FB_WIDTH, FB_HEIGHT);
    printf("Bubble rendering completed.\n");
}

/* ============================================================
 * WeatherCard Tests
 * ============================================================ */
static void test_weather_card(void)
{
    printf("Testing WeatherCard...\n");
    widget_weather_card_t card;
    widget_weather_card_init(&card, 0, 40, FB_WIDTH);

    CHECK(card.x == 0);
    CHECK(card.y == 40);
    CHECK(card.w == FB_WIDTH);
    CHECK(!card.has_data);

    // Position & width
    widget_weather_card_set_position(&card, 10, 50);
    widget_weather_card_set_width(&card, 380);
    CHECK(card.x == 10);
    CHECK(card.y == 50);
    CHECK(card.w == 380);

    // Parsing test
    CHECK(widget_weather_card_parse_icon("晴天") == WIDGET_WEATHER_ICON_SUNNY);
    CHECK(widget_weather_card_parse_icon("雷阵雨") == WIDGET_WEATHER_ICON_RAIN);
    CHECK(widget_weather_card_parse_icon("多云转阴") == WIDGET_WEATHER_ICON_CLOUDY);
    CHECK(widget_weather_card_parse_icon("大雾") == WIDGET_WEATHER_ICON_FOG);
    CHECK(widget_weather_card_parse_icon("大风") == WIDGET_WEATHER_ICON_UNKNOWN);

    // Set data
    widget_weather_data_t data;
    memset(&data, 0, sizeof(data));
    strcpy(data.city, "西安");
    strcpy(data.temp, "25");
    strcpy(data.feels_like, "27");
    strcpy(data.weather_text, "小雨");
    strcpy(data.wind_dir, "东北风");
    strcpy(data.wind_scale, "2");
    strcpy(data.humidity, "80");
    strcpy(data.update_time, "12:00");

    widget_weather_card_set_city_name(&card, "Override City");
    widget_weather_card_set_data(&card, &data);
    CHECK(card.has_data);
    CHECK(strcmp(card.city_name, "Override City") == 0);

    rawdraw_rect_t bounds = widget_weather_card_get_bounds(&card);
    CHECK(bounds.x == 10);
    CHECK(bounds.y == 50);
    CHECK(bounds.w == 380);
    CHECK(bounds.h == WIDGET_WEATHER_CARD_MAX_HEIGHT);

    // Refresh tracker check
    region_refresh_t *ref = widget_weather_card_get_refresh_tracker(&card);
    CHECK(ref != NULL);
    CHECK(ref->dirty == true);

    // Draw check (no-crash validation)
    reset_fb();
    bool drawn = widget_weather_card_render(&card, fb, FB_WIDTH, FB_HEIGHT);
    CHECK(drawn);
    CHECK(ref->dirty == false); // Mark clean after draw
    CHECK(ref->partial_count == 1);

    printf("WeatherCard rendering completed.\n");
}

/* ============================================================
 * VoiceWakeup Tests
 * ============================================================ */
static void test_voice_wakeup(void)
{
    printf("Testing VoiceWakeup...\n");
    widget_voice_wakeup_state_t state;
    widget_voice_wakeup_init(&state, &mock_font);

    CHECK(state.state == WIDGET_VOICE_STATE_IDLE);
    CHECK(state.visible == false);
    CHECK(state.font == &mock_font);

    // Start recording
    widget_voice_wakeup_start_recording(&state);
    CHECK(state.state == WIDGET_VOICE_STATE_RECORDING);
    CHECK(state.visible == true);
    CHECK(strcmp(state.overlay_text, "录音中...") == 0);

    // Waiting
    widget_voice_wakeup_waiting(&state);
    CHECK(state.state == WIDGET_VOICE_STATE_WAITING_RESPONSE);
    CHECK(strcmp(state.overlay_text, "处理中...") == 0);

    // Show offline
    widget_voice_wakeup_show_offline(&state);
    CHECK(state.state == WIDGET_VOICE_STATE_OFFLINE_MSG);
    CHECK(state.visible == true);

    // Done
    widget_voice_wakeup_done(&state);
    CHECK(state.state == WIDGET_VOICE_STATE_DONE);
    CHECK(strcmp(state.overlay_text, "完成") == 0);

    // Get bounds
    rawdraw_rect_t bounds = widget_voice_wakeup_get_bounds();
    CHECK(bounds.x == WIDGET_VOICE_OVERLAY_X);
    CHECK(bounds.y == WIDGET_VOICE_OVERLAY_Y);
    CHECK(bounds.w == WIDGET_VOICE_OVERLAY_W);
    CHECK(bounds.h == WIDGET_VOICE_OVERLAY_H);

    // Tick test
    int64_t start_time = state.state_start_us;
    widget_voice_wakeup_tick(&state, start_time + 500 * 1000); // 500ms
    CHECK(state.visible == true); // Should still be visible (done fade is 1500ms)

    widget_voice_wakeup_tick(&state, start_time + 2000 * 1000); // 2000ms
    CHECK(state.state == WIDGET_VOICE_STATE_IDLE);
    CHECK(state.visible == false); // Should auto-hide

    // Draw check (no-crash validation)
    widget_voice_wakeup_start_recording(&state);
    reset_fb();
    bool drawn = widget_voice_wakeup_render(&state, fb, FB_WIDTH, FB_HEIGHT);
    CHECK(drawn);

    // State string check
    CHECK(strcmp(widget_voice_wakeup_state_to_string(WIDGET_VOICE_STATE_IDLE), "IDLE") == 0);
    CHECK(strcmp(widget_voice_wakeup_state_to_string(WIDGET_VOICE_STATE_RECORDING), "RECORDING") == 0);
    CHECK(strcmp(widget_voice_wakeup_state_to_string(WIDGET_VOICE_STATE_DONE), "DONE") == 0);

    printf("VoiceWakeup rendering completed.\n");
}

int main(void)
{
    printf("== widgets complex tests ==\n");
    test_modal();
    test_bubble();
    test_weather_card();
    test_voice_wakeup();

    if (failures == 0) {
        printf("\nALL TESTS PASSED\n");
        return 0;
    }
    printf("\n%d CHECK(s) FAILED\n", failures);
    return 1;
}
