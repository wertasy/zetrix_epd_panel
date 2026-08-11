#ifndef COMPONENTS_RAWDRAW_INCLUDE_FRAMEBUFFER_H_
#define COMPONENTS_RAWDRAW_INCLUDE_FRAMEBUFFER_H_

#include <stdint.h>
#include <stdbool.h>
#include "rawdraw.h"
#include "rawdraw_ext.h"
#include "font_engine.h"

#ifdef ESP_PLATFORM
#    include <freertos/FreeRTOS.h>
#    include <freertos/semphr.h>
#    include <esp_timer.h>
#else
// Host shims
#    include <pthread.h>
#    include <stdlib.h>
typedef pthread_mutex_t   *SemaphoreHandle_t;
typedef struct host_timer *esp_timer_handle_t;

#    define portMAX_DELAY 0xFFFFFFFF

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

static inline int xSemaphoreTake(SemaphoreHandle_t mutex, uint32_t delay)
{
    if (mutex) {
        return pthread_mutex_lock(mutex) == 0;
    }
    return 0;
}

static inline int xSemaphoreGive(SemaphoreHandle_t mutex)
{
    if (mutex) {
        return pthread_mutex_unlock(mutex) == 0;
    }
    return 0;
}
#endif

typedef void (*framebuffer_refresh_cb_t)(const rawdraw_rect_t *dirty_rect, bool urgent, void *user_data);

typedef struct {
    uint8_t                 *buffer;
    int                      width;
    int                      height;
    SemaphoreHandle_t        mutex;
    rawdraw_rect_t           dirty;
    bool                     pending;
    framebuffer_refresh_cb_t refresh_cb;
    void                    *refresh_user_data;
    uint32_t                 last_refresh_ms;
    uint32_t                 next_kick_ms;
    esp_timer_handle_t       timer;
} framebuffer_t;

void           framebuffer_init(framebuffer_t *fb, uint8_t *buffer, int width, int height, SemaphoreHandle_t mutex);
void           framebuffer_deinit(framebuffer_t *fb);
void           framebuffer_clear(framebuffer_t *fb, rawdraw_color_t color);
void           framebuffer_invalidate_rect(framebuffer_t *fb, int x, int y, int w, int h);
void           framebuffer_invalidate_all(framebuffer_t *fb);
rawdraw_rect_t framebuffer_get_dirty(framebuffer_t *fb);
bool           framebuffer_has_dirty(framebuffer_t *fb);
void           framebuffer_clear_dirty(framebuffer_t *fb);
void           framebuffer_request_refresh(framebuffer_t *fb, bool urgent);
void           framebuffer_lock(framebuffer_t *fb);
void           framebuffer_unlock(framebuffer_t *fb);

// Setter helper for refresh callback
void framebuffer_set_refresh_callback(framebuffer_t *fb, framebuffer_refresh_cb_t cb, void *user_data);

// Buffer allocation/free helpers
uint8_t *framebuffer_alloc_buffer(int width, int height);
void     framebuffer_free_buffer(uint8_t *buffer);

// Convenience drawing helpers
void framebuffer_draw_rect(framebuffer_t *fb, rawdraw_rect_t r, rawdraw_color_t color);
void framebuffer_draw_text(framebuffer_t *fb, int x, int y, const char *text, const lv_font_t *font,
                           rawdraw_color_t color);
void framebuffer_draw_round_rect(framebuffer_t *fb, rawdraw_rect_t r, int radius, rawdraw_color_t fill,
                                 rawdraw_color_t border, int border_w);

#endif // COMPONENTS_RAWDRAW_INCLUDE_FRAMEBUFFER_H_
