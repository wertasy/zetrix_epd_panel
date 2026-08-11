#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "epd_refresh.h"

/* Dummy font declarations */
#include "font_engine.h"
const lv_font_t SourceHanSansSC_Regular_slim;
const lv_font_t SourceHanSansSC_Medium_slim;
const lv_font_t font_zectrix_16_1;
const lv_font_t font_zectrix_48_1;
const lv_font_t weather_icons_16;
const lv_font_t weather_icons_48;

#define SCREEN_W 400
#define SCREEN_H 300

typedef struct {
    int                call_count;
    rawdraw_rect_t     last_rect;
    epd_refresh_mode_t last_mode;
} test_ctx_t;

static void test_cb(rawdraw_rect_t rect, epd_refresh_mode_t mode, void *user_data)
{
    test_ctx_t *ctx = (test_ctx_t *)user_data;
    ctx->call_count++;
    ctx->last_rect = rect;
    ctx->last_mode = mode;
}

static bool rect_eq(rawdraw_rect_t a, rawdraw_rect_t b)
{
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

/* Test: dirty rect accumulation and merging */
static void test_dirty_merging(void)
{
    printf("Testing dirty rect merging...\n");
    test_ctx_t              ctx = {0};
    epd_refresh_scheduler_t s;
    epd_refresh_init(&s, test_cb, &ctx, NULL, SCREEN_W, SCREEN_H);

    /* Mark two overlapping dirty regions */
    epd_refresh_mark_dirty(&s, (rawdraw_rect_t){10, 20, 100, 50});
    epd_refresh_mark_dirty(&s, (rawdraw_rect_t){50, 40, 100, 50});

    /* Process should merge them into a union rect */
    epd_refresh_process(&s);

    assert(ctx.call_count == 1);
    /* Union: x=10, y=20, w=140 (50+100-10), h=70 (40+50-20) */
    assert(ctx.last_rect.x == 8); /* align_x8 rounds 10 down to 8 */
    assert(ctx.last_rect.y == 20);
    assert(ctx.last_rect.w >= 140);
    assert(ctx.last_rect.h == 70);
    assert(ctx.last_mode == EPD_REFRESH_PARTIAL);

    printf("Dirty rect merging passed!\n");
}

/* Test: partial count increment */
static void test_partial_count(void)
{
    printf("Testing partial count increment...\n");
    test_ctx_t              ctx = {0};
    epd_refresh_scheduler_t s;
    epd_refresh_init(&s, test_cb, &ctx, NULL, SCREEN_W, SCREEN_H);

    for (int i = 1; i <= 5; i++) {
        epd_refresh_mark_dirty(&s, (rawdraw_rect_t){0, 0, 50, 50});
        epd_refresh_process(&s);
        assert(epd_refresh_get_partial_count(&s) == i % 5 || (i == 5 && epd_refresh_get_partial_count(&s) == 5));
    }

    printf("Partial count increment passed!\n");
}

/* Test: automatic full refresh at threshold */
static void test_threshold_full_refresh(void)
{
    printf("Testing threshold auto-escalation to full refresh...\n");
    test_ctx_t              ctx = {0};
    epd_refresh_scheduler_t s;
    epd_refresh_init(&s, test_cb, &ctx, NULL, SCREEN_W, SCREEN_H);
    assert(s.config.partial_count_threshold == 10);

    /* Do 9 partial refreshes */
    for (int i = 0; i < 9; i++) {
        epd_refresh_mark_dirty(&s, (rawdraw_rect_t){0, 0, 50, 50});
        epd_refresh_process(&s);
        assert(ctx.last_mode == EPD_REFRESH_PARTIAL);
    }
    assert(epd_refresh_get_partial_count(&s) == 9);

    /* 10th dirty should escalate to full */
    epd_refresh_mark_dirty(&s, (rawdraw_rect_t){0, 0, 50, 50});
    epd_refresh_process(&s);
    assert(ctx.last_mode == EPD_REFRESH_FULL);
    /* Full refresh should cover entire screen */
    assert(ctx.last_rect.x == 0);
    assert(ctx.last_rect.y == 0);
    assert(ctx.last_rect.w == SCREEN_W);
    assert(ctx.last_rect.h == SCREEN_H);
    /* Counter should reset after full */
    assert(epd_refresh_get_partial_count(&s) == 0);

    printf("Threshold auto-escalation passed!\n");
}

/* Test: request_full_refresh resets partial count and triggers FULL */
static void test_request_full_refresh(void)
{
    printf("Testing request_full_refresh...\n");
    test_ctx_t              ctx = {0};
    epd_refresh_scheduler_t s;
    epd_refresh_init(&s, test_cb, &ctx, NULL, SCREEN_W, SCREEN_H);

    /* Build up some partial count */
    for (int i = 0; i < 5; i++) {
        epd_refresh_mark_dirty(&s, (rawdraw_rect_t){0, 0, 50, 50});
        epd_refresh_process(&s);
    }
    assert(epd_refresh_get_partial_count(&s) == 5);

    /* Request full refresh */
    epd_refresh_request_full(&s);

    assert(ctx.last_mode == EPD_REFRESH_FULL);
    assert(ctx.last_rect.w == SCREEN_W);
    assert(ctx.last_rect.h == SCREEN_H);
    assert(epd_refresh_get_partial_count(&s) == 0);

    printf("request_full_refresh passed!\n");
}

/* Test: no callback fires when no dirty region */
static void test_no_dirty_no_call(void)
{
    printf("Testing no-dirty no-call...\n");
    test_ctx_t              ctx = {0};
    epd_refresh_scheduler_t s;
    epd_refresh_init(&s, test_cb, &ctx, NULL, SCREEN_W, SCREEN_H);

    /* Process without any dirty should not call callback */
    epd_refresh_process(&s);
    assert(ctx.call_count == 0);

    printf("No-dirty no-call passed!\n");
}

/* Test: invalid dirty rects are ignored */
static void test_invalid_dirty(void)
{
    printf("Testing invalid dirty rects...\n");
    test_ctx_t              ctx = {0};
    epd_refresh_scheduler_t s;
    epd_refresh_init(&s, test_cb, &ctx, NULL, SCREEN_W, SCREEN_H);

    epd_refresh_mark_dirty(&s, (rawdraw_rect_t){0, 0, 0, 0});
    epd_refresh_mark_dirty(&s, (rawdraw_rect_t){0, 0, -1, 50});
    epd_refresh_mark_dirty(&s, (rawdraw_rect_t){0, 0, 50, -1});

    epd_refresh_process(&s);
    assert(ctx.call_count == 0);

    printf("Invalid dirty rects passed!\n");
}

int main(void)
{
    printf("Starting EPD refresh scheduler tests...\n");

    test_dirty_merging();
    test_partial_count();
    test_threshold_full_refresh();
    test_request_full_refresh();
    test_no_dirty_no_call();
    test_invalid_dirty();

    printf("All EPD refresh tests completed successfully!\n");
    return 0;
}
