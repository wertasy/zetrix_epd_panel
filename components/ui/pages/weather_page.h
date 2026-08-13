/**
 * @file weather_page.h
 * @brief Weather page renderer — C port of C++ rawdraw::WeatherRenderer.
 *
 * Displays the current weather (location / temp / AQI cards), a weather
 * condition stack, humidity/wind/UV metrics, and a 4-day forecast strip.
 */
#ifndef COMPONENTS_UI_PAGES_WEATHER_PAGE_H_
#define COMPONENTS_UI_PAGES_WEATHER_PAGE_H_

#include "page_renderer.h"
#include "weather_dto.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    page_renderer_t base;

    weather_data_t   current_data;
    char             city_name[WEATHER_STR_LEN];
    char             firmware_version[WEATHER_STR_LEN];
    bool             has_data;
    int              page_index; /* selected forecast day index */
    const lv_font_t *font;
    const lv_font_t *title_font;
} weather_page_t;

/* PageRenderer vtable entry points. */
void weather_page_init(page_renderer_t *self, int width, int height);
void weather_page_render(page_renderer_t *self, uint8_t *fb, int width, int height);
bool weather_page_handle_input(page_renderer_t *self, const ui_button_event_t *event);

/* Data interface. */
void weather_page_update(page_renderer_t *self, const weather_data_t *data);
void weather_page_set_city_name(page_renderer_t *self, const char *name);
void weather_page_set_firmware_version(page_renderer_t *self, const char *version);

#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_UI_PAGES_WEATHER_PAGE_H_ */
