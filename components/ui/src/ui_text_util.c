/**
 * @file ui_text_util.c
 * @brief Shared text utilities for page renderers.
 *
 * Ported from file-local statics in the C++ renderers
 * (weather_renderer.cc, chat_renderer.cc, etc.).
 */
#include "ui_text_util.h"
#include "rawdraw_ext.h"

#include <stdlib.h>
#include <string.h>

char *ui_text_fit_to_width(const char *text, const lv_font_t *font, int max_width, char *out, int out_size)
{
    if (!out || out_size <= 0)
        return out;
    out[0] = '\0';
    if (!font || max_width <= 0 || !text || !*text)
        return out;

    if (rawdraw_measure_text_width(text, font) <= max_width) {
        /* Fits as-is; copy full text. */
        strncpy(out, text, (size_t)out_size - 1);
        out[out_size - 1] = '\0';
        return out;
    }

    /* Greedily append characters while "prefix..." still fits. */
    const size_t ellipsis_len = 3; /* "..." in UTF-8 */
    size_t       out_len      = 0;
    const char  *p            = text;
    while (*p) {
        /* Decode one UTF-8 character. */
        unsigned char c = (unsigned char)*p;
        int           seq_len;
        if (c < 0x80) {
            seq_len = 1;
        } else if ((c & 0xE0) == 0xC0) {
            seq_len = 2;
        } else if ((c & 0xF0) == 0xE0) {
            seq_len = 3;
        } else if ((c & 0xF8) == 0xF0) {
            seq_len = 4;
        } else {
            seq_len = 1; /* invalid lead byte; treat as single */
        }

        if (out_len + (size_t)seq_len + ellipsis_len >= (size_t)out_size - 1) {
            break;
        }

        /* Tentatively append this char plus "...". */
        char   tmp[256];
        size_t tmp_len = out_len;
        memcpy(tmp, out, out_len);
        memcpy(tmp + tmp_len, p, (size_t)seq_len);
        tmp_len += (size_t)seq_len;
        memcpy(tmp + tmp_len, "...", ellipsis_len);
        tmp_len += ellipsis_len;
        tmp[tmp_len] = '\0';

        if (rawdraw_measure_text_width(tmp, font) > max_width) {
            break;
        }
        memcpy(out + out_len, p, (size_t)seq_len);
        out_len += (size_t)seq_len;
        out[out_len] = '\0';
        p += seq_len;
    }

    if (out_len + ellipsis_len < (size_t)out_size) {
        memcpy(out + out_len, "...", ellipsis_len);
        out[out_len + ellipsis_len] = '\0';
    }
    return out;
}

const char *ui_text_icon_glyph_for_code(const char *icon_code, const char *weather_text)
{
    int code = 0;
    if (icon_code) {
        code = atoi(icon_code);
    }
    const bool has_text = weather_text && *weather_text;

    /* 100 = 晴 (sun) */
    if (code == 100 || (has_text && strstr(weather_text, "晴"))) {
        return "\xef\x83\x9e"; /* sun */
    }
    /* 101-103 = 多云 / 晴间多云 */
    if ((code >= 101 && code <= 103) ||
        (has_text && (strstr(weather_text, "多云") || strstr(weather_text, "晴间多云")))) {
        return "\xef\x83\x82"; /* cloud */
    }
    /* 104 = 阴 */
    if (code == 104 || (has_text && strstr(weather_text, "阴"))) {
        return "\xef\x83\x82"; /* cloud */
    }
    /* 300-399 = 雨 */
    if ((code >= 300 && code <= 399) || (has_text && strstr(weather_text, "雨"))) {
        return "\xef\x83\xa9"; /* rain */
    }
    /* 400-499 = 雪 */
    if ((code >= 400 && code <= 499) || (has_text && strstr(weather_text, "雪"))) {
        return "\xef\x8b\x9c"; /* snow */
    }
    /* 500-599 = 雾 / 霾 */
    if ((code >= 500 && code <= 599) || (has_text && (strstr(weather_text, "雾") || strstr(weather_text, "霾")))) {
        return "\xef\x9d\x9f"; /* smog/fog */
    }
    return "\xef\x83\x9e"; /* default sun */
}

/* ------------------------------------------------------------------ */
/* Line wrapper                                                        */
/* ------------------------------------------------------------------ */

void ui_text_wrap_lines(const lv_font_t *font, const char *text, int max_width, char out[][128], int line_buf_size,
                        int max_lines, int *out_count)
{
    if (out_count)
        *out_count = 0;
    if (!out || line_buf_size <= 0 || max_lines <= 0)
        return;
    if (!font || max_width <= 0 || !text || !*text)
        return;

    int  line_count = 0;
    char current[128];
    current[0]        = '\0';
    const int cur_cap = (int)sizeof(current);

    const char *p             = text;
    const char *last_consumed = text; /* tracks progress for ellipsis check */
    while (*p) {
        const char    *start = p;
        const uint32_t ch    = utf8_next(&p);
        if (ch == 0)
            break;
        const size_t seq_len = (size_t)(p - start);

        if (ch == '\n') {
            /* Flush the current line (use " " for an empty newline). */
            if (line_count < max_lines) {
                const char *fill = current[0] ? current : " ";
                strncpy(out[line_count], fill, (size_t)line_buf_size - 1);
                out[line_count][line_buf_size - 1] = '\0';
                ++line_count;
            }
            current[0]    = '\0';
            last_consumed = p;
            if (line_count >= max_lines)
                break;
            continue;
        }

        /* Hard byte cap: a pathological narrow-glyph accumulation must not
         * overflow the 128-byte scratch even before the width check fires. */
        if ((int)strlen(current) + (int)seq_len >= cur_cap - 2) {
            if (line_count < max_lines) {
                strncpy(out[line_count], current, (size_t)line_buf_size - 1);
                out[line_count][line_buf_size - 1] = '\0';
                ++line_count;
            }
            memcpy(current, start, seq_len);
            current[seq_len] = '\0';
            last_consumed    = p;
            if (line_count >= max_lines)
                break;
            continue;
        }

        /* Tentatively append this char and measure. */
        char         next_line[128];
        const size_t cur_len = strlen(current);
        memcpy(next_line, current, cur_len);
        memcpy(next_line + cur_len, start, seq_len);
        next_line[cur_len + seq_len] = '\0';

        if (current[0] != '\0' && rawdraw_measure_text_width(next_line, font) > max_width) {
            /* The new char would overflow — flush current, start fresh. */
            if (line_count < max_lines) {
                strncpy(out[line_count], current, (size_t)line_buf_size - 1);
                out[line_count][line_buf_size - 1] = '\0';
                ++line_count;
            }
            memcpy(current, start, seq_len);
            current[seq_len] = '\0';
            last_consumed    = p;
            if (line_count >= max_lines)
                break;
        } else {
            memcpy(current, next_line, cur_len + seq_len + 1);
            last_consumed = p;
        }
    }

    /* Flush any trailing partial line. */
    if (line_count < max_lines && current[0] != '\0') {
        strncpy(out[line_count], current, (size_t)line_buf_size - 1);
        out[line_count][line_buf_size - 1] = '\0';
        ++line_count;
        last_consumed = p;
    }

    /* If we hit max_lines and text remains, ellipsize the last visible line. */
    if (line_count == max_lines && last_consumed && *last_consumed) {
        char fitted[128];
        ui_text_fit_to_width(out[max_lines - 1], font, max_width, fitted, sizeof(fitted));
        strncpy(out[max_lines - 1], fitted, (size_t)line_buf_size - 1);
        out[max_lines - 1][line_buf_size - 1] = '\0';
    }

    if (out_count)
        *out_count = line_count;
}
