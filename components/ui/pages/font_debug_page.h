/**
 * @file font_debug_page.h
 * @brief Font debug page renderer — C port of C++ rawdraw::FontDebugRenderer.
 *
 * Large-row text alignment diagnostics: draws bordered boxes with a center
 * dashed line and short ink-bounds markers so font placement can be tuned
 * on the 400x300 EPD.
 */
#ifndef COMPONENTS_UI_PAGES_FONT_DEBUG_PAGE_H_
#define COMPONENTS_UI_PAGES_FONT_DEBUG_PAGE_H_

#include "page_renderer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    page_renderer_t base;

    const lv_font_t *font;
    const lv_font_t *title_font;
} font_debug_page_t;

/* PageRenderer vtable entry points. */
void font_debug_page_init(page_renderer_t *self, int width, int height);
void font_debug_page_render(page_renderer_t *self, uint8_t *fb, int width, int height);
bool font_debug_page_handle_input(page_renderer_t *self, const ui_button_event_t *event);

#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_UI_PAGES_FONT_DEBUG_PAGE_H_ */
