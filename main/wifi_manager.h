#ifndef MAIN_WIFI_MANAGER_H_
#define MAIN_WIFI_MANAGER_H_

#include <stdbool.h>
#include <esp_err.h>

typedef enum {
    WIFI_EVENT_DISCONNECTED,
    WIFI_EVENT_CONNECTING,
    WIFI_EVENT_CONNECTED,
    WIFI_EVENT_GOT_IP,
} wifi_manager_event_t;

typedef void (*wifi_event_callback_t)(wifi_manager_event_t event, void* user_data);

void wifi_manager_init(void);
esp_err_t wifi_manager_connect(const char* ssid, const char* password);
esp_err_t wifi_manager_disconnect(void);
bool wifi_manager_is_connected(void);
void wifi_manager_get_ip(char* out_ip, size_t max_len);
void wifi_manager_get_ssid(char* out_ssid, size_t max_len);
void wifi_manager_register_callback(wifi_event_callback_t cb, void* user_data);

#endif // MAIN_WIFI_MANAGER_H_
