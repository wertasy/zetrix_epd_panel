/**
 * @file font_metrics_page.h
 * @brief Font metrics page renderer — C port of C++ rawdraw::FontMetricsRenderer.
 *
 * Compact font metrics & baseline formula display page for diagnosing
 * rawdraw text placement.
 */
#ifndef COMPONENTS_UI_PAGES_FONT_METRICS_PAGE_H_
#define COMPONENTS_UI_PAGES_FONT_METRICS_PAGE_H_

#include "page_renderer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    page_renderer_t base;

    const lv_font_t *font;
    const lv_font_t *title_font;
} font_metrics_page_t;

/* PageRenderer vtable entry points. */
void font_metrics_page_init(page_renderer_t *self, int width, int height);
void font_metrics_page_render(page_renderer_t *self, uint8_t *fb, int width, int height);
bool font_metrics_page_handle_input(page_renderer_t *self, const ui_button_event_t *event);

#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_UI_PAGES_FONT_METRICS_PAGE_H_ */
