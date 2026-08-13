/**
 * @file layout.h
 * @brief Layout and positioning utilities for rawdraw UI
 */

#ifndef RAWDRAW_LAYOUT_H
#define RAWDRAW_LAYOUT_H

#include <stdbool.h>
#include "font_engine.h"

typedef struct {
    bool valid;
    int top;
    int bottom;
    int height;
} rawdraw_text_ink_bounds_t;

/**
 * @brief Calculate the baseline Y coordinate for text centered in a line
 * @param font The font being used
 * @param center_y The target vertical center coordinate
 * @param visual_offset Vertically nudge the baseline (e.g. +1 for e-paper optical adjustment)
 * @return The baseline Y coordinate
 */
int rawdraw_layout_calc_baseline_y(const lv_font_t *font, int center_y, int visual_offset);

/**
 * @brief Calculate the top Y (cursor Y) coordinate from a baseline Y coordinate
 * @param font The font being used
 * @param baseline_y The baseline Y coordinate
 * @return The top Y coordinate
 */
int rawdraw_layout_top_y_from_baseline(const lv_font_t *font, int baseline_y);

/**
 * @brief Calculate the top Y (cursor Y) for a text line vertically centered in a box
 * @param font The font being used
 * @param box_top The top Y of the bounding box
 * @param box_height The height of the bounding box
 * @param visual_offset Vertically nudge the baseline
 * @return The top Y coordinate
 */
int rawdraw_layout_center_text_top_y(const lv_font_t *font, int box_top, int box_height, int visual_offset);

/**
 * @brief Measure the ink (visual pixel bounds) of a string relative to the baseline
 * @param font The font being used
 * @param text The UTF-8 string to measure
 * @return Struct containing top/bottom relative bounds and flag indicating validity
 */
rawdraw_text_ink_bounds_t rawdraw_layout_measure_text_ink_bounds(const lv_font_t *font, const char *text);

/**
 * @brief Calculate the top Y (cursor Y) to center the visual ink of text around center_y
 * @param font The font being used
 * @param text The UTF-8 string
 * @param center_y The target vertical center coordinate for the ink
 * @param visual_offset Vertically nudge the baseline
 * @return The top Y coordinate
 */
int rawdraw_layout_ink_centered_text_top_y(const lv_font_t *font, const char *text, int center_y, int visual_offset);

/**
 * @brief Calculate the top Y (cursor Y) to center the visual ink of text inside a box
 * @param font The font being used
 * @param text The UTF-8 string
 * @param box_top The top Y of the bounding box
 * @param box_height The height of the bounding box
 * @param visual_offset Vertically nudge the baseline
 * @return The top Y coordinate
 */
int rawdraw_layout_ink_centered_text_top_y_in_box(const lv_font_t *font, const char *text, int box_top, int box_height,
                                                  int visual_offset);

#endif // RAWDRAW_LAYOUT_H
