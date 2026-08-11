/**
 * @file photo_gallery_page.h
 * @brief Memory-style photo gallery renderer — C port of C++
 *        rawdraw::PhotoGalleryRenderer.
 */
#ifndef COMPONENTS_UI_PAGES_PHOTO_GALLERY_PAGE_H_
#define COMPONENTS_UI_PAGES_PHOTO_GALLERY_PAGE_H_

#include "page_renderer.h"
#include "photo_storage.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PHOTO_GALLERY_MAX_PHOTOS PHOTO_MAX_PHOTOS

typedef enum {
    PHOTO_GALLERY_MODE_MEMORY_CARD = 0, /* left text + right image */
    PHOTO_GALLERY_MODE_FULLSCREEN, /* single photo */
} photo_gallery_mode_t;

typedef struct {
    char     id[16];
    char     title[PHOTO_TITLE_LEN];
    char     date[PHOTO_DATE_LEN];
    char     location[PHOTO_LOCATION_LEN];
    char     body[PHOTO_BODY_LEN];
    uint16_t width;
    uint16_t height;
    uint32_t file_size;
} photo_gallery_entry_t;

typedef struct {
    page_renderer_t base;

    photo_gallery_mode_t mode;
    int                  selected_index;
    bool                 showing_delete_dialog;
    int                  delete_dialog_selected; /* 0=delete, 1=cancel */

    photo_gallery_entry_t photo_ids[PHOTO_GALLERY_MAX_PHOTOS];
    int                   photo_count;

    uint8_t *current_photo_data;
    uint32_t current_photo_size;
    int      current_photo_width;
    int      current_photo_height;

    const lv_font_t *font;
    const lv_font_t *title_font;
    const lv_font_t *icon_font;
} photo_gallery_page_t;

/* PageRenderer vtable entry points. */
void photo_gallery_init(page_renderer_t *self, int width, int height);
void photo_gallery_render(page_renderer_t *self, uint8_t *fb, int width, int height);
bool photo_gallery_handle_input(page_renderer_t *self, const ui_button_event_t *event);

/* Data interface. */
void           photo_gallery_refresh_photo_list(page_renderer_t *self);
int            photo_gallery_get_photo_count(const page_renderer_t *self);
int            photo_gallery_get_selected_index(const page_renderer_t *self);
void           photo_gallery_set_selected_index(page_renderer_t *self, int index);
bool           photo_gallery_set_selected_by_id(page_renderer_t *self, const char *id);
void           photo_gallery_enter_fullscreen_mode(page_renderer_t *self);
bool           photo_gallery_select_next(page_renderer_t *self, bool wrap);
bool           photo_gallery_is_fullscreen_mode(const page_renderer_t *self);
bool           photo_gallery_is_delete_dialog_open(const page_renderer_t *self);
bool           photo_gallery_is_current_photo_bwry2bpp(const page_renderer_t *self);
const uint8_t *photo_gallery_get_current_photo_data(const page_renderer_t *self);
uint32_t       photo_gallery_get_current_photo_size(const page_renderer_t *self);
int            photo_gallery_get_current_photo_width(const page_renderer_t *self);
int            photo_gallery_get_current_photo_height(const page_renderer_t *self);

#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_UI_PAGES_PHOTO_GALLERY_PAGE_H_ */
