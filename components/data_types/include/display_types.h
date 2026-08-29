/**
 * @file display_types.h
 * @brief Shared geometric types and their pure geometry ops for the display
 *        subsystem.
 *
 * Extracted from rawdraw.h so that bsp/epd_refresh.h can use rawdraw_rect_t
 * without pulling in the full rawdraw rendering API (Phase 4.1). Relocated
 * from rawdraw/include/ to the neutral data_types leaf so bsp_display no
 * longer depends on rawdraw (arch-hardening-plan §3.2.2).
 *
 * The pure rectangle operations below are the SINGLE SOURCE OF TRUTH for
 * this geometry: rawdraw_ext.c's public rawdraw_* helpers delegate here and
 * bsp_display/epd_refresh.c calls them directly, so both layers share one
 * implementation instead of drifting copies.
 */
#ifndef DATA_TYPES_DISPLAY_TYPES_H_
#define DATA_TYPES_DISPLAY_TYPES_H_

#ifdef __cplusplus
extern "C" {
#endif

/** Rectangle in framebuffer coordinates. */
typedef struct {
    int x, y, w, h;
} rawdraw_rect_t;

/** Alias for code that prefers the generic name. */
typedef rawdraw_rect_t display_rect_t;

/** 2D point. */
typedef struct {
    int x, y;
} rawdraw_point_t;

/** Clamped area; 0 for empty/degenerate rectangles. */
static inline int display_rect_area(rawdraw_rect_t r)
{
    return (r.w > 0 && r.h > 0) ? (r.w * r.h) : 0;
}

/** Bounding box of two rectangles; an empty operand yields the other one. */
static inline rawdraw_rect_t display_rect_union(rawdraw_rect_t a, rawdraw_rect_t b)
{
    if (display_rect_area(a) == 0)
        return b;
    if (display_rect_area(b) == 0)
        return a;
    int x1 = a.x < b.x ? a.x : b.x;
    int y1 = a.y < b.y ? a.y : b.y;
    int ax2 = a.x + a.w;
    int bx2 = b.x + b.w;
    int x2 = ax2 > bx2 ? ax2 : bx2;
    int ay2 = a.y + a.h;
    int by2 = b.y + b.h;
    int y2 = ay2 > by2 ? ay2 : by2;
    return (rawdraw_rect_t){x1, y1, x2 - x1, y2 - y1};
}

/** Expand the x range to byte (8-pixel) boundaries — the EPD partial-refresh
 *  grain. */
static inline rawdraw_rect_t display_align_x8(rawdraw_rect_t r)
{
    int x0 = (r.x / 8) * 8;
    int x1 = ((r.x + r.w + 7) / 8) * 8;
    return (rawdraw_rect_t){x0, r.y, x1 - x0, r.h};
}

#ifdef __cplusplus
}
#endif

#endif /* DATA_TYPES_DISPLAY_TYPES_H_ */
