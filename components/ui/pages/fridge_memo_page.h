/* components/ui/pages/fridge_memo_page.h */
/**
 * @file fridge_memo_page.h
 * @brief Fridge memo page renderer (design doc v1.2 §5).
 *
 * Summary strip (expired/near counts) + 4 double-line rows per screen with
 * full-page flip navigation + footer (result/banner/hint) + delete overlay
 * (in-page 4-item picker; the overlay IS the confirmation).
 */
#ifndef COMPONENTS_UI_PAGES_FRIDGE_MEMO_PAGE_H_
#define COMPONENTS_UI_PAGES_FRIDGE_MEMO_PAGE_H_

#include "page_renderer.h"
#include "fridge_memo_dto.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FRIDGE_MEMO_ROWS_PER_SCREEN 4
#define FRIDGE_MEMO_FOOTER_TEXT_LEN 64

typedef struct {
    page_renderer_t base;

    fridge_memo_snapshot_t data;
    int page_index; /* 0-based screen */

    char footer_message[FRIDGE_MEMO_FOOTER_TEXT_LEN]; /* result text, "" = none */
    bool offline; /* yellow banner mode */

    /* Delete overlay state (design §5.3). */
    bool showing_delete;
    int delete_focus; /* 0..rows_on_page-1 */

    /* Delete request channel (wired by app layer; pages never do IO). */
    void (*delete_request_cb)(const char *item_id, void *ctx);
    void *delete_request_ctx;
} fridge_memo_page_t;

/* PageRenderer vtable entry points. */
void fridge_memo_page_init(page_renderer_t *self, int width, int height);
void fridge_memo_page_render(page_renderer_t *self, uint8_t *fb, int width, int height);
bool fridge_memo_page_handle_input(page_renderer_t *self, const ui_button_event_t *event);

/* Data interface. */
void fridge_memo_page_update(page_renderer_t *self, const fridge_memo_snapshot_t *data);
void fridge_memo_page_set_footer_message(page_renderer_t *self, const char *msg);
void fridge_memo_page_set_offline(page_renderer_t *self, bool offline);
void fridge_memo_page_set_delete_request_handler(page_renderer_t *self, void (*cb)(const char *item_id, void *ctx),
                                                 void *ctx);

/* Pure helpers (host-testable). */
int fridge_memo_page_count(const fridge_memo_page_t *r);
int fridge_memo_page_pages(const fridge_memo_page_t *r);
int fridge_memo_page_rows_on_page(const fridge_memo_page_t *r, int page_index);

#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_UI_PAGES_FRIDGE_MEMO_PAGE_H_ */
