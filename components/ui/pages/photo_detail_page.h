/**
 * @file photo_detail_page.h
 * @brief Photo detail page renderer — C port of C++
 *        rawdraw::PhotoDetailRenderer.
 */
#ifndef COMPONENTS_UI_PAGES_PHOTO_DETAIL_PAGE_H_
#define COMPONENTS_UI_PAGES_PHOTO_DETAIL_PAGE_H_

#include "page_renderer.h"
#include "photo_storage.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PHOTO_DETAIL_MAX_PHOTOS PHOTO_MAX_PHOTOS

typedef struct {
    page_renderer_t base;

    photo_info_t photos[PHOTO_DETAIL_MAX_PHOTOS];
    int          photo_count;
    int          selected_index;
    bool         metadata_open;

    uint8_t *current_photo_data;
    uint32_t current_photo_size;
    int      current_photo_width;
    int      current_photo_height;

    const lv_font_t *font;
    const lv_font_t *title_font;
} photo_detail_page_t;

/* PageRenderer vtable entry points. */
void photo_detail_page_init(page_renderer_t *self, int width, int height);
void photo_detail_page_render(page_renderer_t *self, uint8_t *fb, int width, int height);
bool photo_detail_page_handle_input(page_renderer_t *self, const ui_button_event_t *event);

/* Data interface. */
void           photo_detail_page_refresh_photo_list(page_renderer_t *self);
void           photo_detail_page_set_selection(page_renderer_t *self, int index);
bool           photo_detail_page_is_metadata_open(const page_renderer_t *self);
bool           photo_detail_page_is_current_photo_bwry2bpp(const page_renderer_t *self);
const uint8_t *photo_detail_page_get_current_photo_data(const page_renderer_t *self);
uint32_t       photo_detail_page_get_current_photo_size(const page_renderer_t *self);
int            photo_detail_page_get_current_photo_width(const page_renderer_t *self);
int            photo_detail_page_get_current_photo_height(const page_renderer_t *self);

#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_UI_PAGES_PHOTO_DETAIL_PAGE_H_ */
