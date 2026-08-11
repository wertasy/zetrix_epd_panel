/**
 * @file test_ui_pages_smoke.c
 * @brief Host smoke tests for page renderers.
 *
 * Verifies that page renderers initialize, render into a 400x300 2bpp
 * framebuffer without crashing, and handle button input. Uses a mock
 * font with deterministic metrics and a small set of components.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "page_renderer.h"
#include "chat_page.h"
#include "coding_plan_page.h"
#include "rawdraw_ext.h"
#include "theme.h"
#include "style.h"

/* Dummy fonts for font_engine.h declarations. */
const lv_font_t SourceHanSansSC_Regular_slim;
const lv_font_t SourceHanSansSC_Medium_slim;
const lv_font_t font_zectrix_16_1;
const lv_font_t font_zectrix_48_1;
const lv_font_t weather_icons_16;
const lv_font_t weather_icons_48;
const lv_font_t fa_settings_16;

/* Mock font: 8px-wide glyphs, deterministic measuring. */
static bool mock_get_glyph_dsc(const struct _lv_font_t *font, lv_font_glyph_dsc_t *dsc, uint32_t letter,
                               uint32_t letter_next)
{
    (void)font;
    (void)letter;
    (void)letter_next;
    dsc->resolved_font = font;
    dsc->adv_w         = 8;
    dsc->box_w         = 6;
    dsc->box_h         = 6;
    dsc->ofs_x         = 1;
    dsc->ofs_y         = 1;
    dsc->stride        = 1;
    dsc->format        = LV_FONT_GLYPH_FORMAT_A1;
    return true;
}
static const uint8_t mock_bitmap[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static const void   *mock_get_glyph_bitmap(lv_font_glyph_dsc_t *g, struct _lv_draw_buf_t *db)
{
    (void)g;
    (void)db;
    return mock_bitmap;
}
const lv_font_t mock_font = {
    .get_glyph_dsc    = mock_get_glyph_dsc,
    .get_glyph_bitmap = mock_get_glyph_bitmap,
    .release_glyph    = NULL,
    .line_height      = 8,
    .base_line        = 1,
};

#define FB_W 400
#define FB_H 300
#define FB_BYTES (((FB_W * 2 + 7) >> 3) * FB_H)

static void test_chat_render_smoke(void)
{
    static chat_page_t page;
    static uint8_t     fb[FB_BYTES];
    memset(fb, 0x55, sizeof(fb));

    page_renderer_t *r = (page_renderer_t *)&page;
    chat_page_init(r, FB_W, FB_H);
    assert(r->width == FB_W);
    assert(r->height == FB_H);

    /* Empty render must not crash. */
    chat_page_render(r, fb, FB_W, FB_H);

    /* Add a user + AI message and render again. */
    chat_page_add_message(r, "你好", CHAT_ROLE_USER);
    chat_page_add_message(r, "你好，有什么可以帮你？", CHAT_ROLE_AI);
    assert(chat_page_get_message_count(r) == 2);
    chat_page_render(r, fb, FB_W, FB_H);

    /* Streaming: begin, append chunks, end. */
    chat_page_begin_stream(r);
    chat_page_append_text(r, "正在");
    chat_page_append_text(r, "生成");
    chat_page_end_stream(r);
    chat_page_render(r, fb, FB_W, FB_H);

    /* Input routing must not crash. */
    ui_button_event_t ev = {BTN_UP_CLICK};
    chat_page_handle_input(r, &ev);
    ev.type = BTN_DOWN_CLICK;
    chat_page_handle_input(r, &ev);
    ev.type = BTN_BOOT_CLICK;
    chat_page_handle_input(r, &ev);
    chat_page_render(r, fb, FB_W, FB_H);

    /* Clear and re-render. */
    chat_page_clear(r);
    assert(chat_page_get_message_count(r) == 0);
    chat_page_render(r, fb, FB_W, FB_H);

    printf("chat_page render smoke ok\n");
}

static void test_chat_volume_dialog(void)
{
    static chat_page_t page;
    static uint8_t     fb[FB_BYTES];
    memset(fb, 0x55, sizeof(fb));
    page_renderer_t *r = (page_renderer_t *)&page;
    chat_page_init(r, FB_W, FB_H);

    chat_page_show_volume_dialog(r, 70);
    assert(chat_page_is_volume_dialog_showing(r));
    chat_page_render(r, fb, FB_W, FB_H);

    ui_button_event_t ev = {BTN_UP_CLICK};
    chat_page_handle_input(r, &ev); /* +10 */
    ev.type = BTN_BOOT_CLICK;
    chat_page_handle_input(r, &ev); /* commit */
    assert(!chat_page_is_volume_dialog_showing(r));
    chat_page_render(r, fb, FB_W, FB_H);
    printf("chat volume dialog ok\n");
}

static void test_theme_styles(void)
{
    /* Theme API must return sane values on host. */
    rawdraw_paint_style_t s = rawdraw_theme_style(THEME_TOKEN_TEXT_PRIMARY);
    assert(s.fg == RAWDRAW_COLOR_BLACK || s.fg == RAWDRAW_COLOR_WHITE);
    rawdraw_paint_style_t card = rawdraw_theme_component(ROLE_CARD_DEFAULT);
    (void)card;
    printf("theme styles ok\n");
}

static void test_coding_plan_render_smoke(void)
{
    static coding_plan_page_t page;
    static uint8_t            fb[FB_BYTES];
    memset(fb, 0x55, sizeof(fb));
    page_renderer_t *r = (page_renderer_t *)&page;
    coding_plan_page_init(r, FB_W, FB_H);

    /* Test render without data */
    coding_plan_page_render(r, fb, FB_W, FB_H);

    /* Populate data */
    coding_plan_data_t data;
    memset(&data, 0, sizeof(data));
    strcpy(data.five_hour_reset_time, "08-09 14:58");
    strcpy(data.week_reset_time, "08-14 10:03");
    data.five_hour_pct = 84;
    data.week_pct = 54;
    data.five_hour_tokens = 1680000ULL;
    data.week_tokens = 5400000ULL;
    data.per_model_count = 2;
    strcpy(data.per_model[0].name, "GLM-5.2");
    data.per_model[0].tokens = 451000000ULL;
    strcpy(data.per_model[1].name, "GLM-4.7");
    data.per_model[1].tokens = 132000000ULL;

    /* Fill hourly usage data with 7 days of token values */
    data.hourly_count = 168;
    for (int i = 0; i < 168; ++i) {
        data.hourly_tokens[i] = (i % 24) * 100000ULL; // Varying hourly tokens
    }

    coding_plan_page_update(r, &data);
    coding_plan_page_render(r, fb, FB_W, FB_H);

    /* Test view mode switching via key clicks */
    ui_button_event_t event;
    event.type = BTN_UP_CLICK;
    assert(coding_plan_page_handle_input(r, &event) == true);
    assert(page.view_mode == 1);
    coding_plan_page_render(r, fb, FB_W, FB_H);

    event.type = BTN_DOWN_CLICK;
    assert(coding_plan_page_handle_input(r, &event) == true);
    assert(page.view_mode == 0);
    coding_plan_page_render(r, fb, FB_W, FB_H);

    printf("coding_plan_page render smoke ok\n");
}

bool coding_plan_api_fetch_async(void)
{
    return true;
}

int main(void)
{
    printf("Testing page renderers...\n");
    test_theme_styles();
    test_chat_render_smoke();
    test_chat_volume_dialog();
    test_coding_plan_render_smoke();
    printf("All page smoke tests passed!\n");
    return 0;
}
