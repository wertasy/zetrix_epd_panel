/**
 * @file weather_detail_page.h
 * @brief Weather detail page renderer — C port of C++ rawdraw::WeatherDetailRenderer.
 *
 * Hourly timeline with a 24 h temperature curve, hour selection and a
 * detail modal for the selected hour.
 */
#ifndef COMPONENTS_UI_PAGES_WEATHER_DETAIL_PAGE_H_
#define COMPONENTS_UI_PAGES_WEATHER_DETAIL_PAGE_H_

#include "page_renderer.h"
#include "weather_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WEATHER_DETAIL_MAX_HOURS 12

typedef struct {
    char    label[8];
    char    icon_code[WEATHER_ICON_LEN];
    char    weather_text[16];
    int32_t temp;
} weather_hour_point_t;

typedef struct {
    page_renderer_t base;

    weather_data_t       data;
    weather_hour_point_t hourly[WEATHER_DETAIL_MAX_HOURS];
    int                  hourly_count;
    int                  selected_hour;
    bool                 detail_open;
    bool                 has_data;

    const lv_font_t *font;
    const lv_font_t *title_font;
    const lv_font_t *icon_font;
} weather_detail_page_t;

/* PageRenderer vtable entry points. */
void weather_detail_page_init(page_renderer_t *self, int width, int height);
void weather_detail_page_render(page_renderer_t *self, uint8_t *fb, int width, int height);
bool weather_detail_page_handle_input(page_renderer_t *self, const ui_button_event_t *event);

/* Data interface. */
void weather_detail_page_update(page_renderer_t *self, const weather_data_t *data);
void weather_detail_page_set_hourly_forecast(page_renderer_t *self, const weather_hour_point_t *points, int count);

#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_UI_PAGES_WEATHER_DETAIL_PAGE_H_ */
