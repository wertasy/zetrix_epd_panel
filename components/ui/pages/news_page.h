/**
 * @file news_page.h
 * @brief News feed page renderer — C port of C++ rawdraw::NewsRenderer.
 *
 * Shows a scrollable news list with UP/DOWN selection; BOOT opens a preview
 * modal with a close / read-aloud footer action.
 */
#ifndef COMPONENTS_UI_PAGES_NEWS_PAGE_H_
#define COMPONENTS_UI_PAGES_NEWS_PAGE_H_

#include "page_renderer.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NEWS_MAX_ITEMS 16
#define NEWS_TITLE_LEN 64
#define NEWS_SUMMARY_LEN 128
#define NEWS_SOURCE_LEN 32
#define NEWS_TIME_LEN 16

typedef struct {
    char title[NEWS_TITLE_LEN];
    char summary[NEWS_SUMMARY_LEN];
    char source[NEWS_SOURCE_LEN];
    char time_label[NEWS_TIME_LEN];
} news_item_t;

typedef struct {
    page_renderer_t base;

    news_item_t items[NEWS_MAX_ITEMS];
    int         count;
    int         selected_index;
    int         scroll_offset;
    bool        preview_open;
    int         footer_focus; /* 0=close, 1=read aloud */
    int         preview_scroll; /* scroll line offset in preview modal */

    const lv_font_t *font;
    const lv_font_t *title_font;

    /* Read-aloud callback: invoked with "title summary" text. */
    void (*tts_request_cb)(const char *text, void *ctx);
    void *tts_ctx;
} news_page_t;

/* PageRenderer vtable entry points. */
void news_page_init(page_renderer_t *self, int width, int height);
void news_page_render(page_renderer_t *self, uint8_t *fb, int width, int height);
bool news_page_handle_input(page_renderer_t *self, const ui_button_event_t *event);

/* Data interface. */
void news_page_set_items(page_renderer_t *self, const news_item_t *items, int count);
void news_page_add_item(page_renderer_t *self, const news_item_t *item);
void news_page_clear(page_renderer_t *self);
void news_page_set_tts_request_callback(page_renderer_t *self, void (*cb)(const char *text, void *ctx), void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_UI_PAGES_NEWS_PAGE_H_ */
