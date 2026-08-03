#include "wifi_manager.h"
#include <string.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

static const char* TAG = "wifi_mgr";

static wifi_event_callback_t s_callback = NULL;
static void* s_callback_user_data = NULL;
static bool s_connected = false;
static char s_ip_address[32] = "0.0.0.0";
static char s_ssid[32] = {0};
static int s_retry_count = 0;
#define MAX_RETRY 5

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        s_retry_count = 0;
        esp_wifi_connect();
        if (s_callback) s_callback(WIFI_EVENT_CONNECTING, s_callback_user_data);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        strcpy(s_ip_address, "0.0.0.0");
        if (s_retry_count < MAX_RETRY) {
            vTaskDelay(pdMS_TO_TICKS(2000));
            esp_wifi_connect();
            s_retry_count++;
            ESP_LOGI(TAG, "retry to connect to the AP... (%d/%d)", s_retry_count, MAX_RETRY);
            if (s_callback) s_callback(WIFI_EVENT_CONNECTING, s_callback_user_data);
        } else {
            ESP_LOGW(TAG, "connect to the AP failed");
            if (s_callback) s_callback(WIFI_EVENT_DISCONNECTED, s_callback_user_data);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        esp_ip4_addr_t ip = event->ip_info.ip;
        snprintf(s_ip_address, sizeof(s_ip_address), IPSTR, IP2STR(&ip));
        ESP_LOGI(TAG, "got ip: %s", s_ip_address);
        s_connected = true;
        s_retry_count = 0;
        if (s_callback) {
            s_callback(WIFI_EVENT_CONNECTED, s_callback_user_data);
            s_callback(WIFI_EVENT_GOT_IP, s_callback_user_data);
        }
    }
}

void wifi_manager_init(void) {
    ESP_LOGI(TAG, "Initializing Wi-Fi Station Mode");
    
    ESP_ERROR_CHECK(esp_netif_init());
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(err);
    }
    
    esp_netif_create_default_wifi_sta();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));
                                                        
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
}

esp_err_t wifi_manager_connect(const char* ssid, const char* password) {
    if (!ssid) return ESP_ERR_INVALID_ARG;
    
    strncpy(s_ssid, ssid, sizeof(s_ssid) - 1);
    s_ssid[sizeof(s_ssid) - 1] = '\0';
    
    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    if (password) {
        strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));
    }
    
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_LOGI(TAG, "Connecting to AP SSID: %s", ssid);
    s_retry_count = 0;
    return esp_wifi_start();
}

esp_err_t wifi_manager_disconnect(void) {
    s_connected = false;
    strcpy(s_ip_address, "0.0.0.0");
    s_retry_count = MAX_RETRY; 
    esp_wifi_disconnect();
    return esp_wifi_stop();
}

bool wifi_manager_is_connected(void) {
    return s_connected;
}

void wifi_manager_get_ip(char* out_ip, size_t max_len) {
    if (out_ip && max_len > 0) {
        strncpy(out_ip, s_ip_address, max_len - 1);
        out_ip[max_len - 1] = '\0';
    }
}

void wifi_manager_get_ssid(char* out_ssid, size_t max_len) {
    if (out_ssid && max_len > 0) {
        strncpy(out_ssid, s_ssid, max_len - 1);
        out_ssid[max_len - 1] = '\0';
    }
}

void wifi_manager_register_callback(wifi_event_callback_t cb, void* user_data) {
    s_callback = cb;
    s_callback_user_data = user_data;
}
