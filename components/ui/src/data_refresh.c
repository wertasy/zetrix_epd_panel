/**
 * @file data_refresh.c
 * @brief Global data-refresh callback dispatch.
 */
#include "data_refresh.h"

static data_refresh_cb_t s_cb  = 0;
static void             *s_ctx = 0;

void data_refresh_set_callback(data_refresh_cb_t cb, void *ctx)
{
    s_cb  = cb;
    s_ctx = ctx;
}

void data_refresh_request(data_refresh_page_t page)
{
    if (s_cb)
        s_cb(page, s_ctx);
}
