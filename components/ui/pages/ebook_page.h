/**
 * @file ebook_page.h
 * @brief Ebook list page and TXT reader renderer — C port of C++
 *        rawdraw::EbookRenderer.
 *
 * Two modes:
 * 1. File list: shows TXT files from SPIFFS, BOOT click selects
 * 2. Reader: paginated TXT display, BOOT click returns to file list
 */
#ifndef COMPONENTS_UI_PAGES_EBOOK_PAGE_H_
#define COMPONENTS_UI_PAGES_EBOOK_PAGE_H_

#include "page_renderer.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EBOOK_MAX_FILES 16
#define EBOOK_FILENAME_LEN 64
#define EBOOK_READER_CONTENT_LEN 8192

#define EBOOK_LANDSCAPE_CHARS_PER_PAGE 450
#define EBOOK_PORTRAIT_CHARS_PER_PAGE 620

typedef struct {
    page_renderer_t base;

    /* File list mode. */
    char files[EBOOK_MAX_FILES][EBOOK_FILENAME_LEN];
    int  file_count;
    int  selected_index;

    /* Reader mode. */
    bool reader_mode;
    bool portrait_reader;
    char reader_filename[EBOOK_FILENAME_LEN];
    char reader_content[EBOOK_READER_CONTENT_LEN];
    int  current_page; /* 0-based */
    int  total_pages;

    const lv_font_t *font;
    const lv_font_t *title_font;
} ebook_page_t;

/* PageRenderer vtable entry points. */
void ebook_page_init(page_renderer_t *self, int width, int height);
void ebook_page_render(page_renderer_t *self, uint8_t *fb, int width, int height);
bool ebook_page_handle_input(page_renderer_t *self, const ui_button_event_t *event);

/* File list mode. */
void        ebook_page_set_file_list(page_renderer_t *self, const char *const *files, int count);
int         ebook_page_get_selected_index(const page_renderer_t *self);
const char *ebook_page_get_selected_file(const page_renderer_t *self);

/* Reader mode. */
void        ebook_page_open_file(page_renderer_t *self, const char *filename, const char *content);
void        ebook_page_close_reader(page_renderer_t *self);
bool        ebook_page_is_reader_mode(const page_renderer_t *self);
bool        ebook_page_is_portrait_reader(const page_renderer_t *self);
const char *ebook_page_get_reader_filename(const page_renderer_t *self);
int         ebook_page_get_current_page(const page_renderer_t *self);
int         ebook_page_get_total_pages(const page_renderer_t *self);

#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_UI_PAGES_EBOOK_PAGE_H_ */
