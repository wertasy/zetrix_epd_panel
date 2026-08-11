/**
 * @file ap_transfer_page.h
 * @brief AP transfer mode renderer — C port of C++
 *        rawdraw::ApTransferRenderer.
 *
 * Shows WiFi AP connection instructions (SSID / password / browser URL) and
 * live upload status while the device is in AP image-transfer mode.
 */
#ifndef COMPONENTS_UI_PAGES_AP_TRANSFER_PAGE_H_
#define COMPONENTS_UI_PAGES_AP_TRANSFER_PAGE_H_

#include "page_renderer.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AP_TRANSFER_MSG_LEN 128 /* status message buffer */
#define AP_TRANSFER_TEXT_LEN 64 /* instruction field buffer */

typedef enum {
    AP_TRANSFER_STATE_WAITING_CONNECTION = 0, /* AP started, waiting for client */
    AP_TRANSFER_STATE_CLIENT_CONNECTED, /* client connected to AP */
    AP_TRANSFER_STATE_UPLOADING, /* image being uploaded */
    AP_TRANSFER_STATE_PROCESSING, /* Floyd-Steinberg dithering */
    AP_TRANSFER_STATE_COMPLETE, /* upload complete, image saved */
    AP_TRANSFER_STATE_ERROR, /* error occurred */
} ap_transfer_state_t;

typedef struct {
    page_renderer_t base;

    ap_transfer_state_t state;
    char                status_message[AP_TRANSFER_MSG_LEN];
    char                title_text[AP_TRANSFER_TEXT_LEN];
    char                ssid_text[AP_TRANSFER_TEXT_LEN];
    char                password_text[AP_TRANSFER_TEXT_LEN];
    char                url_text[AP_TRANSFER_TEXT_LEN];
    char                hint_text[AP_TRANSFER_TEXT_LEN];
    char                exit_hint_text[AP_TRANSFER_TEXT_LEN];

    void (*exit_callback)(void *ctx);
    void *exit_callback_ctx;

    const lv_font_t *font;
    const lv_font_t *title_font;
} ap_transfer_page_t;

/* PageRenderer vtable entry points. */
void ap_transfer_page_init(page_renderer_t *self, int width, int height);
void ap_transfer_page_render(page_renderer_t *self, uint8_t *fb, int width, int height);
bool ap_transfer_page_handle_input(page_renderer_t *self, const ui_button_event_t *event);

/* Data interface. */
void                ap_transfer_page_set_state(page_renderer_t *self, ap_transfer_state_t state, const char *message);
ap_transfer_state_t ap_transfer_page_get_state(const page_renderer_t *self);
const char         *ap_transfer_page_get_status_message(const page_renderer_t *self);
void                ap_transfer_page_use_default_instructions(page_renderer_t *self);
void                ap_transfer_page_set_instruction_content(page_renderer_t *self, const char *title, const char *ssid,
                                                             const char *password, const char *url, const char *hint,
                                                             const char *exit_hint);
void                ap_transfer_page_set_exit_callback(page_renderer_t *self, void (*callback)(void *ctx), void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_UI_PAGES_AP_TRANSFER_PAGE_H_ */
