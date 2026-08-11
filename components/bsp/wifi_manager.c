#include "wifi_manager.h"
#include <string.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

static const char *TAG = "wifi_mgr";

static wifi_event_callback_t s_callback           = NULL;
static void                 *s_callback_user_data = NULL;
static bool                  s_connected          = false;
static char                  s_ip_address[32]     = "0.0.0.0";
static char                  s_ssid[32]           = {0};
static int                   s_retry_count        = 0;

static SemaphoreHandle_t s_wifi_mutex = NULL;

static void lock_wifi(void)
{
    if (s_wifi_mutex) {
        xSemaphoreTake(s_wifi_mutex, portMAX_DELAY);
    }
}

static void unlock_wifi(void)
{
    if (s_wifi_mutex) {
        xSemaphoreGive(s_wifi_mutex);
    }
}
#define MAX_RETRY 5
#define RECONNECT_DELAY_MS 2000

static esp_timer_handle_t s_reconnect_timer = NULL;

static void reconnect_timer_callback(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "reconnect timer fired, attempting esp_wifi_connect()");
    esp_wifi_connect();
}

static void schedule_reconnect(void)
{
    if (!s_reconnect_timer) {
        const esp_timer_create_args_t timer_args = {
            .callback              = reconnect_timer_callback,
            .arg                   = NULL,
            .dispatch_method       = ESP_TIMER_TASK,
            .name                  = "wifi_reconn",
            .skip_unhandled_events = true,
        };
        if (esp_timer_create(&timer_args, &s_reconnect_timer) != ESP_OK) {
            ESP_LOGE(TAG, "failed to create reconnect timer");
            return;
        }
    }
    esp_timer_stop(s_reconnect_timer);
    esp_timer_start_once(s_reconnect_timer, RECONNECT_DELAY_MS * 1000ULL);
    ESP_LOGI(TAG, "reconnect scheduled in %d ms (non-blocking)", RECONNECT_DELAY_MS);
}

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        lock_wifi();
        s_retry_count = 0;
        unlock_wifi();
        esp_wifi_connect();
        if (s_callback)
            s_callback(WIFI_EVENT_CONNECTING, s_callback_user_data);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disconnected_data = (wifi_event_sta_disconnected_t *)event_data;
        uint8_t                        reason            = disconnected_data ? disconnected_data->reason : 0;
        ESP_LOGW(TAG, "Disconnected from AP, reason code: %d", reason);
        lock_wifi();
        s_connected = false;
        strcpy(s_ip_address, "0.0.0.0");
        int retry = s_retry_count;
        if (s_retry_count < MAX_RETRY) {
            schedule_reconnect();
            s_retry_count++;
            retry = s_retry_count;
        }
        unlock_wifi();

        if (retry < MAX_RETRY) {
            ESP_LOGI(TAG, "retry to connect to the AP... (%d/%d)", retry, MAX_RETRY);
            if (s_callback)
                s_callback(WIFI_EVENT_CONNECTING, s_callback_user_data);
        } else {
            ESP_LOGW(TAG, "connect to the AP failed");
            if (s_callback)
                s_callback(WIFI_EVENT_DISCONNECTED, s_callback_user_data);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        esp_ip4_addr_t     ip    = event->ip_info.ip;
        char ip_str[32];
        lock_wifi();
        snprintf(s_ip_address, sizeof(s_ip_address), IPSTR, IP2STR(&ip));
        strcpy(ip_str, s_ip_address);
        s_connected   = true;
        s_retry_count = 0;
        unlock_wifi();
        ESP_LOGI(TAG, "got ip: %s", ip_str);
        /* Enable Wi-Fi Modem Sleep now that we have an IP — saves power
         * between beacons by gating the RF/BB between listen intervals. */
        esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
        if (s_callback) {
            s_callback(WIFI_EVENT_CONNECTED, s_callback_user_data);
            s_callback(WIFI_EVENT_GOT_IP, s_callback_user_data);
        }
    }
}

void wifi_manager_init(void)
{
    if (!s_wifi_mutex) {
        s_wifi_mutex = xSemaphoreCreateMutex();
    }
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
    ESP_ERROR_CHECK(
        esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
}

esp_err_t wifi_manager_connect(const char *ssid, const char *password)
{
    if (!ssid)
        return ESP_ERR_INVALID_ARG;

    if (s_reconnect_timer) {
        esp_timer_stop(s_reconnect_timer);
    }

    lock_wifi();
    strncpy(s_ssid, ssid, sizeof(s_ssid) - 1);
    s_ssid[sizeof(s_ssid) - 1] = '\0';
    s_retry_count = 0;
    unlock_wifi();

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    if (password) {
        strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_LOGI(TAG, "Connecting to AP SSID: %s", ssid);
    return esp_wifi_start();
}

esp_err_t wifi_manager_disconnect(void)
{
    if (s_reconnect_timer) {
        esp_timer_stop(s_reconnect_timer);
    }

    lock_wifi();
    s_connected = false;
    strcpy(s_ip_address, "0.0.0.0");
    s_retry_count = MAX_RETRY;
    unlock_wifi();

    esp_wifi_disconnect();
    return esp_wifi_stop();
}

bool wifi_manager_is_connected(void)
{
    lock_wifi();
    bool conn = s_connected;
    unlock_wifi();
    return conn;
}

void wifi_manager_get_ip(char *out_ip, size_t max_len)
{
    if (out_ip && max_len > 0) {
        lock_wifi();
        strncpy(out_ip, s_ip_address, max_len - 1);
        unlock_wifi();
        out_ip[max_len - 1] = '\0';
    }
}

void wifi_manager_get_ssid(char *out_ssid, size_t max_len)
{
    if (out_ssid && max_len > 0) {
        lock_wifi();
        strncpy(out_ssid, s_ssid, max_len - 1);
        unlock_wifi();
        out_ssid[max_len - 1] = '\0';
    }
}

void wifi_manager_register_callback(wifi_event_callback_t cb, void *user_data)
{
    s_callback           = cb;
    s_callback_user_data = user_data;
}
