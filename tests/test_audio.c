#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <pthread.h>

// Mutex instrumentation
static int g_mutex_locks = 0;
static int g_mutex_unlocks = 0;

// xSemaphoreTake and xSemaphoreGive wrappers are implemented below to count lock calls

// Include target/mock headers first
#include <cJSON.h>
#include "esp_websocket_client.h"
#include "protocol.h"
#include "text_chunker.h"
#include "stream_pipeline.h"

// Undefine the mock redirections so we can define the wrappers and call real APIs
#undef malloc
#undef cJSON_CreateObject
#undef cJSON_PrintUnformatted

void *malloc(size_t size);
cJSON* cJSON_CreateObject(void);
char* cJSON_PrintUnformatted(const cJSON* item);

int test_xSemaphoreTake(SemaphoreHandle_t mutex, uint32_t delay) {
    g_mutex_locks++;
    if (mutex) {
        return pthread_mutex_lock(mutex) == 0;
    }
    return 0;
}

int test_xSemaphoreGive(SemaphoreHandle_t mutex) {
    g_mutex_unlocks++;
    if (mutex) {
        return pthread_mutex_unlock(mutex) == 0;
    }
    return 0;
}

// Global failure flags
static bool g_malloc_fail = false;
static bool g_cjson_fail = false;

// Mock implementations
void *test_malloc(size_t size) {
    if (g_malloc_fail) {
        return NULL;
    }
    return malloc(size);
}

cJSON* test_cJSON_CreateObject(void) {
    if (g_cjson_fail) return NULL;
    return cJSON_CreateObject();
}

char* test_cJSON_PrintUnformatted(const cJSON* item) {
    if (g_cjson_fail) return NULL;
    return cJSON_PrintUnformatted(item);
}

// Implement ESP mock functions
// esp_timer
struct host_timer {
    esp_timer_cb_t callback;
    void *arg;
    bool active;
};

esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *out_handle) {
    struct host_timer *t = malloc(sizeof(struct host_timer));
    t->callback = args->callback;
    t->arg = args->arg;
    t->active = false;
    *out_handle = t;
    return 0;
}

esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeout_us) {
    timer->active = true;
    return 0;
}

esp_err_t esp_timer_stop(esp_timer_handle_t timer) {
    timer->active = false;
    return 0;
}

esp_err_t esp_timer_delete(esp_timer_handle_t timer) {
    free(timer);
    return 0;
}

int64_t esp_timer_get_time(void) {
    return 1000000;
}

// esp_websocket_client
struct mock_ws_client {
    esp_event_handler_t callback;
    void *context;
    bool started;
    bool stopped;
};

void *esp_websocket_client_init(const esp_websocket_client_config_t *config) {
    struct mock_ws_client *c = malloc(sizeof(struct mock_ws_client));
    memset(c, 0, sizeof(*c));
    return c;
}

esp_err_t esp_websocket_register_events(void *client, int event_type, esp_event_handler_t callback, void *context) {
    struct mock_ws_client *c = (struct mock_ws_client *)client;
    c->callback = callback;
    c->context = context;
    return 0;
}

esp_err_t esp_websocket_client_start(void *client) {
    struct mock_ws_client *c = (struct mock_ws_client *)client;
    c->started = true;
    return 0;
}

esp_err_t esp_websocket_client_stop(void *client) {
    struct mock_ws_client *c = (struct mock_ws_client *)client;
    c->stopped = true;
    return 0;
}

esp_err_t esp_websocket_client_destroy(void *client) {
    free(client);
    return 0;
}

static char g_last_sent_text[1024] = {0};
static int g_send_text_count = 0;

int esp_websocket_client_send_text(void *client, const char *data, int len, uint32_t timeout) {
    g_send_text_count++;
    if (data && len < sizeof(g_last_sent_text)) {
        memcpy(g_last_sent_text, data, len);
        g_last_sent_text[len] = '\0';
    }
    return len;
}

// settings
void* settings_open(const char* namespace, bool read_only) {
    return (void*)1;
}

bool settings_get_string(void* handle, const char* key, char* out_val, size_t max_len, const char* default_val) {
    strncpy(out_val, "wss://api.example.com/v1/chat_real", max_len);
    return true;
}

void settings_close(void* handle) {}

// system_info
void system_info_get_device_id(char* out_id, size_t max_len) {
    strncpy(out_id, "mock_device_id", max_len);
}

// ui_manager
static char g_ui_chat_text[4096] = {0};
static bool g_ui_chat_stream_begun = false;
static bool g_ui_chat_stream_ended = false;

bool ui_manager_append_chat_text(ui_manager_t *ui, const char *text) {
    strcat(g_ui_chat_text, text);
    return true;
}

void ui_manager_begin_chat_stream(ui_manager_t *ui) {
    g_ui_chat_stream_begun = true;
}

void ui_manager_end_chat_stream(ui_manager_t *ui) {
    g_ui_chat_stream_ended = true;
}

// Chunker Callback
static char g_chunked_text[4096] = {0};
static int g_chunks_count = 0;

void chunker_callback(const char *chunk, void *ctx) {
    g_chunks_count++;
    strcat(g_chunked_text, chunk);
}

// Unit Tests
void test_text_chunker_basic(void) {
    text_chunker_t tc;
    text_chunker_init(&tc, chunker_callback, NULL);
    g_chunked_text[0] = '\0';
    g_chunks_count = 0;

    text_chunker_feed(&tc, "Hello world. This is a test.");
    text_chunker_flush(&tc);
    assert(g_chunks_count == 2);
    assert(strcmp(g_chunked_text, "Hello world. This is a test.") == 0);

    text_chunker_destroy(&tc);
}

void test_text_chunker_malloc_fail(void) {
    text_chunker_t tc;
    text_chunker_init(&tc, chunker_callback, NULL);
    g_chunked_text[0] = '\0';
    g_chunks_count = 0;

    // First feed some text
    text_chunker_feed(&tc, "Hello world. ");
    assert(g_chunks_count == 1);

    // Make malloc fail
    g_malloc_fail = true;
    text_chunker_feed(&tc, "This is a test.");
    // The second sentence boundary was found but malloc failed, so it should not be emitted
    // and should remain in the buffer
    assert(g_chunks_count == 1);

    // Restore malloc
    g_malloc_fail = false;
    // Flushing should emit the rest
    text_chunker_flush(&tc);
    assert(g_chunks_count == 2);
    assert(strcmp(g_chunked_text, "Hello world. This is a test.") == 0);

    text_chunker_destroy(&tc);
}

void test_protocol_cjson_null_checks(void) {
    protocol_t p;
    protocol_init(&p);

    // Set up url to allow start
    strcpy(p.wss_url, "wss://api.example.com/v1/chat_real");
    assert(protocol_start(&p) == true);
    p.ws_connected = true;

    // Normal send
    g_send_text_count = 0;
    bool ok = protocol_send_text(&p, "hello");
    assert(ok == true);
    assert(g_send_text_count == 1);

    // Make cJSON fail
    g_cjson_fail = true;
    ok = protocol_send_text(&p, "hello");
    assert(ok == false); // Should check NULL and fail gracefully

    // Test starts
    g_send_text_count = 0;
    protocol_send_start_listening(&p, PROTO_LISTENING_AUTO);
    assert(g_send_text_count == 0); // cJSON failed, should not send

    g_cjson_fail = false;
    protocol_send_start_listening(&p, PROTO_LISTENING_AUTO);
    assert(g_send_text_count == 1); // should send now

    protocol_destroy(&p);
}

void test_protocol_continuation_frames(void) {
    protocol_t p;
    protocol_init(&p);

    // Mock callbacks
    static char incoming_text_buf[256] = {0};
    void mock_incoming_text(const char *text, void *ctx) {
        strcpy(incoming_text_buf, text);
    }
    p.on_incoming_text = mock_incoming_text;

    // Set up url to allow start
    strcpy(p.wss_url, "wss://api.example.com/v1/chat_real");
    assert(protocol_start(&p) == true);

    // Simulate event handler connect first
    struct mock_ws_client *c = (struct mock_ws_client *)p.ws_client;
    assert(c != NULL);
    assert(c->callback != NULL);

    c->callback(&p, NULL, WEBSOCKET_EVENT_CONNECTED, NULL);
    assert(p.ws_connected == true);

    // Simulate Text Frame (opcode 0x01)
    esp_websocket_event_data_t data_text = {
        .op_code = 0x01,
        .data_ptr = "{\"type\":\"text\",\"text\":\"First part\"}",
        .data_len = 36
    };
    incoming_text_buf[0] = '\0';
    c->callback(&p, NULL, WEBSOCKET_EVENT_DATA, &data_text);
    assert(strcmp(incoming_text_buf, "First part") == 0);

    // Simulate Continuation Frame (opcode 0x00)
    // The continuation frame should resolve to last_opcode (0x01) and process the text
    esp_websocket_event_data_t data_cont = {
        .op_code = 0x00,
        .data_ptr = "{\"type\":\"text\",\"text\":\"Second part\"}",
        .data_len = 37
    };
    incoming_text_buf[0] = '\0';
    c->callback(&p, NULL, WEBSOCKET_EVENT_DATA, &data_cont);
    assert(strcmp(incoming_text_buf, "Second part") == 0);

    protocol_destroy(&p);
}

void test_stream_pipeline_mutex(void) {
    protocol_t p;
    stream_pipeline_t sp;

    protocol_init(&p);
    stream_pipeline_init(&sp, &p, NULL);

    g_mutex_locks = 0;
    g_mutex_unlocks = 0;

    // Check mutex usage on stream functions
    stream_pipeline_begin_stream(&sp);
    assert(g_mutex_locks == 1);
    assert(g_mutex_unlocks == 1);

    stream_pipeline_feed_llm_text(&sp, "hello. ");
    assert(g_mutex_locks == 2);
    assert(g_mutex_unlocks == 2);

    stream_pipeline_end_stream(&sp);
    assert(g_mutex_locks == 3);
    assert(g_mutex_unlocks == 3);

    stream_pipeline_reset(&sp);
    assert(g_mutex_locks == 4);
    assert(g_mutex_unlocks == 4);

    stream_pipeline_deinit(&sp);
    assert(g_mutex_locks == 5); // Take + release in deinit
    assert(g_mutex_unlocks == 5);

    protocol_destroy(&p);
}

int main(void) {
    printf("Starting Audio unit tests...\n");
    test_text_chunker_basic();
    test_text_chunker_malloc_fail();
    test_protocol_cjson_null_checks();
    test_protocol_continuation_frames();
    test_stream_pipeline_mutex();
    printf("All Audio unit tests successfully completed!\n");
    return 0;
}
