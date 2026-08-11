#include "rawdraw_ext.h"
#include "framebuffer.h"
#include <string.h>
#include <stdlib.h>

#ifdef ESP_PLATFORM
#    include <esp_timer.h>
#    include <esp_heap_caps.h>
#else
#    include <sys/time.h>

struct host_timer {
    pthread_t       thread;
    pthread_mutex_t lock;
    pthread_cond_t  cond;
    void (*callback)(void *arg);
    void    *arg;
    bool     active;
    bool     quit;
    uint64_t target_time_ms;
};

static inline uint64_t host_get_time_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static void *host_timer_worker(void *arg)
{
    struct host_timer *t = (struct host_timer *)arg;
    pthread_mutex_lock(&t->lock);
    while (!t->quit) {
        if (!t->active) {
            pthread_cond_wait(&t->cond, &t->lock);
        } else {
            uint64_t now = host_get_time_ms();
            if (now >= t->target_time_ms) {
                t->active = false;
                pthread_mutex_unlock(&t->lock);
                t->callback(t->arg);
                pthread_mutex_lock(&t->lock);
            } else {
                uint64_t       delay = t->target_time_ms - now;
                struct timeval now_tv;
                gettimeofday(&now_tv, NULL);
                struct timespec ts;
                ts.tv_sec  = now_tv.tv_sec + (delay / 1000);
                ts.tv_nsec = (now_tv.tv_usec + (delay % 1000) * 1000) * 1000;
                if (ts.tv_nsec >= 1000000000) {
                    ts.tv_sec += ts.tv_nsec / 1000000000;
                    ts.tv_nsec %= 1000000000;
                }
                pthread_cond_timedwait(&t->cond, &t->lock, &ts);
            }
        }
    }
    pthread_mutex_unlock(&t->lock);
    return NULL;
}

static inline struct host_timer *host_timer_create(void (*callback)(void *arg), void *arg, const char *name)
{
    struct host_timer *t = (struct host_timer *)malloc(sizeof(struct host_timer));
    if (!t)
        return NULL;
    t->callback       = callback;
    t->arg            = arg;
    t->active         = false;
    t->quit           = false;
    t->target_time_ms = 0;
    pthread_mutex_init(&t->lock, NULL);
    pthread_cond_init(&t->cond, NULL);
    if (pthread_create(&t->thread, NULL, host_timer_worker, t) != 0) {
        pthread_mutex_destroy(&t->lock);
        pthread_cond_destroy(&t->cond);
        free(t);
        return NULL;
    }
    return t;
}

static inline void host_timer_stop(struct host_timer *t)
{
    if (!t)
        return;
    pthread_mutex_lock(&t->lock);
    t->active = false;
    pthread_cond_broadcast(&t->cond);
    pthread_mutex_unlock(&t->lock);
}

static inline void host_timer_start_once(struct host_timer *t, uint64_t timeout_us)
{
    if (!t)
        return;
    pthread_mutex_lock(&t->lock);
    t->target_time_ms = host_get_time_ms() + (timeout_us / 1000);
    t->active         = true;
    pthread_cond_broadcast(&t->cond);
    pthread_mutex_unlock(&t->lock);
}

static inline void host_timer_delete(struct host_timer *t)
{
    if (!t)
        return;
    pthread_mutex_lock(&t->lock);
    t->quit   = true;
    t->active = false;
    pthread_cond_broadcast(&t->cond);
    pthread_mutex_unlock(&t->lock);
    pthread_join(t->thread, NULL);
    pthread_mutex_destroy(&t->lock);
    pthread_cond_destroy(&t->cond);
    free(t);
}
#endif

// ============================================================
// Time Helpers
// ============================================================

static inline uint32_t framebuffer_get_time_ms(void)
{
#ifdef ESP_PLATFORM
    return (uint32_t)(esp_timer_get_time() / 1000);
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
#endif
}

// ============================================================
// Rect Math Helpers
// ============================================================

static void framebuffer_invalidate_rect_no_lock(framebuffer_t *fb, rawdraw_rect_t r)
{
    if (rawdraw_rect_area(r) <= 0)
        return;
    rawdraw_rect_t aligned = rawdraw_align_x8(rawdraw_clamp_rect(r, fb->width, fb->height));
    if (rawdraw_rect_area(aligned) > 0) {
        fb->dirty   = rawdraw_rect_union(fb->dirty, aligned);
        fb->pending = true;
    }
}

// ============================================================
// Timer Callback
// ============================================================

static void framebuffer_timer_callback(void *arg)
{
    framebuffer_t *fb = (framebuffer_t *)arg;
    if (!fb)
        return;

    framebuffer_lock(fb);
    int area = rawdraw_rect_area(fb->dirty);
    if (fb->pending && area > 0) {
        rawdraw_rect_t r    = fb->dirty;
        fb->dirty           = (rawdraw_rect_t){0, 0, 0, 0};
        fb->pending         = false;
        fb->last_refresh_ms = framebuffer_get_time_ms();

        framebuffer_refresh_cb_t cb        = fb->refresh_cb;
        void                    *user_data = fb->refresh_user_data;
        framebuffer_unlock(fb);

        if (cb) {
            cb(&r, false, user_data);
        }
    } else {
        framebuffer_unlock(fb);
    }
}

// ============================================================
// Public APIs
// ============================================================

void framebuffer_init(framebuffer_t *fb, uint8_t *buffer, int width, int height, SemaphoreHandle_t mutex)
{
    if (!fb)
        return;
    fb->timer             = NULL;
    fb->buffer            = buffer;
    fb->width             = width;
    fb->height            = height;
    fb->mutex             = mutex;
    fb->dirty             = (rawdraw_rect_t){0, 0, 0, 0};
    fb->pending           = false;
    fb->refresh_cb        = NULL;
    fb->refresh_user_data = NULL;
    fb->last_refresh_ms   = 0;
    fb->next_kick_ms      = 0;

#ifdef ESP_PLATFORM
    esp_timer_create_args_t timer_args = {.callback = framebuffer_timer_callback, .arg = fb, .name = "fb_refresh"};
    if (esp_timer_create(&timer_args, &fb->timer) != ESP_OK) {
        fb->timer = NULL;
    }
#else
    fb->timer = host_timer_create(framebuffer_timer_callback, fb, "fb_refresh");
#endif
}

void framebuffer_deinit(framebuffer_t *fb)
{
    if (!fb)
        return;

#ifdef ESP_PLATFORM
    if (fb->timer) {
        esp_timer_stop(fb->timer);
        esp_timer_delete(fb->timer);
        fb->timer = NULL;
    }
#else
    if (fb->timer) {
        host_timer_delete(fb->timer);
        fb->timer = NULL;
    }
#endif
    fb->buffer = NULL;
}

void framebuffer_clear(framebuffer_t *fb, rawdraw_color_t color)
{
    if (!fb)
        return;
    framebuffer_lock(fb);
    rawdraw_clear(fb->buffer, fb->width, fb->height, color);
    framebuffer_invalidate_rect_no_lock(fb, (rawdraw_rect_t){0, 0, fb->width, fb->height});
    framebuffer_unlock(fb);
}

void framebuffer_invalidate_rect(framebuffer_t *fb, int x, int y, int w, int h)
{
    if (!fb)
        return;
    rawdraw_rect_t r = {x, y, w, h};
    if (rawdraw_rect_area(r) <= 0)
        return;
    framebuffer_lock(fb);
    framebuffer_invalidate_rect_no_lock(fb, r);
    framebuffer_unlock(fb);
}

void framebuffer_invalidate_all(framebuffer_t *fb)
{
    if (!fb)
        return;
    framebuffer_lock(fb);
    framebuffer_invalidate_rect_no_lock(fb, (rawdraw_rect_t){0, 0, fb->width, fb->height});
    framebuffer_unlock(fb);
}

rawdraw_rect_t framebuffer_get_dirty(framebuffer_t *fb)
{
    if (!fb)
        return (rawdraw_rect_t){0, 0, 0, 0};
    framebuffer_lock(fb);
    rawdraw_rect_t r = fb->dirty;
    framebuffer_unlock(fb);
    return r;
}

bool framebuffer_has_dirty(framebuffer_t *fb)
{
    if (!fb)
        return false;
    framebuffer_lock(fb);
    bool has = fb->pending && (rawdraw_rect_area(fb->dirty) > 0);
    framebuffer_unlock(fb);
    return has;
}

void framebuffer_clear_dirty(framebuffer_t *fb)
{
    if (!fb)
        return;
    framebuffer_lock(fb);
    fb->dirty   = (rawdraw_rect_t){0, 0, 0, 0};
    fb->pending = false;
    framebuffer_unlock(fb);
}

void framebuffer_request_refresh(framebuffer_t *fb, bool urgent)
{
    if (!fb)
        return;

    framebuffer_lock(fb);
    int area = rawdraw_rect_area(fb->dirty);
    if (!fb->pending || area <= 0) {
        framebuffer_unlock(fb);
        return;
    }

    if (urgent) {
#ifdef ESP_PLATFORM
        esp_timer_stop(fb->timer);
#else
        host_timer_stop(fb->timer);
#endif
        rawdraw_rect_t r    = fb->dirty;
        fb->dirty           = (rawdraw_rect_t){0, 0, 0, 0};
        fb->pending         = false;
        fb->last_refresh_ms = framebuffer_get_time_ms();

        framebuffer_refresh_cb_t cb        = fb->refresh_cb;
        void                    *user_data = fb->refresh_user_data;
        framebuffer_unlock(fb);

        if (cb) {
            cb(&r, true, user_data);
        }
    } else {
        uint32_t delay_ms = (fb->next_kick_ms > 0) ? fb->next_kick_ms : 300;
#ifdef ESP_PLATFORM
        esp_timer_stop(fb->timer);
        esp_timer_start_once(fb->timer, (uint64_t)delay_ms * 1000);
#else
        host_timer_stop(fb->timer);
        host_timer_start_once(fb->timer, (uint64_t)delay_ms * 1000);
#endif
        framebuffer_unlock(fb);
    }
}

void framebuffer_lock(framebuffer_t *fb)
{
    if (fb && fb->mutex) {
        xSemaphoreTake(fb->mutex, portMAX_DELAY);
    }
}

void framebuffer_unlock(framebuffer_t *fb)
{
    if (fb && fb->mutex) {
        xSemaphoreGive(fb->mutex);
    }
}

void framebuffer_set_refresh_callback(framebuffer_t *fb, framebuffer_refresh_cb_t cb, void *user_data)
{
    if (!fb)
        return;
    framebuffer_lock(fb);
    fb->refresh_cb        = cb;
    fb->refresh_user_data = user_data;
    framebuffer_unlock(fb);
}

uint8_t *framebuffer_alloc_buffer(int width, int height)
{
    size_t bytes_per_row = (size_t)(((width + 7) / 8) * 2);
    size_t size          = bytes_per_row * height;
#ifdef ESP_PLATFORM
    uint8_t *buf = (uint8_t *)heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        buf = (uint8_t *)malloc(size);
    }
    return buf;
#else
    return (uint8_t *)malloc(size);
#endif
}

void framebuffer_free_buffer(uint8_t *buffer)
{
    free(buffer);
}

// ============================================================
// Convenience Drawing Helpers
// ============================================================

void framebuffer_draw_rect(framebuffer_t *fb, rawdraw_rect_t r, rawdraw_color_t color)
{
    if (!fb || !fb->buffer)
        return;
    framebuffer_lock(fb);
    rawdraw_fill_rect(fb->buffer, fb->width, fb->height, r, color);
    framebuffer_invalidate_rect_no_lock(fb, r);
    framebuffer_unlock(fb);
}

void framebuffer_draw_text(framebuffer_t *fb, int x, int y, const char *text, const lv_font_t *font,
                           rawdraw_color_t color)
{
    if (!fb || !fb->buffer || !text || !font)
        return;
    framebuffer_lock(fb);
    rawdraw_rect_t bounds = rawdraw_measure_text_bounds(text, font, 0);
    bounds.x              = x;
    bounds.y              = y;
    rawdraw_draw_text(fb->buffer, fb->width, fb->height, x, y, text, font, (int)color);
    framebuffer_invalidate_rect_no_lock(fb, bounds);
    framebuffer_unlock(fb);
}

void framebuffer_draw_round_rect(framebuffer_t *fb, rawdraw_rect_t r, int radius, rawdraw_color_t fill,
                                 rawdraw_color_t border, int border_w)
{
    if (!fb || !fb->buffer)
        return;
    framebuffer_lock(fb);
    rawdraw_draw_round_rect(fb->buffer, fb->width, fb->height, r.x, r.y, r.w, r.h, radius, (int)fill, (int)border,
                            border_w);
    framebuffer_invalidate_rect_no_lock(fb, r);
    framebuffer_unlock(fb);
}
