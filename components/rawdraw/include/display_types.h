/**
 * @file display_types.h
 * @brief Shared geometric types for display subsystem.
 *
 * Extracted from rawdraw.h so that bsp/epd_refresh.h can use rawdraw_rect_t
 * without pulling in the full rawdraw rendering API (Phase 4.1).
 */
#ifndef DISPLAY_TYPES_H_
#define DISPLAY_TYPES_H_

#include <stdint.h>

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

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_TYPES_H_ */
