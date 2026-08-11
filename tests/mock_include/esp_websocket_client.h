#ifndef MOCK_ESP_WEBSOCKET_CLIENT_H_
#define MOCK_ESP_WEBSOCKET_CLIENT_H_

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#ifndef portMAX_DELAY
#define portMAX_DELAY 0xFFFFFFFF
#endif


// Events
#define WEBSOCKET_EVENT_ANY -1
typedef enum {
    WEBSOCKET_EVENT_CONNECTED,
    WEBSOCKET_EVENT_DISCONNECTED,
    WEBSOCKET_EVENT_DATA,
    WEBSOCKET_EVENT_ERROR
} esp_websocket_event_id_t;

typedef struct {
    int         op_code;
    const char *data_ptr;
    int         data_len;
} esp_websocket_event_data_t;

typedef struct {
    const char *uri;
} esp_websocket_client_config_t;

typedef void *esp_event_base_t;

typedef void (*esp_event_handler_t)(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);

void *esp_websocket_client_init(const esp_websocket_client_config_t *config);
esp_err_t esp_websocket_register_events(void *client, int event_type, esp_event_handler_t callback, void *context);
esp_err_t esp_websocket_client_start(void *client);
esp_err_t esp_websocket_client_stop(void *client);
esp_err_t esp_websocket_client_destroy(void *client);
int esp_websocket_client_send_text(void *client, const char *data, int len, uint32_t timeout);

#endif
