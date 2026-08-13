/**
 * @file chat_page.h
 * @brief Chat page renderer — C port of C++ rawdraw::ChatRenderer.
 *
 * Flat-text chat messages (">" user / "[AI]" AI prefix), system messages
 * centered, scroll indicator, streaming dots, and a volume dialog overlay.
 */
#ifndef COMPONENTS_UI_PAGES_CHAT_PAGE_H_
#define COMPONENTS_UI_PAGES_CHAT_PAGE_H_

#include "page_renderer.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CHAT_MAX_MESSAGES 64
#define CHAT_MSG_TEXT_LEN 512

typedef enum {
    CHAT_ROLE_USER = 0,
    CHAT_ROLE_AI,
    CHAT_ROLE_SYSTEM,
} chat_role_t;

typedef struct {
    char text[CHAT_MSG_TEXT_LEN];
    chat_role_t role;
    int y_pos; /* computed layout position */
    int block_h; /* computed block height incl. gap */
} chat_message_t;

typedef struct {
    page_renderer_t base;

    chat_message_t messages[CHAT_MAX_MESSAGES];
    int message_count;
    bool is_streaming;
    bool is_listening;
    bool follow_latest;
    int scroll_offset;
    int max_scroll_offset;
    char bottom_status_text[64];
    int stream_frame;
    bool showing_volume_dialog;
    int volume_dialog_value;
    void (*volume_dialog_handler)(int value, bool commit, void *ctx);
    void *volume_dialog_ctx;

    const lv_font_t *font;
    const lv_font_t *title_font;
} chat_page_t;

/* PageRenderer vtable entry points. */
void chat_page_init(page_renderer_t *self, int width, int height);
void chat_page_render(page_renderer_t *self, uint8_t *fb, int width, int height);
bool chat_page_handle_input(page_renderer_t *self, const ui_button_event_t *event);
bool chat_page_append_text(page_renderer_t *self, const char *chunk);
void chat_page_begin_stream(page_renderer_t *self);
void chat_page_end_stream(page_renderer_t *self);

/* Data interface. */
void chat_page_clear(page_renderer_t *self);
void chat_page_add_message(page_renderer_t *self, const char *text, chat_role_t role);
void chat_page_show_status(page_renderer_t *self, const char *status, chat_role_t role);
void chat_page_hide_status(page_renderer_t *self);
void chat_page_set_listening(page_renderer_t *self, bool listening);
void chat_page_set_bottom_status(page_renderer_t *self, const char *status);
int chat_page_get_message_count(const page_renderer_t *self);
void chat_page_show_volume_dialog(page_renderer_t *self, int volume);
void chat_page_set_volume_dialog_handler(page_renderer_t *self, void (*handler)(int, bool, void *), void *ctx);
bool chat_page_is_volume_dialog_showing(const page_renderer_t *self);
void chat_page_hide_volume_dialog(page_renderer_t *self);

#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_UI_PAGES_CHAT_PAGE_H_ */
