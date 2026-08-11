/**
 * @file log_page.h
 * @brief Log page renderer — C port of C++ rawdraw::LogRenderer.
 *
 * Shows boot events, connection status, memory stats, and recent
 * activity log entries. Scrollable list with UP/DOWN navigation.
 */
#ifndef COMPONENTS_UI_PAGES_LOG_PAGE_H_
#define COMPONENTS_UI_PAGES_LOG_PAGE_H_

#include "page_renderer.h"

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LOG_PAGE_MAX_ENTRIES 32
#define LOG_PAGE_TAG_LEN 8
#define LOG_PAGE_MSG_LEN 64
#define LOG_PAGE_TITLE_BAR_H 28

typedef struct {
    time_t time;
    char   tag[LOG_PAGE_TAG_LEN];
    char   message[LOG_PAGE_MSG_LEN];
} log_page_entry_t;

typedef struct {
    page_renderer_t base;

    int              selected_index;
    int              scroll_offset;
    const lv_font_t *font;
    const lv_font_t *title_font;
    const lv_font_t *icon_font;

    /* Ring buffer of log entries (displayed oldest-first). */
    log_page_entry_t entries[LOG_PAGE_MAX_ENTRIES];
    int              count;
    int              head; /* next write position */
} log_page_t;

/* PageRenderer vtable entry points. */
void log_page_init(page_renderer_t *self, int width, int height);
void log_page_render(page_renderer_t *self, uint8_t *fb, int width, int height);
bool log_page_handle_input(page_renderer_t *self, const ui_button_event_t *event);

/* Data interface. */
void log_page_refresh(page_renderer_t *self);

#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_UI_PAGES_LOG_PAGE_H_ */
