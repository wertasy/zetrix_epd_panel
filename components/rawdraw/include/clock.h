#ifndef COMPONENTS_RAWDRAW_INCLUDE_CLOCK_H_
#define COMPONENTS_RAWDRAW_INCLUDE_CLOCK_H_

#include <stdint.h>
#include <stdbool.h>
#include "rawdraw.h"
#include "font_engine.h"

/* Clock position constants */
#define CLOCK_DEFAULT_X 320
#define CLOCK_DEFAULT_Y 4
#define CLOCK_W 80
#define CLOCK_H 32

typedef struct {
    int x;
    int y;
    const lv_font_t *font;
    rawdraw_color_t color;
    int last_minute;
    char time_buf[6]; /* "HH:MM\0" */
} epd_clock_t;

void epd_clock_init(epd_clock_t *c, int x, int y, const lv_font_t *font);
void epd_clock_set_position(epd_clock_t *c, int x, int y);
void epd_clock_set_font(epd_clock_t *c, const lv_font_t *font);
void epd_clock_set_color(epd_clock_t *c, rawdraw_color_t color);
rawdraw_rect_t epd_clock_get_bounds(const epd_clock_t *c);

/* Draw clock to framebuffer. Returns true if time changed and was redrawn. */
bool epd_clock_draw(epd_clock_t *c, uint8_t *fb, int width, int height);

/* Draw clock with background fill first (clears previous frame). */
bool epd_clock_draw_with_clear(epd_clock_t *c, uint8_t *fb, int width, int height, rawdraw_color_t bg_color);

/* Get current time string "HH:MM" (or "--:--" if unsynced). Static buffer. */
const char *epd_clock_get_time_string(void);

/* Get date string. buf must be at least 24 bytes. */
void epd_clock_get_date_string(char *buf, int buf_size, bool iso_format);

/* Get reserved zone rect (all pages must avoid this area). */
rawdraw_rect_t epd_clock_reserved_zone(void);

#endif /* COMPONENTS_RAWDRAW_INCLUDE_CLOCK_H_ */
