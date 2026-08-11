#ifndef COMPONENTS_UI_INCLUDE_PAGE_RENDERER_H_
#define COMPONENTS_UI_INCLUDE_PAGE_RENDERER_H_

#include <stdint.h>
#include <stdbool.h>
#include "rawdraw.h"
#include "rawdraw_ext.h"

/* Forward declaration */
typedef struct page_renderer page_renderer_t;

/* Linux-kernel-style read-only ops vtable (stored in flash) */
struct page_renderer_ops {
    void (*init)(page_renderer_t *self, int width, int height);
    void (*enter)(page_renderer_t *self);
    void (*exit)(page_renderer_t *self);
    void (*render)(page_renderer_t *self, uint8_t *fb, int width, int height);
    bool (*handle_input)(page_renderer_t *self, const ui_button_event_t *event);
    rawdraw_rect_t (*get_dirty_rect)(const page_renderer_t *self);
    bool (*needs_full_refresh)(const page_renderer_t *self);
    void (*mark_full_refresh)(page_renderer_t *self);
    void (*clear_full_refresh_flag)(page_renderer_t *self);
    bool (*append_text)(page_renderer_t *self, const char *chunk);
    void (*begin_stream)(page_renderer_t *self);
    void (*end_stream)(page_renderer_t *self);
};

/* Base struct — embed as first member in concrete renderer structs
 * to enable zero-cost safe upcast. */
struct page_renderer {
    const struct page_renderer_ops *ops;
    int                             width;
    int                             height;
    bool                            needs_full_refresh_flag;
};

typedef struct page_renderer_ops page_renderer_ops_t;

/* Inline helpers for dispatching through the vtable */
static inline void page_renderer_init(page_renderer_t *r, int width, int height)
{
    if (r && r->ops && r->ops->init)
        r->ops->init(r, width, height);
}
static inline void page_renderer_enter(page_renderer_t *r)
{
    if (r && r->ops && r->ops->enter)
        r->ops->enter(r);
}

static inline void page_renderer_exit(page_renderer_t *r)
{
    if (r && r->ops && r->ops->exit)
        r->ops->exit(r);
}

static inline void page_renderer_render(page_renderer_t *r, uint8_t *fb, int width, int height)
{
    if (r && r->ops && r->ops->render)
        r->ops->render(r, fb, width, height);
}

static inline bool page_renderer_handle_input(page_renderer_t *r, const ui_button_event_t *event)
{
    if (r && r->ops && r->ops->handle_input)
        return r->ops->handle_input(r, event);
    return false;
}

static inline rawdraw_rect_t page_renderer_get_dirty_rect(const page_renderer_t *r)
{
    if (r && r->ops && r->ops->get_dirty_rect)
        return r->ops->get_dirty_rect(r);
    rawdraw_rect_t empty = {0, 0, 0, 0};
    return empty;
}

static inline bool page_renderer_needs_full_refresh(const page_renderer_t *r)
{
    if (r && r->ops && r->ops->needs_full_refresh)
        return r->ops->needs_full_refresh(r);
    return r ? r->needs_full_refresh_flag : false;
}

static inline void page_renderer_mark_full_refresh(page_renderer_t *r)
{
    if (r && r->ops && r->ops->mark_full_refresh)
        r->ops->mark_full_refresh(r);
    else if (r)
        r->needs_full_refresh_flag = true;
}

static inline void page_renderer_clear_full_refresh_flag(page_renderer_t *r)
{
    if (r && r->ops && r->ops->clear_full_refresh_flag)
        r->ops->clear_full_refresh_flag(r);
    else if (r)
        r->needs_full_refresh_flag = false;
}

static inline bool page_renderer_append_text(page_renderer_t *r, const char *chunk)
{
    if (r && r->ops && r->ops->append_text)
        return r->ops->append_text(r, chunk);
    return false;
}

static inline void page_renderer_begin_stream(page_renderer_t *r)
{
    if (r && r->ops && r->ops->begin_stream)
        r->ops->begin_stream(r);
}

static inline void page_renderer_end_stream(page_renderer_t *r)
{
    if (r && r->ops && r->ops->end_stream)
        r->ops->end_stream(r);
}

#endif /* COMPONENTS_UI_INCLUDE_PAGE_RENDERER_H_ */
