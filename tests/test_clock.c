#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "clock.h"
#include "rawdraw.h"
#include "rawdraw_ext.h"

const lv_font_t SourceHanSansSC_Regular_slim;
const lv_font_t SourceHanSansSC_Medium_slim;
const lv_font_t font_zectrix_16_1;
const lv_font_t font_zectrix_48_1;
const lv_font_t weather_icons_16;
const lv_font_t weather_icons_48;

#define FB_W 400
#define FB_H 300
#define FB_SIZE (((FB_W * 2 + 7) >> 3) * FB_H)

static void test_clock_init(void)
{
    printf("Testing clock init...\n");
    epd_clock_t c;
    epd_clock_init(&c, 100, 50, &font_zectrix_16_1);
    assert(c.x == 100);
    assert(c.y == 50);
    assert(c.font == &font_zectrix_16_1);
    assert(c.color == RAWDRAW_COLOR_RED);
    assert(c.last_minute == -1);
    assert(c.time_buf[0] == '\0');
    printf("Clock init passed!\n");
}

static void test_clock_setters(void)
{
    printf("Testing clock setters...\n");
    epd_clock_t c;
    epd_clock_init(&c, 0, 0, NULL);

    epd_clock_set_position(&c, 200, 10);
    assert(c.x == 200 && c.y == 10);

    epd_clock_set_font(&c, &font_zectrix_48_1);
    assert(c.font == &font_zectrix_48_1);

    epd_clock_set_color(&c, RAWDRAW_COLOR_BLACK);
    assert(c.color == RAWDRAW_COLOR_BLACK);
    printf("Clock setters passed!\n");
}

static void test_clock_bounds(void)
{
    printf("Testing clock bounds...\n");
    epd_clock_t c;
    epd_clock_init(&c, 320, 4, &font_zectrix_16_1);
    rawdraw_rect_t r = epd_clock_get_bounds(&c);
    assert(r.x == 320 && r.y == 4 && r.w == CLOCK_W && r.h == CLOCK_H);

    rawdraw_rect_t zone = epd_clock_reserved_zone();
    assert(zone.x == CLOCK_DEFAULT_X && zone.y == CLOCK_DEFAULT_Y);
    assert(zone.w == CLOCK_W && zone.h == CLOCK_H);
    printf("Clock bounds passed!\n");
}

static void test_clock_time_string(void)
{
    printf("Testing clock time string...\n");
    const char *t = epd_clock_get_time_string();
    assert(t != NULL);
    assert(strlen(t) == 5);
    printf("Clock time string passed! (got: %s)\n", t);
}

static void test_clock_date_string(void)
{
    printf("Testing clock date string...\n");
    char buf[24];
    epd_clock_get_date_string(buf, sizeof(buf), false);
    assert(strlen(buf) > 0);
    printf("Date (Chinese): %s\n", buf);

    epd_clock_get_date_string(buf, sizeof(buf), true);
    assert(strlen(buf) > 0);
    printf("Date (ISO): %s\n", buf);
    printf("Clock date string passed!\n");
}

static void test_clock_draw_no_font(void)
{
    printf("Testing clock draw with no font...\n");
    static uint8_t fb[FB_SIZE];
    memset(fb, 0x55, sizeof(fb));
    epd_clock_t c;
    epd_clock_init(&c, 320, 4, NULL);
    bool ret = epd_clock_draw(&c, fb, FB_W, FB_H);
    assert(ret == false);
    printf("Clock draw no-font passed!\n");
}

static void test_clock_draw_with_font(void)
{
    printf("Testing clock draw with font...\n");
    static uint8_t fb[FB_SIZE];
    memset(fb, 0x55, sizeof(fb));
    epd_clock_t c;
    epd_clock_init(&c, 320, 4, &font_zectrix_16_1);
    bool ret = epd_clock_draw(&c, fb, FB_W, FB_H);
    assert(ret == true);
    ret = epd_clock_draw(&c, fb, FB_W, FB_H);
    assert(ret == false);
    printf("Clock draw with font passed!\n");
}

static void test_clock_draw_with_clear(void)
{
    printf("Testing clock draw with clear...\n");
    static uint8_t fb[FB_SIZE];
    memset(fb, 0x00, sizeof(fb));
    epd_clock_t c;
    epd_clock_init(&c, 320, 4, &font_zectrix_16_1);
    c.last_minute = -1;
    bool ret      = epd_clock_draw_with_clear(&c, fb, FB_W, FB_H, RAWDRAW_COLOR_WHITE);
    assert(ret == true);
    printf("Clock draw with clear passed!\n");
}

int main(void)
{
    printf("Starting clock component tests...\n");
    test_clock_init();
    test_clock_setters();
    test_clock_bounds();
    test_clock_time_string();
    test_clock_date_string();
    test_clock_draw_no_font();
    test_clock_draw_with_font();
    test_clock_draw_with_clear();
    printf("All clock tests completed successfully!\n");
    return 0;
}
