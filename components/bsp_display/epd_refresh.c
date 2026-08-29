#include "epd_refresh.h"
#include <string.h>

static void refresh_task_fn(void *arg)
{
    epd_refresh_scheduler_t *s = (epd_refresh_scheduler_t *)arg;
    if (!s)
        return;

    while (s->running) {
        EventBits_t bits = xEventGroupWaitBits(s->event_group, EPD_REFRESH_REQUEST_BIT | EPD_REFRESH_STOP_BIT, pdTRUE,
                                               pdFALSE, pdMS_TO_TICKS(100));

        if (bits & EPD_REFRESH_STOP_BIT)
            break;

        if ((bits & EPD_REFRESH_REQUEST_BIT) || s->has_dirty) {
            epd_refresh_process(s);
        }
    }
    vTaskSuspend(NULL);
}

void epd_refresh_init(epd_refresh_scheduler_t *s, epd_refresh_cb_t cb, void *user_data,
                      const epd_refresh_config_t *config, int screen_width, int screen_height)
{
    if (!s)
        return;
    memset(s, 0, sizeof(*s));
    s->callback = cb;
    s->user_data = user_data;
    s->mutex = xSemaphoreCreateMutex();
    s->event_group = xEventGroupCreate();
    s->task_handle = NULL;
    s->dirty_rect = (rawdraw_rect_t){0, 0, 0, 0};
    s->has_dirty = false;
    s->partial_count = 0;
    s->full_refresh_pending = false;
    s->running = false;
    s->screen_width = screen_width > 0 ? screen_width : 400;
    s->screen_height = screen_height > 0 ? screen_height : 300;

    if (config) {
        s->config = *config;
    } else {
        s->config.partial_count_threshold = 10;
        s->config.task_stack_size = 4096;
        s->config.task_priority = 5;
        s->config.cpu_core = 1;
    }
    if (s->config.partial_count_threshold <= 0)
        s->config.partial_count_threshold = 10;
}

void epd_refresh_start(epd_refresh_scheduler_t *s)
{
    if (!s || s->running || s->task_handle)
        return;
    s->running = true;
    BaseType_t ret = xTaskCreatePinnedToCore(refresh_task_fn, "epd_refresh", s->config.task_stack_size, s,
                                             s->config.task_priority, &s->task_handle, s->config.cpu_core);
    if (ret != pdPASS) {
        s->running = false;
        s->task_handle = NULL;
    }
}

void epd_refresh_stop(epd_refresh_scheduler_t *s)
{
    if (!s || !s->running)
        return;
    s->running = false;
    if (s->event_group) {
        xEventGroupSetBits(s->event_group, EPD_REFRESH_STOP_BIT);
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    if (s->task_handle) {
        vTaskDelete(s->task_handle);
        s->task_handle = NULL;
    }
}

void epd_refresh_mark_dirty(epd_refresh_scheduler_t *s, rawdraw_rect_t rect)
{
    if (!s || rect.w <= 0 || rect.h <= 0)
        return;
    if (s->mutex)
        xSemaphoreTake(s->mutex, portMAX_DELAY);
    if (s->has_dirty) {
        s->dirty_rect = display_rect_union(s->dirty_rect, rect);
    } else {
        s->dirty_rect = rect;
        s->has_dirty = true;
    }
    if (s->mutex)
        xSemaphoreGive(s->mutex);
}

void epd_refresh_trigger(epd_refresh_scheduler_t *s, bool urgent)
{
    if (!s || !s->event_group)
        return;
    xEventGroupSetBits(s->event_group, EPD_REFRESH_REQUEST_BIT);
    if (urgent) {
        epd_refresh_process(s);
    }
}

void epd_refresh_request_full(epd_refresh_scheduler_t *s)
{
    if (!s)
        return;
    s->full_refresh_pending = true;
    s->partial_count = 0;
    epd_refresh_trigger(s, true);
}

int epd_refresh_get_partial_count(epd_refresh_scheduler_t *s)
{
    if (!s)
        return 0;
    return s->partial_count;
}

void epd_refresh_reset_partial_count(epd_refresh_scheduler_t *s)
{
    if (s)
        s->partial_count = 0;
}

bool epd_refresh_is_running(epd_refresh_scheduler_t *s)
{
    return s ? s->task_handle != NULL : false;
}

void epd_refresh_process(epd_refresh_scheduler_t *s)
{
    if (!s || !s->callback)
        return;

    rawdraw_rect_t rect_to_refresh = {0, 0, 0, 0};
    bool do_full = false;

    if (s->mutex)
        xSemaphoreTake(s->mutex, portMAX_DELAY);

    if (s->full_refresh_pending) {
        do_full = true;
        s->full_refresh_pending = false;
        rect_to_refresh = (rawdraw_rect_t){0, 0, s->screen_width, s->screen_height};
        s->partial_count = 0;
    } else if (s->has_dirty) {
        rect_to_refresh = display_align_x8(s->dirty_rect);
        s->has_dirty = false;
        s->dirty_rect = (rawdraw_rect_t){0, 0, 0, 0};
        s->partial_count++;

        if (s->partial_count >= s->config.partial_count_threshold) {
            do_full = true;
            rect_to_refresh = (rawdraw_rect_t){0, 0, s->screen_width, s->screen_height};
            s->partial_count = 0;
        }
    }

    if (s->mutex)
        xSemaphoreGive(s->mutex);

    if (rect_to_refresh.w > 0 && rect_to_refresh.h > 0) {
        epd_refresh_mode_t mode = do_full ? EPD_REFRESH_FULL : EPD_REFRESH_PARTIAL;
        s->callback(rect_to_refresh, mode, s->user_data);
    }
}
