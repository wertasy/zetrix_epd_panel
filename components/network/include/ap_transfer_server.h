/**
 * @file ap_transfer_server.h
 * @brief WiFi AP + HTTP server for image transfer — C port of C++
 *        rawdraw::ApTransferServer.
 *
 * Provides:
 *  - WiFi AP (SSID: InkScreen-AP, Password: 12345678)
 *  - HTTP server at 192.168.4.1
 *  - HTML page for image upload
 *  - Floyd-Steinberg dithering
 *  - Save to LittleFS via photo_storage
 */
#ifndef COMPONENTS_NETWORK_AP_TRANSFER_SERVER_H_
#define COMPONENTS_NETWORK_AP_TRANSFER_SERVER_H_

#include <stdbool.h>
#include <stdint.h>

#include <esp_http_server.h>
#include <esp_netif.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Server lifecycle states. Numeric values are part of the public contract
 * (the UI manager maps them to page states); do not reorder. */
typedef enum {
    AP_SERVER_STATE_K_STOPPED = 0,
    AP_SERVER_STATE_K_AP_STARTED,
    AP_SERVER_STATE_K_CLIENT_CONNECTED,
    AP_SERVER_STATE_K_RECEIVING_IMAGE,
    AP_SERVER_STATE_K_PROCESSING_IMAGE,
    AP_SERVER_STATE_K_IMAGE_SAVED,
    AP_SERVER_STATE_K_ERROR,
} ap_server_state_t;

typedef enum {
    AP_SERVER_MODE_NONE = 0,
    AP_SERVER_MODE_AP,
    AP_SERVER_MODE_LAN,
} ap_server_mode_t;

/* Callback types (all function pointer + ctx). */
typedef void (*ap_server_state_cb_t)(int state, const char *message, void *ctx);
typedef void (*ap_server_image_received_cb_t)(const char *photo_id, void *ctx);
typedef void (*ap_server_settings_changed_cb_t)(int slideshow_interval_minutes, void *ctx);
typedef void (*ap_server_photos_changed_cb_t)(void *ctx);
typedef bool (*ap_server_show_photo_cb_t)(const char *photo_id, void *ctx);

typedef struct ap_transfer_server {
    httpd_handle_t server;
    esp_netif_t *ap_netif;
    volatile bool running;
    volatile bool starting;
    TaskHandle_t start_task;
    char ap_ip[32];
    ap_server_mode_t mode;

    ap_server_state_cb_t state_cb;
    void *state_cb_ctx;
    ap_server_image_received_cb_t image_received_cb;
    void *image_received_cb_ctx;
    ap_server_settings_changed_cb_t settings_changed_cb;
    void *settings_changed_cb_ctx;
    ap_server_photos_changed_cb_t photos_changed_cb;
    void *photos_changed_cb_ctx;
    ap_server_show_photo_cb_t show_photo_cb;
    void *show_photo_cb_ctx;
} ap_transfer_server_t;

/* Lifecycle. */
void ap_transfer_server_init(ap_transfer_server_t *server);
void ap_transfer_server_start(ap_transfer_server_t *server);
bool ap_transfer_server_start_lan(ap_transfer_server_t *server, const char *ip_address);
void ap_transfer_server_stop(ap_transfer_server_t *server);
bool ap_transfer_server_is_running(const ap_transfer_server_t *server);
bool ap_transfer_server_is_ap_mode(const ap_transfer_server_t *server);
bool ap_transfer_server_is_lan_mode(const ap_transfer_server_t *server);
const char *ap_transfer_server_get_ap_ip(const ap_transfer_server_t *server);

/* D3 idle auto-off support: every URI handler calls touch_activity();
 * idle_ms() returns time since the last request (0 before first). */
void ap_transfer_server_touch_activity(void);
int64_t ap_transfer_server_idle_ms(void);

/* D11 per-device auth token (8 hex chars, NVS-backed, generated on first
 * use). Empty string only if NVS is unavailable. With
 * CONFIG_TRANSFER_AUTH_ENABLE disabled (default build) it returns "" and
 * never generates or persists a token. */
const char *ap_transfer_server_get_token(void);

/* Callbacks. Passing NULL for cb is allowed (callback cleared). */
void ap_transfer_server_set_state_callback(ap_transfer_server_t *server, ap_server_state_cb_t cb, void *ctx);
void ap_transfer_server_set_image_received_callback(ap_transfer_server_t *server, ap_server_image_received_cb_t cb,
                                                    void *ctx);
void ap_transfer_server_set_settings_changed_callback(ap_transfer_server_t *server, ap_server_settings_changed_cb_t cb,
                                                      void *ctx);
void ap_transfer_server_set_photos_changed_callback(ap_transfer_server_t *server, ap_server_photos_changed_cb_t cb,
                                                    void *ctx);
void ap_transfer_server_set_show_photo_callback(ap_transfer_server_t *server, ap_server_show_photo_cb_t cb, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_NETWORK_AP_TRANSFER_SERVER_H_ */
