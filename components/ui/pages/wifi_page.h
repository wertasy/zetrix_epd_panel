/**
 * @file wifi_page.h
 * @brief WiFi status page renderer — C port of C++ rawdraw::WifiRenderer.
 *
 * Visual connection status with large icon, signal strength bars,
 * server status card and connection progress.
 */
#ifndef COMPONENTS_UI_PAGES_WIFI_PAGE_H_
#define COMPONENTS_UI_PAGES_WIFI_PAGE_H_

#include "page_renderer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_STATE_CONNECTING = 0, ///< Blinking WiFi icon + progress bar
    WIFI_STATE_CONNECTED, ///< Solid WiFi icon + SSID + signal bars
    WIFI_STATE_DISCONNECTED, ///< Cross icon + disconnected message
} wifi_state_t;

typedef struct {
    wifi_state_t state;
    char         ssid[32];
    int          signal_strength; /* dBm, typically -30 .. -90 */
    int          progress; /* connection progress 0-100 */
    bool         server_connected;
    char         server_uri[64];
} wifi_status_t;

typedef struct {
    page_renderer_t base;

    wifi_status_t status;
    bool          is_blinking;
    int           blink_frame;

    const lv_font_t *font;
    const lv_font_t *title_font;
    const lv_font_t *icon_font;
    const lv_font_t *large_icon_font;
} wifi_page_t;

/* PageRenderer vtable entry points. */
void wifi_page_init(page_renderer_t *self, int width, int height);
void wifi_page_render(page_renderer_t *self, uint8_t *fb, int width, int height);
bool wifi_page_handle_input(page_renderer_t *self, const ui_button_event_t *event);

/* Data interface. */
void                 wifi_page_update(page_renderer_t *self, const wifi_status_t *status);
const wifi_status_t *wifi_page_get_status(const page_renderer_t *self);
void                 wifi_page_set_blinking(page_renderer_t *self, bool blinking);
bool                 wifi_page_is_blinking(const page_renderer_t *self);

#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_UI_PAGES_WIFI_PAGE_H_ */
