#ifndef COMPONENTS_BSP_INCLUDE_EPD_REFRESH_H_
#define COMPONENTS_BSP_INCLUDE_EPD_REFRESH_H_

#include <stdint.h>
#include <stdbool.h>
#include "display_types.h"

#ifdef ESP_PLATFORM
#    include <freertos/FreeRTOS.h>
#    include <freertos/task.h>
#    include <freertos/semphr.h>
#    include <freertos/event_groups.h>
#else
/* Host shims for FreeRTOS primitives */
#    include <pthread.h>
#    include <stdlib.h>

typedef pthread_mutex_t *SemaphoreHandle_t;
typedef struct host_event_group *EventGroupHandle_t;
typedef void *TaskHandle_t;

#    define portMAX_DELAY 0xFFFFFFFF
#    define pdTRUE 1
#    define pdFALSE 0
#    define pdPASS 1
#    define pdMS_TO_TICKS(ms) (ms)

typedef uint32_t EventBits_t;

struct host_event_group {
    uint32_t bits;
    pthread_mutex_t lock;
    pthread_cond_t cond;
};

static inline SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    pthread_mutex_t *m = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    if (m) {
        pthread_mutex_init(m, NULL);
    }
    return m;
}
static inline void vSemaphoreDelete(SemaphoreHandle_t m)
{
    if (m) {
        pthread_mutex_destroy(m);
        free(m);
    }
}
static inline int xSemaphoreTake(SemaphoreHandle_t m, uint32_t delay)
{
    (void)delay;
    if (m)
        return pthread_mutex_lock(m) == 0;
    return 0;
}
static inline int xSemaphoreGive(SemaphoreHandle_t m)
{
    if (m)
        return pthread_mutex_unlock(m) == 0;
    return 0;
}
static inline EventGroupHandle_t xEventGroupCreate(void)
{
    struct host_event_group *eg = (struct host_event_group *)calloc(1, sizeof(*eg));
    if (eg) {
        pthread_mutex_init(&eg->lock, NULL);
        pthread_cond_init(&eg->cond, NULL);
    }
    return eg;
}
static inline void vEventGroupDelete(EventGroupHandle_t eg)
{
    if (eg) {
        pthread_mutex_destroy(&eg->lock);
        pthread_cond_destroy(&eg->cond);
        free(eg);
    }
}
static inline EventBits_t xEventGroupWaitBits(EventGroupHandle_t eg, uint32_t bits, int clear, int wait_all,
                                              uint32_t timeout_ms)
{
    (void)wait_all;
    (void)timeout_ms;
    if (!eg)
        return 0;
    pthread_mutex_lock(&eg->lock);
    EventBits_t got = eg->bits & bits;
    if (got == 0) {
        /* Simple non-blocking: return 0 if no bits set */
    }
    if (clear)
        eg->bits &= ~bits;
    pthread_mutex_unlock(&eg->lock);
    return got;
}
static inline void xEventGroupSetBits(EventGroupHandle_t eg, uint32_t bits)
{
    if (!eg)
        return;
    pthread_mutex_lock(&eg->lock);
    eg->bits |= bits;
    pthread_cond_signal(&eg->cond);
    pthread_mutex_unlock(&eg->lock);
}
typedef int BaseType_t;
static inline BaseType_t xTaskCreatePinnedToCore(void (*task_fn)(void *), const char *name, uint32_t stack, void *arg,
                                                 uint32_t prio, TaskHandle_t *handle, int core)
{
    (void)name;
    (void)stack;
    (void)prio;
    (void)core;
    pthread_t thread;
    int ret = pthread_create(&thread, NULL, (void *(*)(void *))task_fn, arg);
    if (ret == 0) {
        pthread_detach(thread);
        if (handle)
            *handle = (TaskHandle_t)thread;
        return pdPASS;
    }
    return 0;
}
static inline void vTaskDelete(TaskHandle_t handle)
{
    (void)handle; /* pthreads are detached, nothing to do */
}
static inline void vTaskDelay(uint32_t ticks)
{
    (void)ticks;
}
#endif /* ESP_PLATFORM */

typedef enum {
    EPD_REFRESH_PARTIAL = 0,
    EPD_REFRESH_FULL,
} epd_refresh_mode_t;

typedef struct {
    int partial_count_threshold; /* Default: 10 */
    int task_stack_size; /* Default: 4096 */
    int task_priority; /* Default: 5 */
    int cpu_core; /* Default: 1 */
} epd_refresh_config_t;

typedef void (*epd_refresh_cb_t)(rawdraw_rect_t rect, epd_refresh_mode_t mode, void *user_data);

#define EPD_REFRESH_REQUEST_BIT (1u << 0)
#define EPD_REFRESH_STOP_BIT (1u << 1)

typedef struct {
    epd_refresh_config_t config;
    epd_refresh_cb_t callback;
    void *user_data;
    TaskHandle_t task_handle;
    SemaphoreHandle_t mutex;
    EventGroupHandle_t event_group;
    rawdraw_rect_t dirty_rect;
    bool has_dirty;
    int partial_count;
    bool full_refresh_pending;
    bool running;
    int screen_width;
    int screen_height;
} epd_refresh_scheduler_t;

void epd_refresh_init(epd_refresh_scheduler_t *s, epd_refresh_cb_t cb, void *user_data,
                      const epd_refresh_config_t *config, int screen_width, int screen_height);
void epd_refresh_start(epd_refresh_scheduler_t *s);
void epd_refresh_stop(epd_refresh_scheduler_t *s);
void epd_refresh_mark_dirty(epd_refresh_scheduler_t *s, rawdraw_rect_t rect);
void epd_refresh_trigger(epd_refresh_scheduler_t *s, bool urgent);
void epd_refresh_request_full(epd_refresh_scheduler_t *s);
int epd_refresh_get_partial_count(epd_refresh_scheduler_t *s);
void epd_refresh_reset_partial_count(epd_refresh_scheduler_t *s);
bool epd_refresh_is_running(epd_refresh_scheduler_t *s);

/* Internal: called by the task loop and exposed for testing */
void epd_refresh_process(epd_refresh_scheduler_t *s);

#endif /* COMPONENTS_BSP_INCLUDE_EPD_REFRESH_H_ */
