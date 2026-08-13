#include "stream_pipeline.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "StreamPipeline";

static void on_chunk_cb(const char *chunk, void *ctx)
{
    stream_pipeline_t *sp = (stream_pipeline_t *)ctx;
    if (!sp || !sp->cb)
        return;
    sp->cb(chunk, sp->cb_ctx);
}

static void stream_pipeline_on_text(const char *text, void *ctx)
{
    stream_pipeline_t *sp = (stream_pipeline_t *)ctx;
    if (!sp)
        return;
    if (sp->mutex) {
        xSemaphoreTake(sp->mutex, portMAX_DELAY);
    }
    text_chunker_feed(&sp->chunker, text);
    if (sp->mutex) {
        xSemaphoreGive(sp->mutex);
    }
}

void stream_pipeline_init(stream_pipeline_t *sp, protocol_t *proto, text_chunk_cb_t cb, void *cb_ctx)
{
    if (!sp)
        return;
    memset(sp, 0, sizeof(*sp));
    sp->proto = proto;
    sp->cb = cb;
    sp->cb_ctx = cb_ctx;
    sp->mutex = xSemaphoreCreateMutex();

    text_chunker_init(&sp->chunker, on_chunk_cb, sp);

    if (sp->proto) {
        sp->proto->on_incoming_text = stream_pipeline_on_text;
        sp->proto->text_ctx = sp;
    }
}

void stream_pipeline_deinit(stream_pipeline_t *sp)
{
    if (!sp)
        return;

    if (sp->mutex) {
        xSemaphoreTake(sp->mutex, portMAX_DELAY);
    }
    text_chunker_destroy(&sp->chunker);
    if (sp->mutex) {
        xSemaphoreGive(sp->mutex);
        vSemaphoreDelete(sp->mutex);
        sp->mutex = NULL;
    }
}

void stream_pipeline_begin_stream(stream_pipeline_t *sp)
{
    if (!sp)
        return;

    if (sp->mutex) {
        xSemaphoreTake(sp->mutex, portMAX_DELAY);
    }

    sp->is_streaming = true;
    text_chunker_reset(&sp->chunker);

    if (sp->mutex) {
        xSemaphoreGive(sp->mutex);
    }

    ESP_LOGI(TAG, "Begin stream");
}

void stream_pipeline_feed_llm_text(stream_pipeline_t *sp, const char *chunk)
{
    if (!sp || !chunk)
        return;

    if (sp->mutex) {
        xSemaphoreTake(sp->mutex, portMAX_DELAY);
    }

    text_chunker_feed(&sp->chunker, chunk);

    if (sp->mutex) {
        xSemaphoreGive(sp->mutex);
    }
}

void stream_pipeline_end_stream(stream_pipeline_t *sp)
{
    if (!sp)
        return;

    if (sp->mutex) {
        xSemaphoreTake(sp->mutex, portMAX_DELAY);
    }

    text_chunker_flush(&sp->chunker);
    sp->is_streaming = false;

    if (sp->mutex) {
        xSemaphoreGive(sp->mutex);
    }

    ESP_LOGI(TAG, "End stream");
}

void stream_pipeline_reset(stream_pipeline_t *sp)
{
    if (!sp)
        return;

    if (sp->mutex) {
        xSemaphoreTake(sp->mutex, portMAX_DELAY);
    }

    sp->is_streaming = false;
    text_chunker_reset(&sp->chunker);

    if (sp->mutex) {
        xSemaphoreGive(sp->mutex);
    }

    ESP_LOGI(TAG, "Reset pipeline");
}
