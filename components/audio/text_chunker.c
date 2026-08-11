#include "text_chunker.h"
#include <esp_timer.h>
#include <esp_log.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "TextChunker";

#define MIN_CHUNK_INTERVAL_MS 300
#define MAX_CHUNK_SIZE 100
#define MIN_CHUNK_SIZE 5

static bool is_sentence_end_punctuation(char ch)
{
    switch (ch) {
    case '.':
    case '!':
    case '?':
    case ';':
    case ':':
    case '\n':
    case '\r':
        return true;
    default:
        return false;
    }
}

static bool is_chinese_punctuation(const char *str, size_t len, size_t pos)
{
    if (pos + 2 >= len)
        return false;

    unsigned char b0 = (unsigned char)str[pos];
    unsigned char b1 = (unsigned char)str[pos + 1];
    unsigned char b2 = (unsigned char)str[pos + 2];

    // Check for 。 (E3 80 82)
    if (b0 == 0xE3 && b1 == 0x80 && b2 == 0x82) {
        return true;
    }

    // Check for ！？；： (EF BC xx)
    if (b0 == 0xEF && b1 == 0xBC) {
        switch (b2) {
        case 0x81: // ！
        case 0x9F: // ？
        case 0x9B: // ；
        case 0x9A: // ：
            return true;
        default:
            return false;
        }
    }
    return false;
}

static size_t find_sentence_boundary(const char *str, size_t len)
{
    size_t best_boundary = 0;
    for (size_t i = 0; i < len; ++i) {
        unsigned char ch = (unsigned char)str[i];
        if (ch < 0x80) {
            if (is_sentence_end_punctuation(str[i])) {
                best_boundary = i + 1;
                if (best_boundary >= MIN_CHUNK_SIZE) {
                    return best_boundary;
                }
            }
        } else {
            if (is_chinese_punctuation(str, len, i)) {
                best_boundary = i + 3;
                if (best_boundary >= MIN_CHUNK_SIZE) {
                    return best_boundary;
                }
            }
        }
    }
    return best_boundary;
}

void text_chunker_init(text_chunker_t *tc, text_chunker_cb_t cb, void *ctx)
{
    if (!tc)
        return;
    tc->callback          = cb;
    tc->callback_ctx      = ctx;
    tc->buffer            = malloc(TEXT_CHUNKER_MAX_BUFFER);
    tc->buffer_len        = 0;
    tc->buffer_cap        = tc->buffer ? TEXT_CHUNKER_MAX_BUFFER : 0;
    tc->last_emit_time_ms = 0;
    if (tc->buffer) {
        tc->buffer[0] = '\0';
    }
}

void text_chunker_destroy(text_chunker_t *tc)
{
    if (!tc)
        return;
    if (tc->buffer) {
        free(tc->buffer);
        tc->buffer = NULL;
    }
    tc->buffer_len = 0;
    tc->buffer_cap = 0;
}

static bool try_emit_chunk_ex(text_chunker_t *tc, bool force)
{
    if (!tc || tc->buffer_len == 0 || !tc->callback) {
        return false;
    }

    int64_t now_ms  = esp_timer_get_time() / 1000;
    int64_t elapsed = now_ms - tc->last_emit_time_ms;

    if (!force && elapsed < MIN_CHUNK_INTERVAL_MS && tc->last_emit_time_ms > 0) {
        return false;
    }

    size_t boundary = find_sentence_boundary(tc->buffer, tc->buffer_len);

    if (boundary == 0 && (tc->buffer_len > MAX_CHUNK_SIZE || force)) {
        boundary = tc->buffer_len > MAX_CHUNK_SIZE ? MAX_CHUNK_SIZE : tc->buffer_len;
        while (boundary > 0 && ((unsigned char)tc->buffer[boundary] & 0xC0) == 0x80) {
            boundary--;
        }
        if (boundary == 0) {
            boundary = tc->buffer_len > MAX_CHUNK_SIZE ? MAX_CHUNK_SIZE : tc->buffer_len;
        }
    }

    if (boundary > 0 && boundary <= tc->buffer_len) {
        char *chunk = malloc(boundary + 1);
        if (!chunk) {
            return false;
        }
        memcpy(chunk, tc->buffer, boundary);
        chunk[boundary] = '\0';
        tc->callback(chunk, tc->callback_ctx);
        free(chunk);

        memmove(tc->buffer, tc->buffer + boundary, tc->buffer_len - boundary);
        tc->buffer_len -= boundary;
        tc->buffer[tc->buffer_len] = '\0';
        tc->last_emit_time_ms      = now_ms;

        return true;
    }

    return false;
}

static bool try_emit_chunk(text_chunker_t *tc)
{
    return try_emit_chunk_ex(tc, false);
}

void text_chunker_feed(text_chunker_t *tc, const char *text)
{
    if (!tc || !text || !tc->buffer)
        return;

    size_t text_len = strlen(text);
    if (text_len == 0)
        return;

    size_t processed = 0;
    while (processed < text_len) {
        while (try_emit_chunk(tc)) {
        }

        size_t available = tc->buffer_cap - 1 - tc->buffer_len;
        if (available == 0) {
            if (!try_emit_chunk_ex(tc, true)) {
                ESP_LOGE(TAG, "Buffer full and force emit failed. Truncating input.");
                break;
            }
            available = tc->buffer_cap - 1 - tc->buffer_len;
            if (available == 0) {
                break;
            }
        }

        size_t to_copy = text_len - processed;
        if (to_copy > available) {
            to_copy = available;
            while (to_copy > 0 && ((unsigned char)text[processed + to_copy] & 0xC0) == 0x80) {
                to_copy--;
            }
            if (to_copy == 0) {
                break;
            }
        }

        memcpy(tc->buffer + tc->buffer_len, text + processed, to_copy);
        tc->buffer_len += to_copy;
        tc->buffer[tc->buffer_len] = '\0';
        processed += to_copy;
    }

    while (try_emit_chunk(tc)) {
    }
}


void text_chunker_flush(text_chunker_t *tc)
{
    if (!tc)
        return;
    if (tc->buffer_len > 0 && tc->callback) {
        tc->callback(tc->buffer, tc->callback_ctx);
        tc->buffer_len = 0;
        if (tc->buffer) {
            tc->buffer[0] = '\0';
        }
    }
    tc->last_emit_time_ms = 0;
}

void text_chunker_reset(text_chunker_t *tc)
{
    if (!tc)
        return;
    tc->buffer_len = 0;
    if (tc->buffer) {
        tc->buffer[0] = '\0';
    }
    tc->last_emit_time_ms = 0;
}

bool text_chunker_has_pending_data(const text_chunker_t *tc)
{
    return tc && tc->buffer_len > 0;
}
