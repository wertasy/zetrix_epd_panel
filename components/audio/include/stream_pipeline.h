#ifndef STREAM_PIPELINE_H_
#define STREAM_PIPELINE_H_

#ifdef ESP_PLATFORM
#    include <freertos/FreeRTOS.h>
#    include <freertos/semphr.h>
#else
#    include <pthread.h>
#    include <stdlib.h>
#    include <stdint.h>
#    ifndef SEMAPHORE_HANDLE_T_DEFINED
#        define SEMAPHORE_HANDLE_T_DEFINED
typedef pthread_mutex_t *SemaphoreHandle_t;
#    endif

static inline SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    pthread_mutex_t *mutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    if (mutex) {
        pthread_mutex_init(mutex, NULL);
    }
    return mutex;
}

static inline void vSemaphoreDelete(SemaphoreHandle_t mutex)
{
    if (mutex) {
        pthread_mutex_destroy(mutex);
        free(mutex);
    }
}
int test_xSemaphoreTake(SemaphoreHandle_t mutex, uint32_t delay);
int test_xSemaphoreGive(SemaphoreHandle_t mutex);
#define xSemaphoreTake test_xSemaphoreTake
#define xSemaphoreGive test_xSemaphoreGive
#    ifndef portMAX_DELAY
#        define portMAX_DELAY 0xFFFFFFFF
#    endif
#endif

#include "protocol.h"
#include "text_chunker.h"
#include "ui_manager.h"

typedef struct {
    protocol_t    *proto;
    text_chunker_t chunker;
    ui_manager_t  *ui;

    bool    is_streaming;
    int64_t last_ui_update_ms;

    SemaphoreHandle_t mutex;
} stream_pipeline_t;

void stream_pipeline_init(stream_pipeline_t *sp, protocol_t *proto, ui_manager_t *ui);
void stream_pipeline_deinit(stream_pipeline_t *sp);
void stream_pipeline_begin_stream(stream_pipeline_t *sp);
void stream_pipeline_feed_llm_text(stream_pipeline_t *sp, const char *chunk);
void stream_pipeline_end_stream(stream_pipeline_t *sp);
void stream_pipeline_reset(stream_pipeline_t *sp);

#endif // STREAM_PIPELINE_H_
