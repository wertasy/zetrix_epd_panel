/**
 * @file car_move_page.h
 * @brief Car move (scan-QR-to-call-owner) page renderer — local pure-C
 *        implementation (计划 Task 5.9, 本地全新).
 *
 * Displays the core "微信扫码，呼叫车主" headline, the owner phone number
 * (read from NVS settings key "car_phone", default 13800000000) encoded as a
 * `tel:` payload, and a QR code placeholder when no QR library is available.
 */
#ifndef COMPONENTS_UI_PAGES_CAR_MOVE_PAGE_H_
#define COMPONENTS_UI_PAGES_CAR_MOVE_PAGE_H_

#include "page_renderer.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Max length of the owner phone number. */
#define CAR_MOVE_PHONE_LEN 24
/** Default owner phone number when no NVS value is stored. */
#define CAR_MOVE_PHONE_DEFAULT "13800000000"

typedef struct {
    page_renderer_t base;

    char phone[CAR_MOVE_PHONE_LEN]; /* owner phone number (from settings)  */
    char tel_text[48]; /* "tel:<phone>" QR payload            */
    char phone_display[40]; /* phone formatted for on-screen text  */

    const lv_font_t *font;
    const lv_font_t *title_font;
} car_move_page_t;

/* PageRenderer vtable entry points. */
void car_move_page_init(page_renderer_t *self, int width, int height);
void car_move_page_render(page_renderer_t *self, uint8_t *fb, int width, int height);
bool car_move_page_handle_input(page_renderer_t *self, const ui_button_event_t *event);

/* Data interface. */
void        car_move_page_set_phone(page_renderer_t *self, const char *phone);
const char *car_move_page_get_phone(const page_renderer_t *self);
const char *car_move_page_get_tel_payload(const page_renderer_t *self);

#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_UI_PAGES_CAR_MOVE_PAGE_H_ */
