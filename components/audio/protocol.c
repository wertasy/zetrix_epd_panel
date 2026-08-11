#include "protocol.h"
#include <esp_log.h>
#include <esp_websocket_client.h>
#include <cJSON.h>
#include "settings.h"
#include "system_info.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "Protocol";

// Forward declarations
static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
static void handle_idle_timeout(void *arg);

void protocol_init(protocol_t *p)
{
    if (!p)
        return;
    memset(p, 0, sizeof(*p));
    p->server_sample_rate    = 24000;
    p->server_frame_duration = 60;

    // Get device ID
    system_info_get_device_id(p->device_id, sizeof(p->device_id));

    // Get WSS URL
    settings_handle_t ws_handle = settings_open("websocket", false);
    if (ws_handle) {
        settings_get_string(ws_handle, "url", p->wss_url, sizeof(p->wss_url), "");
        settings_close(ws_handle);
    }

    /* WSS URL from NVS or Kconfig default (empty = disabled). */
}

bool protocol_start(protocol_t *p)
{
    if (!p)
        return false;
    if (p->ws_client)
        return true;

    /* Skip when no WSS URL is configured (xiaozhi AI disabled). */
    if (p->wss_url[0] == '\0' || strcmp(p->wss_url, "wss://api.example.com/v1/chat") == 0) {
        return false;
    }

    ESP_LOGI(TAG, "Starting WebSocket client: %s", p->wss_url);

    esp_websocket_client_config_t ws_cfg = {
        .uri = p->wss_url,
    };

    p->ws_client = esp_websocket_client_init(&ws_cfg);
    if (!p->ws_client) {
        ESP_LOGE(TAG, "Failed to initialize WebSocket client");
        return false;
    }

    esp_websocket_register_events(p->ws_client, WEBSOCKET_EVENT_ANY, websocket_event_handler, p);

    esp_err_t err = esp_websocket_client_start(p->ws_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WebSocket client: %d", err);
        esp_websocket_client_destroy(p->ws_client);
        p->ws_client = NULL;
        return false;
    }

    // Setup idle timer
    esp_timer_create_args_t timer_args = {.callback = handle_idle_timeout, .arg = p, .name = "protocol_idle"};
    esp_timer_create(&timer_args, &p->idle_timer);

    return true;
}

static void handle_idle_timeout(void *arg)
{
    protocol_t *p = (protocol_t *)arg;
    if (!p)
        return;

    ESP_LOGW(TAG, "Idle timeout reached");
    if (p->on_idle_timeout) {
        p->on_idle_timeout(p->idle_ctx);
    }
}

void protocol_refresh_idle_timer(protocol_t *p)
{
    if (!p || !p->idle_timer)
        return;
    esp_timer_stop(p->idle_timer);
    // 15 seconds idle timeout
    esp_timer_start_once(p->idle_timer, 15000 * 1000ULL);
    p->last_rx_ms = esp_timer_get_time() / 1000;
}

static void process_incoming_text(protocol_t *p, const char *text_data, size_t len)
{
    // Parse as JSON first
    cJSON *root = cJSON_ParseWithLength(text_data, len);
    if (!root) {
        // Not valid JSON, just pass as text
        char *safe_text = malloc(len + 1);
        if (safe_text) {
            memcpy(safe_text, text_data, len);
            safe_text[len] = '\0';
            if (p->on_incoming_text) {
                p->on_incoming_text(safe_text, p->text_ctx);
            }
            free(safe_text);
        }
        return;
    }

    // JSON callback
    if (p->on_incoming_json) {
        p->on_incoming_json(root, p->json_ctx);
    }

    // Try to extract text/speech or streaming fields
    // e.g. for Xiaozhi protocol: {"type": "text", "text": "..."}
    cJSON *type_item = cJSON_GetObjectItem(root, "type");
    if (type_item && type_item->valuestring) {
        if (strcmp(type_item->valuestring, "text") == 0) {
            cJSON *text_item = cJSON_GetObjectItem(root, "text");
            if (text_item && text_item->valuestring && p->on_incoming_text) {
                p->on_incoming_text(text_item->valuestring, p->text_ctx);
            }
        }
    }

    cJSON_Delete(root);
}

static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    protocol_t                 *p    = (protocol_t *)handler_args;
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WebSocket connected");
        p->ws_connected = true;
        protocol_refresh_idle_timer(p);
        if (p->on_connected) {
            p->on_connected(p->connected_ctx);
        }
        break;

    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "WebSocket disconnected");
        p->ws_connected = false;
        p->last_opcode = 0;
        if (p->idle_timer) {
            esp_timer_stop(p->idle_timer);
        }
        if (p->on_disconnected) {
            p->on_disconnected(p->disconnected_ctx);
        }
        break;

    case WEBSOCKET_EVENT_DATA:
        protocol_refresh_idle_timer(p);
        {
            int resolved_op = data->op_code;
            if (resolved_op == 0x00) {
                resolved_op = p->last_opcode;
            } else {
                p->last_opcode = resolved_op;
            }

            if (resolved_op == 0x08) {
                ESP_LOGI(TAG, "Received close frame");
            } else if (resolved_op == 0x01) { // Text
                process_incoming_text(p, data->data_ptr, data->data_len);
            } else if (resolved_op == 0x02) { // Binary (audio)
                // Decode or parse binary protocol envelope if needed.
                // Currently placeholder as audio is parked.
                if (p->on_incoming_audio) {
                    audio_stream_packet_t pkt = {.sample_rate    = p->server_sample_rate,
                                                 .frame_duration = p->server_frame_duration,
                                                 .timestamp      = 0,
                                                 .payload        = (const uint8_t *)data->data_ptr,
                                                 .payload_len    = data->data_len};
                    p->on_incoming_audio(&pkt, p->audio_ctx);
                }
            }
        }
        break;

    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "WebSocket error");
        if (p->on_network_error) {
            p->on_network_error("WebSocket error", p->err_ctx);
        }
        break;
    }
}

bool protocol_open_audio_channel(protocol_t *p)
{
    // Under skeleton mode, just returns true if connected
    return p && p->ws_connected;
}

void protocol_close_audio_channel(protocol_t *p)
{
    (void)p;
}

bool protocol_is_audio_channel_opened(const protocol_t *p)
{
    return p && p->ws_connected;
}

bool protocol_send_text(protocol_t *p, const char *text)
{
    if (!p || !p->ws_client || !p->ws_connected)
        return false;

    // Construct a standard JSON packet wrapper
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return false;
    }
    cJSON_AddStringToObject(root, "type", "text");
    cJSON_AddStringToObject(root, "text", text);
    cJSON_AddStringToObject(root, "device_id", p->device_id);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_str) {
        return false;
    }

    int len  = strlen(json_str);
    int sent = esp_websocket_client_send_text(p->ws_client, json_str, len, portMAX_DELAY);
    free(json_str);

    return sent == len;
}

void protocol_send_start_listening(protocol_t *p, listening_mode_t m)
{
    if (!p || !p->ws_client || !p->ws_connected)
        return;

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return;
    }
    cJSON_AddStringToObject(root, "type", "start_listening");
    cJSON_AddNumberToObject(root, "mode", (double)m);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_str) {
        return;
    }

    esp_websocket_client_send_text(p->ws_client, json_str, strlen(json_str), portMAX_DELAY);
    free(json_str);
}

void protocol_send_stop_listening(protocol_t *p)
{
    if (!p || !p->ws_client || !p->ws_connected)
        return;

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return;
    }
    cJSON_AddStringToObject(root, "type", "stop_listening");

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_str) {
        return;
    }

    esp_websocket_client_send_text(p->ws_client, json_str, strlen(json_str), portMAX_DELAY);
    free(json_str);
}

void protocol_send_abort_speaking(protocol_t *p, abort_reason_t r)
{
    if (!p || !p->ws_client || !p->ws_connected)
        return;

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return;
    }
    cJSON_AddStringToObject(root, "type", "abort_speaking");
    cJSON_AddNumberToObject(root, "reason", (double)r);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_str) {
        return;
    }

    esp_websocket_client_send_text(p->ws_client, json_str, strlen(json_str), portMAX_DELAY);
    free(json_str);
}
void protocol_stop(protocol_t *p)
{
    if (!p)
        return;

    if (p->ws_client) {
        esp_websocket_client_stop(p->ws_client);
        p->ws_connected = false;
    }

    if (p->idle_timer) {
        esp_timer_stop(p->idle_timer);
    }
}

void protocol_destroy(protocol_t *p)
{
    if (!p)
        return;

    protocol_stop(p);

    if (p->ws_client) {
        esp_websocket_client_destroy(p->ws_client);
        p->ws_client = NULL;
    }

    if (p->idle_timer) {
        esp_timer_delete(p->idle_timer);
        p->idle_timer = NULL;
    }
}
