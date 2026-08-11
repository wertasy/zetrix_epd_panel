#include "clock.h"
#include "rawdraw_ext.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

void epd_clock_init(epd_clock_t *c, int x, int y, const lv_font_t *font)
{
    if (!c)
        return;
    c->x           = x;
    c->y           = y;
    c->font        = font;
    c->color       = RAWDRAW_COLOR_RED;
    c->last_minute = -1;
    c->time_buf[0] = '\0';
}

void epd_clock_set_position(epd_clock_t *c, int x, int y)
{
    if (c) {
        c->x = x;
        c->y = y;
    }
}

void epd_clock_set_font(epd_clock_t *c, const lv_font_t *font)
{
    if (c)
        c->font = font;
}

void epd_clock_set_color(epd_clock_t *c, rawdraw_color_t color)
{
    if (c)
        c->color = color;
}

rawdraw_rect_t epd_clock_get_bounds(const epd_clock_t *c)
{
    rawdraw_rect_t r = {0, 0, 0, 0};
    if (c) {
        r.x = c->x;
        r.y = c->y;
        r.w = CLOCK_W;
        r.h = CLOCK_H;
    }
    return r;
}

bool epd_clock_draw(epd_clock_t *c, uint8_t *fb, int width, int height)
{
    if (!c || !fb || !c->font)
        return false;

    time_t    now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);

    /* Only redraw when minute changes */
    if (tm.tm_min == c->last_minute && c->time_buf[0] != '\0') {
        return false;
    }

    snprintf(c->time_buf, sizeof(c->time_buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
    c->last_minute = tm.tm_min;

    rawdraw_draw_text(fb, width, height, c->x, c->y, c->time_buf, c->font, (int)c->color);
    return true;
}

bool epd_clock_draw_with_clear(epd_clock_t *c, uint8_t *fb, int width, int height, rawdraw_color_t bg_color)
{
    if (!c || !fb || !c->font)
        return false;

    rawdraw_rect_t r = {c->x, c->y, CLOCK_W, CLOCK_H};
    rawdraw_fill_rect(fb, width, height, r, bg_color);

    return epd_clock_draw(c, fb, width, height);
}

const char *epd_clock_get_time_string(void)
{
    static char buf[6];
    time_t      now = time(NULL);
    struct tm   tm;
    localtime_r(&now, &tm);
    if (tm.tm_year + 1900 < 2020) {
        snprintf(buf, sizeof(buf), "--:--");
    } else {
        snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
    }
    return buf;
}

void epd_clock_get_date_string(char *buf, int buf_size, bool iso_format)
{
    if (!buf || buf_size <= 0)
        return;
    time_t    now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);

    if (tm.tm_year + 1900 < 2020) {
        snprintf(buf, buf_size, "--");
        return;
    }

    if (iso_format) {
        unsigned int y = (unsigned int)(tm.tm_year + 1900);
        unsigned int m = (unsigned int)(tm.tm_mon + 1);
        unsigned int d = (unsigned int)(tm.tm_mday);
        snprintf(buf, buf_size, "%04u-%02u-%02u", y, m, d);
    } else {
        /* Chinese format "M月D日" */
        snprintf(buf, buf_size, "%d月%d日", tm.tm_mon + 1, tm.tm_mday);
    }
}

rawdraw_rect_t epd_clock_reserved_zone(void)
{
    rawdraw_rect_t r = {CLOCK_DEFAULT_X, CLOCK_DEFAULT_Y, CLOCK_W, CLOCK_H};
    return r;
}
