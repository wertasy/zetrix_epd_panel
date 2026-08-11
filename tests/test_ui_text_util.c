/**
 * @file test_ui_text_util.c
 * @brief Host tests for the shared page text utilities
 *        (ui_text_fit_to_width / ui_text_icon_glyph_for_code).
 *
 * These helpers are used by every page renderer; verify UTF-8-safe
 * truncation, ellipsis fitting, and QWeather icon-code mapping.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "ui_text_util.h"
#include "rawdraw_ext.h"

/* Dummy fonts for font_engine.h declarations. */
const lv_font_t SourceHanSansSC_Regular_slim;
const lv_font_t SourceHanSansSC_Medium_slim;
const lv_font_t font_zectrix_16_1;
const lv_font_t font_zectrix_48_1;
const lv_font_t weather_icons_16;
const lv_font_t weather_icons_48;

/* Mock font: every glyph is 8px wide so measuring is deterministic. */
static bool mock_get_glyph_dsc(const struct _lv_font_t *font, lv_font_glyph_dsc_t *dsc_out, uint32_t letter,
                               uint32_t letter_next)
{
    (void)font;
    (void)letter;
    (void)letter_next;
    dsc_out->resolved_font = font;
    dsc_out->adv_w         = 8;
    dsc_out->box_w         = 6;
    dsc_out->box_h         = 6;
    dsc_out->ofs_x         = 1;
    dsc_out->ofs_y         = 1;
    dsc_out->stride        = 1;
    dsc_out->format        = LV_FONT_GLYPH_FORMAT_A1;
    return true;
}

static const uint8_t mock_bitmap[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static const void   *mock_get_glyph_bitmap(lv_font_glyph_dsc_t *g, struct _lv_draw_buf_t *db)
{
    (void)g;
    (void)db;
    return mock_bitmap;
}

const lv_font_t mock_font = {
    .get_glyph_dsc    = mock_get_glyph_dsc,
    .get_glyph_bitmap = mock_get_glyph_bitmap,
    .release_glyph    = NULL,
    .line_height      = 8,
    .base_line        = 1,
};

static void test_fit_short_text(void)
{
    char out[64];
    ui_text_fit_to_width("abc", &mock_font, 100, out, sizeof(out));
    assert(strcmp(out, "abc") == 0);
    printf("fit short: '%s'\n", out);
}

static void test_fit_truncates_with_ellipsis(void)
{
    char out[64];
    /* 10 chars * 8px = 80px; max 40px -> truncate with "...". */
    ui_text_fit_to_width("abcdefghij", &mock_font, 40, out, sizeof(out));
    assert(strlen(out) > 0);
    assert(strlen(out) < 10);
    assert(strstr(out, "...") != NULL);
    printf("fit trunc: '%s'\n", out);
}

static void test_fit_empty_and_null(void)
{
    char out[8];
    out[0] = 'X';
    ui_text_fit_to_width("", &mock_font, 100, out, sizeof(out));
    assert(out[0] == '\0');
    ui_text_fit_to_width(NULL, &mock_font, 100, out, sizeof(out));
    assert(out[0] == '\0');
    ui_text_fit_to_width("abc", NULL, 100, out, sizeof(out));
    assert(out[0] == '\0');
    printf("fit empty/null ok\n");
}

static void test_fit_utf8_boundary(void)
{
    char        out[64];
    const char *text = "\xe4\xb8\xad\xe6\x96\x87\xe6\xb5\x8b\xe8\xaf\x95"; /* 中文测试 */
    /* Full text is 4 * 8px = 32px; max 40px -> returned unchanged. */
    ui_text_fit_to_width(text, &mock_font, 40, out, sizeof(out));
    assert(strcmp(out, text) == 0);
    /* max 25px < 32px -> truncation. The mock renders "..." as 3 glyphs at
     * 8px each (24px), so even 1 CJK + "..." (32px) exceeds 25px and only
     * the bare "..." fits; what matters is the output is valid UTF-8 and
     * shorter than the input (no mid-sequence split). */
    ui_text_fit_to_width(text, &mock_font, 25, out, sizeof(out));
    assert(strlen(out) < strlen(text));
    /* Truncated output must be either the bare "..." (3 bytes) or whole
     * 3-byte CJK chars followed by "...": never a split sequence. */
    const size_t n  = strlen(out);
    const bool   ok = (n == 3) || ((n - 3) % 3 == 0);
    assert(ok);
    printf("fit utf8: full='%s' truncated='%s' (%zu bytes)\n", text, out, strlen(out));
}

static void test_icon_mapping(void)
{
    assert(strcmp(ui_text_icon_glyph_for_code("100", "晴"), "\xef\x83\x9e") == 0); /* sun */
    assert(strcmp(ui_text_icon_glyph_for_code("101", "多云"), "\xef\x83\x82") == 0); /* cloud */
    assert(strcmp(ui_text_icon_glyph_for_code("104", "阴"), "\xef\x83\x82") == 0); /* cloud */
    assert(strcmp(ui_text_icon_glyph_for_code("300", "小雨"), "\xef\x83\xa9") == 0); /* rain */
    assert(strcmp(ui_text_icon_glyph_for_code("400", "雪"), "\xef\x8b\x9c") == 0); /* snow */
    assert(strcmp(ui_text_icon_glyph_for_code("500", "雾"), "\xef\x9d\x9f") == 0); /* fog */
    assert(strcmp(ui_text_icon_glyph_for_code("999", ""), "\xef\x83\x9e") == 0); /* default sun */
    /* Text-only fallback. */
    assert(strcmp(ui_text_icon_glyph_for_code("", "有雨"), "\xef\x83\xa9") == 0);
    printf("icon mapping ok\n");
}

/* ------------------------------------------------------------------ */
/* ui_text_wrap_lines tests                                            */
/* ------------------------------------------------------------------ */

static void test_wrap_simple(void)
{
    /* mock_font: 8px per char. max_width=24 -> 3 chars per line. */
    char lines[4][128];
    int  count = 0;
    ui_text_wrap_lines(&mock_font, "abcdef", 24, lines, 128, 4, &count);
    assert(count == 2);
    assert(strcmp(lines[0], "abc") == 0);
    assert(strcmp(lines[1], "def") == 0);
    printf("wrap simple: lines[0]='%s' lines[1]='%s'\n", lines[0], lines[1]);
}

static void test_wrap_newline_break(void)
{
    char lines[4][128];
    int  count = 0;
    ui_text_wrap_lines(&mock_font, "ab\ncd", 100, lines, 128, 4, &count);
    assert(count == 2);
    assert(strcmp(lines[0], "ab") == 0);
    assert(strcmp(lines[1], "cd") == 0);
    printf("wrap newline: lines[0]='%s' lines[1]='%s'\n", lines[0], lines[1]);
}

static void test_wrap_empty_newline_placeholder(void)
{
    /* A bare \n with no accumulated text should produce a " " line. */
    char lines[4][128];
    int  count = 0;
    ui_text_wrap_lines(&mock_font, "a\n\nb", 100, lines, 128, 4, &count);
    assert(count == 3);
    assert(strcmp(lines[0], "a") == 0);
    assert(strcmp(lines[1], " ") == 0);
    assert(strcmp(lines[2], "b") == 0);
    printf("wrap empty newline placeholder ok\n");
}

static void test_wrap_max_lines_ellipsis(void)
{
    /* 6 chars at 3-per-line = 2 lines. max_lines=1 -> ellipsize. */
    char lines[1][128];
    int  count = 99;
    ui_text_wrap_lines(&mock_font, "abcdef", 24, lines, 128, 1, &count);
    assert(count == 1);
    /* "abc" would be the first line; ellipsized to fit 24px. "a.." = 3 chars
     * but only "ab..." fits? adv_w=8: "a..."=4 chars=32px>24, "..."=24px=ok.
     * Actually "a" + "..." = 1+3 = 4 glyph-slots = 32px > 24.
     * So the fit_to_width will trim to just "..." (3 chars * 8px = 24px). */
    printf("wrap ellipsis: lines[0]='%s'\n", lines[0]);
    assert(lines[0][0] != '\0'); /* something was written */
}

static void test_wrap_null_and_empty(void)
{
    char lines[4][128];
    int  count = 5;
    ui_text_wrap_lines(&mock_font, NULL, 24, lines, 128, 4, &count);
    assert(count == 0);
    ui_text_wrap_lines(&mock_font, "", 24, lines, 128, 4, &count);
    assert(count == 0);
    printf("wrap null/empty ok\n");
}

static void test_wrap_utf8_cjk(void)
{
    /* Each CJK char is 3 UTF-8 bytes, 8px wide. max_width=24 -> 3 chars/line.
     * "你好世界" = 4 CJK chars, 12 bytes. At 3 per line -> 2 lines. */
    char lines[4][128];
    int  count = 0;
    ui_text_wrap_lines(&mock_font, "你好世界", 24, lines, 128, 4, &count);
    assert(count == 2);
    assert(strcmp(lines[0], "你好世") == 0);
    assert(strcmp(lines[1], "界") == 0);
    printf("wrap cjk: lines[0]='%s' lines[1]='%s'\n", lines[0], lines[1]);
}

int main(void)
{
    printf("Testing ui_text_util...\n");
    test_fit_short_text();
    test_fit_truncates_with_ellipsis();
    test_fit_empty_and_null();
    test_fit_utf8_boundary();
    test_icon_mapping();
    test_wrap_simple();
    test_wrap_newline_break();
    test_wrap_empty_newline_placeholder();
    test_wrap_max_lines_ellipsis();
    test_wrap_null_and_empty();
    test_wrap_utf8_cjk();
    printf("All ui_text_util tests passed!\n");
    return 0;
}
