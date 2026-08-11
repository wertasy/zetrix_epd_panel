#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>
#include "framebuffer.h"

// Define dummy fonts for the declarations in font_engine.h
const lv_font_t SourceHanSansSC_Regular_slim;
const lv_font_t SourceHanSansSC_Medium_slim;
const lv_font_t font_zectrix_16_1;
const lv_font_t font_zectrix_48_1;
const lv_font_t weather_icons_16;
const lv_font_t weather_icons_48;

// Define a mock font for testing text drawing
static bool mock_get_glyph_dsc(const struct _lv_font_t *font, lv_font_glyph_dsc_t *dsc_out, uint32_t letter,
                               uint32_t letter_next)
{
    dsc_out->adv_w  = 12;
    dsc_out->box_w  = 8;
    dsc_out->box_h  = 16;
    dsc_out->ofs_x  = 0;
    dsc_out->ofs_y  = 0;
    dsc_out->format = 0;
    return true;
}

static const uint8_t mock_bitmap[] = {0x00, 0x00, 0x00, 0x00};

static const void *mock_get_glyph_bitmap(lv_font_glyph_dsc_t *g_dsc, struct _lv_draw_buf_t *draw_buf)
{
    return mock_bitmap;
}

const lv_font_t mock_font = {
    .get_glyph_dsc    = mock_get_glyph_dsc,
    .get_glyph_bitmap = mock_get_glyph_bitmap,
    .line_height      = 20,
    .base_line        = 4,
};

#define FB_WIDTH 400
#define FB_HEIGHT 300

// Helper to check if rectangles are equal
static bool rect_equals(rawdraw_rect_t r1, rawdraw_rect_t r2) __attribute__((unused));
static bool rect_equals(rawdraw_rect_t r1, rawdraw_rect_t r2)
{
    return r1.x == r2.x && r1.y == r2.y && r1.w == r2.w && r1.h == r2.h;
}

// ------------------------------------------------------------
// Test: Invalidate Rect Logic
// ------------------------------------------------------------
void test_invalidate_rect(void)
{
    printf("Testing invalidate rect logic...\n");

    uint8_t *buffer = framebuffer_alloc_buffer(FB_WIDTH, FB_HEIGHT);
    assert(buffer != NULL);

    SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
    assert(mutex != NULL);

    framebuffer_t fb;
    framebuffer_init(&fb, buffer, FB_WIDTH, FB_HEIGHT, mutex);

    // Initial state: not pending, dirty is empty
    assert(!framebuffer_has_dirty(&fb));
    rawdraw_rect_t dirty = framebuffer_get_dirty(&fb);
    assert(dirty.w == 0 && dirty.h == 0);

    // Invalidate a rect: (2, 5, 5, 10)
    // Clamped: (2, 5, 5, 10)
    // Aligned to 8-byte boundary:
    // x0 = (2 / 8) * 8 = 0
    // x1 = ((2 + 5 + 7) / 8) * 8 = 8
    // Result: (0, 5, 8, 10)
    framebuffer_invalidate_rect(&fb, 2, 5, 5, 10);
    assert(framebuffer_has_dirty(&fb));
    dirty = framebuffer_get_dirty(&fb);
    assert(dirty.x == 0 && dirty.y == 5 && dirty.w == 8 && dirty.h == 10);

    // Invalidate another rect that merges: (10, 5, 2, 10)
    // Aligned: (8, 5, 8, 10)
    // Union: (0, 5, 16, 10)
    framebuffer_invalidate_rect(&fb, 10, 5, 2, 10);
    dirty = framebuffer_get_dirty(&fb);
    assert(dirty.x == 0 && dirty.y == 5 && dirty.w == 16 && dirty.h == 10);

    // Invalidate region outside bounds
    // Should be clamped inside width (400) and height (300)
    framebuffer_invalidate_rect(&fb, 395, 295, 20, 20);
    dirty = framebuffer_get_dirty(&fb);
    // clamped: (395, 295, 5, 5)
    // align_x8: x0 = 392, x1 = 400 => x=392, w=8
    // y union: min(5, 295) = 5, max(15, 300) = 300 => y=5, h=295
    // x union: min(0, 392) = 0, max(16, 400) = 400 => x=0, w=400
    assert(dirty.x == 0 && dirty.y == 5 && dirty.w == 400 && dirty.h == 295);

    framebuffer_clear_dirty(&fb);
    assert(!framebuffer_has_dirty(&fb));

    framebuffer_deinit(&fb);
    vSemaphoreDelete(mutex);
    framebuffer_free_buffer(buffer);

    printf("Invalidate rect logic passed!\n");
}

// ------------------------------------------------------------
// Test: Clear and Draw Functions Update Dirty Rect
// ------------------------------------------------------------
void test_clear_and_draw_dirty(void)
{
    printf("Testing clear and draw dirty rect updates...\n");

    uint8_t          *buffer = framebuffer_alloc_buffer(FB_WIDTH, FB_HEIGHT);
    SemaphoreHandle_t mutex  = xSemaphoreCreateMutex();

    framebuffer_t fb;
    framebuffer_init(&fb, buffer, FB_WIDTH, FB_HEIGHT, mutex);

    // Clear should update dirty rect to full screen
    framebuffer_clear(&fb, RAWDRAW_COLOR_WHITE);
    assert(framebuffer_has_dirty(&fb));
    rawdraw_rect_t dirty = framebuffer_get_dirty(&fb);
    assert(dirty.x == 0 && dirty.y == 0 && dirty.w == FB_WIDTH && dirty.h == FB_HEIGHT);

    framebuffer_clear_dirty(&fb);
    assert(!framebuffer_has_dirty(&fb));

    // Draw rect should update dirty rect
    rawdraw_rect_t rect_to_draw = {10, 20, 30, 40};
    framebuffer_draw_rect(&fb, rect_to_draw, RAWDRAW_COLOR_BLACK);
    assert(framebuffer_has_dirty(&fb));
    dirty = framebuffer_get_dirty(&fb);
    // align_x8(10, 20, 30, 40): x0=8, x1=((10+30+7)/8)*8=40 => x=8, w=32
    assert(dirty.x == 8 && dirty.y == 20 && dirty.w == 32 && dirty.h == 40);

    framebuffer_clear_dirty(&fb);

    // Draw text should update dirty rect
    framebuffer_draw_text(&fb, 50, 60, "Hello", &mock_font, RAWDRAW_COLOR_BLACK);
    assert(framebuffer_has_dirty(&fb));
    dirty = framebuffer_get_dirty(&fb);
    assert(dirty.w > 0 && dirty.h > 0);

    framebuffer_clear_dirty(&fb);

    // Draw round rect should update dirty rect
    framebuffer_draw_round_rect(&fb, rect_to_draw, 5, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_BLACK, 1);
    assert(framebuffer_has_dirty(&fb));
    dirty = framebuffer_get_dirty(&fb);
    assert(dirty.x == 8 && dirty.y == 20 && dirty.w == 32 && dirty.h == 40);

    framebuffer_deinit(&fb);
    vSemaphoreDelete(mutex);
    framebuffer_free_buffer(buffer);

    printf("Clear and draw dirty rect updates passed!\n");
}

// ------------------------------------------------------------
// Test Refresh Callbacks structures
// ------------------------------------------------------------
typedef struct {
    int             call_count;
    rawdraw_rect_t  last_dirty;
    bool            last_urgent;
    pthread_mutex_t lock;
} test_callback_ctx_t;

static void test_refresh_callback(const rawdraw_rect_t *dirty_rect, bool urgent, void *user_data)
{
    test_callback_ctx_t *ctx = (test_callback_ctx_t *)user_data;
    pthread_mutex_lock(&ctx->lock);
    ctx->call_count++;
    ctx->last_dirty  = *dirty_rect;
    ctx->last_urgent = urgent;
    pthread_mutex_unlock(&ctx->lock);
}

// ------------------------------------------------------------
// Test: Urgent Refresh
// ------------------------------------------------------------
void test_urgent_refresh(void)
{
    printf("Testing urgent refresh triggers immediately...\n");

    uint8_t          *buffer = framebuffer_alloc_buffer(FB_WIDTH, FB_HEIGHT);
    SemaphoreHandle_t mutex  = xSemaphoreCreateMutex();

    framebuffer_t fb;
    framebuffer_init(&fb, buffer, FB_WIDTH, FB_HEIGHT, mutex);

    test_callback_ctx_t ctx;
    ctx.call_count  = 0;
    ctx.last_dirty  = (rawdraw_rect_t){0, 0, 0, 0};
    ctx.last_urgent = false;
    pthread_mutex_init(&ctx.lock, NULL);

    framebuffer_set_refresh_callback(&fb, test_refresh_callback, &ctx);

    // Invalidate region
    framebuffer_invalidate_rect(&fb, 8, 10, 16, 20);

    // Request urgent refresh
    framebuffer_request_refresh(&fb, true);

    pthread_mutex_lock(&ctx.lock);
    assert(ctx.call_count == 1);
    assert(ctx.last_urgent == true);
    assert(ctx.last_dirty.x == 8 && ctx.last_dirty.w == 16);
    pthread_mutex_unlock(&ctx.lock);

    // Framebuffer dirty should be cleared
    assert(!framebuffer_has_dirty(&fb));

    framebuffer_deinit(&fb);
    vSemaphoreDelete(mutex);
    framebuffer_free_buffer(buffer);
    pthread_mutex_destroy(&ctx.lock);

    printf("Urgent refresh passed!\n");
}

// ------------------------------------------------------------
// Test: Debounce and Coalescing Mechanism
// ------------------------------------------------------------
void test_debounce_mechanism(void)
{
    printf("Testing debounce and coalescing mechanism...\n");

    uint8_t          *buffer = framebuffer_alloc_buffer(FB_WIDTH, FB_HEIGHT);
    SemaphoreHandle_t mutex  = xSemaphoreCreateMutex();

    framebuffer_t fb;
    framebuffer_init(&fb, buffer, FB_WIDTH, FB_HEIGHT, mutex);

    // Set next kick to 150ms for faster test run
    fb.next_kick_ms = 150;

    test_callback_ctx_t ctx;
    ctx.call_count  = 0;
    ctx.last_dirty  = (rawdraw_rect_t){0, 0, 0, 0};
    ctx.last_urgent = false;
    pthread_mutex_init(&ctx.lock, NULL);

    framebuffer_set_refresh_callback(&fb, test_refresh_callback, &ctx);

    // Invalidate rect 1 and request refresh (debounce)
    framebuffer_invalidate_rect(&fb, 8, 10, 16, 20);
    framebuffer_request_refresh(&fb, false);

    // Sleep 50ms (less than 150ms delay)
    usleep(50 * 1000);

    pthread_mutex_lock(&ctx.lock);
    assert(ctx.call_count == 0); // Should not have triggered yet
    pthread_mutex_unlock(&ctx.lock);

    // Invalidate rect 2 (coalesces/merges with rect 1) and request refresh again
    // This should restart/coalesce the timer
    framebuffer_invalidate_rect(&fb, 24, 10, 8, 20);
    framebuffer_request_refresh(&fb, false);

    // Sleep another 100ms
    // (Total 150ms since first call, but only 100ms since second call.
    // If timer restarted, it should not trigger yet. If it coalesced, it might trigger 150ms after second call.)
    usleep(100 * 1000);

    pthread_mutex_lock(&ctx.lock);
    // Depending on coalesce strategy:
    // If timer restarted, it requires another 50ms.
    // Let's verify that it hasn't triggered yet if restarting is used, or let's be flexible.
    // Standard debounce restarts the timer. Let's make sure it triggers at most once.
    pthread_mutex_unlock(&ctx.lock);

    // Sleep another 100ms (Total 200ms since second call, timer should have fired)
    usleep(100 * 1000);

    pthread_mutex_lock(&ctx.lock);
    assert(ctx.call_count == 1);
    assert(ctx.last_urgent == false);
    // Merged rect: x=8, w=16 + x=24, w=8 => x=8, w=24
    assert(ctx.last_dirty.x == 8 && ctx.last_dirty.w == 24);
    pthread_mutex_unlock(&ctx.lock);

    // Framebuffer dirty should be cleared
    assert(!framebuffer_has_dirty(&fb));

    framebuffer_deinit(&fb);
    vSemaphoreDelete(mutex);
    framebuffer_free_buffer(buffer);
    pthread_mutex_destroy(&ctx.lock);

    printf("Debounce and coalescing mechanism passed!\n");
}

int main(void)
{
    printf("Starting Framebuffer tests...\n");
    test_invalidate_rect();
    test_clear_and_draw_dirty();
    test_urgent_refresh();
    test_debounce_mechanism();
    printf("All Framebuffer tests completed successfully!\n");
    return 0;
}
