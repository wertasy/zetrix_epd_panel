#ifndef TEXT_CHUNKER_H_
#define TEXT_CHUNKER_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef void (*text_chunker_cb_t)(const char *chunk, void *ctx);

#define TEXT_CHUNKER_MAX_BUFFER 4096

typedef struct {
    text_chunker_cb_t callback;
    void *callback_ctx;

    char *buffer;
    size_t buffer_len;
    size_t buffer_cap;

    int64_t last_emit_time_ms;
} text_chunker_t;

void text_chunker_init(text_chunker_t *tc, text_chunker_cb_t cb, void *ctx);
void text_chunker_feed(text_chunker_t *tc, const char *text);
void text_chunker_flush(text_chunker_t *tc);
void text_chunker_reset(text_chunker_t *tc);
bool text_chunker_has_pending_data(const text_chunker_t *tc);
void text_chunker_destroy(text_chunker_t *tc);

#ifndef ESP_PLATFORM
void *test_malloc(size_t size);
#    define malloc test_malloc
#endif
#endif // TEXT_CHUNKER_H_
