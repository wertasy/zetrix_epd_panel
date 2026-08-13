#ifndef PROTOCOL_H_
#define PROTOCOL_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <cJSON.h>
#include <esp_timer.h>

typedef struct {
    int sample_rate;
    int frame_duration;
    uint32_t timestamp;
    const uint8_t *payload;
    size_t payload_len;
} audio_stream_packet_t;

typedef enum {
    PROTO_LISTENING_AUTO,
    PROTO_LISTENING_MANUAL,
    PROTO_LISTENING_REALTIME
} listening_mode_t;

typedef enum {
    PROTO_ABORT_NONE,
    PROTO_ABORT_WAKE_WORD
} abort_reason_t;

typedef struct protocol protocol_t;

typedef void (*proto_incoming_json_cb)(const cJSON *root, void *ctx);
typedef void (*proto_incoming_audio_cb)(const audio_stream_packet_t *pkt, void *ctx);
typedef void (*proto_text_cb)(const char *text, void *ctx);
typedef void (*proto_state_cb)(void *ctx);
typedef void (*proto_error_cb)(const char *msg, void *ctx);

struct protocol {
    int server_sample_rate;
    int server_frame_duration;
    char session_id[40];
    char device_id[32];
    char wss_url[128];

    proto_incoming_json_cb on_incoming_json;
    void *json_ctx;
    proto_incoming_audio_cb on_incoming_audio;
    void *audio_ctx;
    proto_text_cb on_incoming_text;
    void *text_ctx;
    proto_state_cb on_connected;
    void *connected_ctx;
    proto_state_cb on_disconnected;
    void *disconnected_ctx;
    proto_state_cb on_idle_timeout;
    void *idle_ctx;
    proto_error_cb on_network_error;
    void *err_ctx;

    // WebSocket Client
    void *ws_client;
    bool ws_connected;
    esp_timer_handle_t idle_timer;
    int64_t last_rx_ms;
    int last_opcode;
};

void protocol_init(protocol_t *p);
bool protocol_start(protocol_t *p);
bool protocol_open_audio_channel(protocol_t *p);
void protocol_close_audio_channel(protocol_t *p);
bool protocol_send_text(protocol_t *p, const char *text);
void protocol_send_start_listening(protocol_t *p, listening_mode_t m);
void protocol_send_stop_listening(protocol_t *p);
void protocol_send_abort_speaking(protocol_t *p, abort_reason_t r);
void protocol_refresh_idle_timer(protocol_t *p);
bool protocol_is_audio_channel_opened(const protocol_t *p);
void protocol_stop(protocol_t *p);
void protocol_destroy(protocol_t *p);

#ifndef ESP_PLATFORM
#    define cJSON_CreateObject test_cJSON_CreateObject
#    define cJSON_PrintUnformatted test_cJSON_PrintUnformatted
cJSON *test_cJSON_CreateObject(void);
char *test_cJSON_PrintUnformatted(const cJSON *item);
#endif
#endif // PROTOCOL_H_
