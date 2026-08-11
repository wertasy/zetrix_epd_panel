/**
 * @file ui_text_util.h
 * @brief Shared text utilities for page renderers — C port of helpers that
 *        were file-local statics in the C++ renderers.
 */
#ifndef COMPONENTS_UI_INCLUDE_UI_TEXT_UTIL_H_
#define COMPONENTS_UI_INCLUDE_UI_TEXT_UTIL_H_

#include <stdbool.h>
#include "font_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Truncate @p text with "..." so it fits within @p max_width.
 *
 * Uses a caller-provided buffer (reentrant; no static state).
 * UTF-8 safe: truncation happens on character boundaries.
 *
 * @return The buffer (== @p out) for chaining.
 */
char *ui_text_fit_to_width(const char *text, const lv_font_t *font, int max_width, char *out, int out_size);

/**
 * @brief Map a QWeather icon code / weather text to a FontAwesome glyph
 *        (UTF-8 codepoint) suitable for weather_icons fonts.
 */
const char *ui_text_icon_glyph_for_code(const char *icon_code, const char *weather_text);

/**
 * @brief Greedy word/char-aware line wrapper (UTF-8 safe).
 *
 * Breaks @p text into display lines that each fit within @p max_width
 * (measured via @c rawdraw_measure_text_width). Newlines force a break.
 * If wrapping fills exactly @p max_lines and text remains, the last line
 * is ellipsized with @c ui_text_fit_to_width.
 *
 * @param font        Font for width measurement (must be non-NULL).
 * @param text        NUL-terminated UTF-8 source (may be NULL/empty).
 * @param max_width   Maximum pixel width of a line (must be > 0).
 * @param out         Caller buffer; each row must be @p line_buf_size bytes.
 * @param line_buf_size  Bytes per row in @p out (e.g. 128).
 * @param max_lines   Row capacity of @p out.
 * @param out_count   Written: number of populated rows (0 on bad input).
 */
void ui_text_wrap_lines(const lv_font_t *font, const char *text, int max_width, char out[][128], int line_buf_size,
                        int max_lines, int *out_count);

#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_UI_INCLUDE_UI_TEXT_UTIL_H_ */
