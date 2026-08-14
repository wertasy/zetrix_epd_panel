#ifdef ESP_PLATFORM
#    include "sdkconfig.h"
#endif

#include "theme.h"
#include "rawdraw_ext.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

static rawdraw_theme_id_t g_current_theme = THEME_INDUSTRIAL;

static const rawdraw_theme_definition_t themes[] = {
    // 1. Nintendo Pop / Industrial
    {.id = THEME_INDUSTRIAL,
     .key = "nintendo_pop",
     .display_name = "Nintendo Pop",
     .tokens = {[THEME_TOKEN_TEXT_PRIMARY] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_BLACK,
                                              DITHER_NONE, 1, false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_TEXT_SECONDARY] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_BLACK,
                                                DITHER_LIGHT_GRAY, 1, false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_BACKGROUND_PRIMARY] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_BLACK,
                                                    DITHER_NONE, 1, false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_BACKGROUND_SECONDARY] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_YELLOW, RAWDRAW_COLOR_BLACK,
                                                      DITHER_NONE, 1, false, REFRESH_SMALL_ACCENT},
                [THEME_TOKEN_ACCENT] = {RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_RED, RAWDRAW_COLOR_BLACK, DITHER_NONE, 1,
                                        false, REFRESH_SMALL_ACCENT},
                [THEME_TOKEN_WARNING] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_YELLOW, RAWDRAW_COLOR_BLACK, DITHER_NONE, 1,
                                         false, REFRESH_SMALL_ACCENT},
                [THEME_TOKEN_DANGER] = {RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_RED, RAWDRAW_COLOR_RED, DITHER_NONE, 1,
                                        false, REFRESH_SMALL_ACCENT},
                [THEME_TOKEN_SUCCESS_LIKE] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_YELLOW, RAWDRAW_COLOR_BLACK,
                                              DITHER_NONE, 1, false, REFRESH_SMALL_ACCENT},
                [THEME_TOKEN_SELECTED] = {RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_RED, RAWDRAW_COLOR_BLACK, DITHER_NONE, 1,
                                          false, REFRESH_SMALL_ACCENT},
                [THEME_TOKEN_DISABLED] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_BLACK,
                                          DITHER_LIGHT_GRAY, 1, false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_BORDER] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_BLACK, DITHER_NONE, 1,
                                        false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_SHADOW] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_BLACK,
                                        DITHER_LIGHT_GRAY, 1, false, REFRESH_AVOID_LARGE_AREA},
                [THEME_TOKEN_FOCUS] = {RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_RED, RAWDRAW_COLOR_BLACK, DITHER_NONE, 2,
                                       false, REFRESH_SMALL_ACCENT},
                [THEME_TOKEN_BADGE] = {RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_RED, RAWDRAW_COLOR_BLACK, DITHER_NONE, 1,
                                       false, REFRESH_SMALL_ACCENT},
                [THEME_TOKEN_PROGRESS_FILL] = {RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_RED, RAWDRAW_COLOR_BLACK, DITHER_NONE,
                                               1, false, REFRESH_SMALL_ACCENT}}},
    // 2. Bright Lemon
    {.id = THEME_BRIGHT_LEMON,
     .key = "bright_lemon",
     .display_name = "Bright Lemon",
     .tokens = {[THEME_TOKEN_TEXT_PRIMARY] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_BLACK,
                                              DITHER_NONE, 1, false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_TEXT_SECONDARY] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_BLACK,
                                                DITHER_SOFT, 1, false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_BACKGROUND_PRIMARY] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_BLACK,
                                                    DITHER_NONE, 1, false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_BACKGROUND_SECONDARY] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_YELLOW, RAWDRAW_COLOR_BLACK,
                                                      DITHER_SOFT, 1, false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_ACCENT] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_YELLOW, RAWDRAW_COLOR_BLACK, DITHER_NONE, 1,
                                        false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_WARNING] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_YELLOW, RAWDRAW_COLOR_BLACK, DITHER_GOLD, 1,
                                         false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_DANGER] = {RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_RED, RAWDRAW_COLOR_RED, DITHER_NONE, 1,
                                        false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_SUCCESS_LIKE] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_YELLOW, RAWDRAW_COLOR_BLACK,
                                              DITHER_NONE, 1, false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_SELECTED] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_YELLOW, RAWDRAW_COLOR_BLACK, DITHER_NONE,
                                          1, false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_DISABLED] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_BLACK,
                                          DITHER_LIGHT_GRAY, 1, false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_BORDER] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_BLACK, DITHER_NONE, 1,
                                        false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_SHADOW] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_YELLOW, RAWDRAW_COLOR_BLACK, DITHER_SOFT, 1,
                                        false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_FOCUS] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_YELLOW, RAWDRAW_COLOR_BLACK, DITHER_NONE, 2,
                                       false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_BADGE] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_YELLOW, RAWDRAW_COLOR_BLACK, DITHER_NONE, 1,
                                       false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_PROGRESS_FILL] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_YELLOW, RAWDRAW_COLOR_BLACK,
                                               DITHER_NONE, 1, false, REFRESH_STATIC_SAFE}}},
    // 3. Console
    {.id = THEME_CONSOLE,
     .key = "console",
     .display_name = "Console",
     .tokens = {[THEME_TOKEN_TEXT_PRIMARY] = {RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_WHITE,
                                              DITHER_NONE, 1, false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_TEXT_SECONDARY] = {RAWDRAW_COLOR_YELLOW, RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_WHITE,
                                                DITHER_NONE, 1, false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_BACKGROUND_PRIMARY] = {RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_WHITE,
                                                    DITHER_NONE, 1, false, REFRESH_AVOID_LARGE_AREA},
                [THEME_TOKEN_BACKGROUND_SECONDARY] = {RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_WHITE,
                                                      DITHER_GRAY, 1, false, REFRESH_AVOID_LARGE_AREA},
                [THEME_TOKEN_ACCENT] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_YELLOW, RAWDRAW_COLOR_YELLOW, DITHER_NONE, 1,
                                        false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_WARNING] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_YELLOW, RAWDRAW_COLOR_YELLOW, DITHER_NONE,
                                         1, false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_DANGER] = {RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_RED, RAWDRAW_COLOR_RED, DITHER_NONE, 1,
                                        false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_SUCCESS_LIKE] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_YELLOW, RAWDRAW_COLOR_YELLOW,
                                              DITHER_NONE, 1, false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_SELECTED] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_YELLOW, RAWDRAW_COLOR_YELLOW, DITHER_NONE,
                                          1, false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_DISABLED] = {RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_WHITE, DITHER_GRAY, 1,
                                          false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_BORDER] = {RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_WHITE, DITHER_NONE, 1,
                                        false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_SHADOW] = {RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_WHITE, DITHER_GRAY, 1,
                                        false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_FOCUS] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_YELLOW, RAWDRAW_COLOR_YELLOW, DITHER_NONE, 2,
                                       false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_BADGE] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_YELLOW, RAWDRAW_COLOR_YELLOW, DITHER_NONE, 1,
                                       false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_PROGRESS_FILL] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_YELLOW, RAWDRAW_COLOR_YELLOW,
                                               DITHER_NONE, 1, false, REFRESH_STATIC_SAFE}}},
    // 4. Peach Paper
    {.id = THEME_PEACH_PAPER,
     .key = "peach_paper",
     .display_name = "Peach Paper",
     .tokens = {[THEME_TOKEN_TEXT_PRIMARY] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_BLACK,
                                              DITHER_NONE, 1, false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_TEXT_SECONDARY] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_BLACK,
                                                DITHER_PEACH, 1, false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_BACKGROUND_PRIMARY] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_BLACK,
                                                    DITHER_NONE, 1, false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_BACKGROUND_SECONDARY] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_BLACK,
                                                      DITHER_PEACH, 1, false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_ACCENT] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_YELLOW, RAWDRAW_COLOR_BLACK, DITHER_PEACH, 1,
                                        false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_WARNING] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_YELLOW, RAWDRAW_COLOR_BLACK, DITHER_NONE, 1,
                                         false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_DANGER] = {RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_RED, RAWDRAW_COLOR_RED, DITHER_NONE, 1,
                                        false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_SUCCESS_LIKE] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_YELLOW, RAWDRAW_COLOR_BLACK,
                                              DITHER_SOFT, 1, false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_SELECTED] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_YELLOW, RAWDRAW_COLOR_BLACK, DITHER_PEACH,
                                          1, false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_DISABLED] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_BLACK,
                                          DITHER_LIGHT_GRAY, 1, false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_BORDER] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_BLACK, DITHER_NONE, 1,
                                        false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_SHADOW] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_BLACK, DITHER_PEACH, 1,
                                        false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_FOCUS] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_YELLOW, RAWDRAW_COLOR_BLACK, DITHER_PEACH, 2,
                                       false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_BADGE] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_YELLOW, RAWDRAW_COLOR_BLACK, DITHER_PEACH, 1,
                                       false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_PROGRESS_FILL] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_YELLOW, RAWDRAW_COLOR_BLACK,
                                               DITHER_GOLD, 1, false, REFRESH_STATIC_SAFE}}},
    // 5. Sticker
    {.id = THEME_STICKER,
     .key = "sticker",
     .display_name = "Sticker",
     .tokens = {[THEME_TOKEN_TEXT_PRIMARY] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_BLACK,
                                              DITHER_NONE, 1, false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_TEXT_SECONDARY] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_BLACK,
                                                DITHER_SOFT, 1, false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_BACKGROUND_PRIMARY] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_BLACK,
                                                    DITHER_NONE, 1, false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_BACKGROUND_SECONDARY] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_BLACK,
                                                      DITHER_LIGHT_GRAY, 1, false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_ACCENT] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_YELLOW, RAWDRAW_COLOR_BLACK, DITHER_NONE, 1,
                                        false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_WARNING] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_YELLOW, RAWDRAW_COLOR_BLACK, DITHER_NONE, 1,
                                         false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_DANGER] = {RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_RED, RAWDRAW_COLOR_RED, DITHER_NONE, 1,
                                        false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_SUCCESS_LIKE] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_YELLOW, RAWDRAW_COLOR_BLACK,
                                              DITHER_NONE, 1, false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_SELECTED] = {RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_BLACK, DITHER_NONE, 1,
                                          false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_DISABLED] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_BLACK, DITHER_GRAY, 1,
                                          false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_BORDER] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_BLACK, DITHER_NONE, 1,
                                        false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_SHADOW] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_BLACK, DITHER_GRAY, 1,
                                        false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_FOCUS] = {RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_BLACK, DITHER_NONE, 2,
                                       false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_BADGE] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_YELLOW, RAWDRAW_COLOR_BLACK, DITHER_NONE, 1,
                                       false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_PROGRESS_FILL] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_RED, RAWDRAW_COLOR_RED, DITHER_NONE,
                                               1, false, REFRESH_STATIC_SAFE}}},
    // 6. Candy Pop
    {.id = THEME_CANDY_POP,
     .key = "candy_pop",
     .display_name = "Candy Pop",
     .tokens = {[THEME_TOKEN_TEXT_PRIMARY] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_BLACK,
                                              DITHER_NONE, 1, false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_TEXT_SECONDARY] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_RED,
                                                DITHER_PEACH, 1, false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_BACKGROUND_PRIMARY] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_BLACK,
                                                    DITHER_NONE, 1, false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_BACKGROUND_SECONDARY] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_YELLOW,
                                                      DITHER_GOLD, 1, false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_ACCENT] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_YELLOW, RAWDRAW_COLOR_BLACK, DITHER_NONE, 1,
                                        false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_WARNING] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_YELLOW, RAWDRAW_COLOR_BLACK, DITHER_NONE, 1,
                                         false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_DANGER] = {RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_RED, RAWDRAW_COLOR_RED, DITHER_NONE, 1,
                                        false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_SUCCESS_LIKE] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_YELLOW, RAWDRAW_COLOR_BLACK,
                                              DITHER_NONE, 1, false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_SELECTED] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_YELLOW, RAWDRAW_COLOR_RED, DITHER_NONE, 1,
                                          false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_DISABLED] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_BLACK,
                                          DITHER_LIGHT_GRAY, 1, false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_BORDER] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_RED, DITHER_NONE, 1,
                                        false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_SHADOW] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_RED, DITHER_PEACH, 1,
                                        false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_FOCUS] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_YELLOW, RAWDRAW_COLOR_RED, DITHER_NONE, 2,
                                       false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_BADGE] = {RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_YELLOW, RAWDRAW_COLOR_RED, DITHER_NONE, 1,
                                       false, REFRESH_STATIC_SAFE},
                [THEME_TOKEN_PROGRESS_FILL] = {RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_RED, RAWDRAW_COLOR_RED, DITHER_NONE,
                                               1, false, REFRESH_STATIC_SAFE}}}};

static const rawdraw_theme_definition_t *ThemeById(rawdraw_theme_id_t id)
{
    int theme_count = (int)(sizeof(themes) / sizeof(themes[0]));
    for (int i = 0; i < theme_count; i++) {
        if (themes[i].id == id) {
            return &themes[i];
        }
    }
    return &themes[0];
}

static rawdraw_paint_style_t NormalizeForPanel(rawdraw_paint_style_t style)
{
#if CONFIG_ZECTRIX_EPD_PANEL_1BPP
    // On the black/white EPD, RED and YELLOW are electrically rendered as
    // black. Keep raw photo pixels untouched in the display driver, but make
    // semantic UI surfaces degrade to readable white cards with black ink.
    if (style.bg == RAWDRAW_COLOR_RED || style.bg == RAWDRAW_COLOR_YELLOW) {
        style.bg = RAWDRAW_COLOR_WHITE;
        if (style.dither == DITHER_NONE) {
            style.dither = DITHER_LIGHT_GRAY;
        }
    }
    if (style.fg == RAWDRAW_COLOR_RED || style.fg == RAWDRAW_COLOR_YELLOW) {
        style.fg = RAWDRAW_COLOR_BLACK;
    }
    if (style.bg == RAWDRAW_COLOR_BLACK && style.fg == RAWDRAW_COLOR_BLACK) {
        style.fg = RAWDRAW_COLOR_WHITE;
    } else if (style.bg == RAWDRAW_COLOR_WHITE && style.fg == RAWDRAW_COLOR_WHITE) {
        style.fg = RAWDRAW_COLOR_BLACK;
    }

    if (style.border == RAWDRAW_COLOR_RED || style.border == RAWDRAW_COLOR_YELLOW) {
        style.border = RAWDRAW_COLOR_BLACK;
    }
    if (style.bg == RAWDRAW_COLOR_BLACK && style.border == RAWDRAW_COLOR_BLACK) {
        style.border = RAWDRAW_COLOR_WHITE;
    } else if (style.bg == RAWDRAW_COLOR_WHITE && style.border == RAWDRAW_COLOR_WHITE) {
        style.border = RAWDRAW_COLOR_BLACK;
    }

    if (style.dither == DITHER_ORANGE || style.dither == DITHER_PEACH || style.dither == DITHER_GOLD) {
        style.dither = DITHER_LIGHT_GRAY;
    }
#endif
    return style;
}

static bool DitherPixel(rawdraw_dither_token_t token, int x, int y)
{
    switch (token) {
    case DITHER_GRAY:
        return ((x & 1) == 0) && ((y & 1) == 0);
    case DITHER_LIGHT_GRAY:
        return ((x & 3) == 0) && ((y & 3) == 0);
    case DITHER_ORANGE:
    case DITHER_GOLD:
        return ((x & 3) != 0) || ((y & 3) != 0);
    case DITHER_PEACH:
        return ((x & 3) == 0) && ((y & 1) == 0);
    case DITHER_NOISE:
        return ((((uint32_t)x * 17) ^ ((uint32_t)y * 31)) & 7) < 3;
    case DITHER_SOFT:
        return ((x & 7) == 0) && ((y & 3) == 0);
    case DITHER_NONE:
    default:
        return false;
    }
}

/* Resolve the two colours a dither token alternates between for a given style.
 * `on` corresponds to DitherPixel() == true.  Mirrors the colour-selection
 * logic that previously lived inline in DitherColor(), hoisted out so callers
 * can compute it once per draw instead of once per pixel. */
static void dither_colors(rawdraw_dither_token_t token, rawdraw_paint_style_t style, rawdraw_color_t *on_color,
                          rawdraw_color_t *off_color)
{
    switch (token) {
    case DITHER_ORANGE:
        *on_color = RAWDRAW_COLOR_YELLOW;
        *off_color = RAWDRAW_COLOR_RED;
        return;
    case DITHER_PEACH:
        *on_color = RAWDRAW_COLOR_RED;
        *off_color = RAWDRAW_COLOR_WHITE;
        return;
    case DITHER_GOLD:
        *on_color = RAWDRAW_COLOR_YELLOW;
        *off_color = RAWDRAW_COLOR_WHITE;
        return;
    case DITHER_GRAY:
    case DITHER_LIGHT_GRAY:
    case DITHER_NOISE:
    case DITHER_SOFT:
    default:
        *on_color = style.fg;
        *off_color = style.bg;
        return;
    }
}

/* Precomputed dither pattern lookup table.
 *
 * Every DitherPixel() expression is periodic in both x and y with a period that
 * divides 8 (see the period table in docs/kernel-techniques-analysis.md), so the
 * full pattern collapses to an 8x8 tile: dither_pattern[token][y & 7][x & 7].
 * This removes the per-pixel switch from the hot fill loops for just 512 bytes
 * of static memory. */
#define DITHER_PATTERN_PERIOD 8
static bool dither_pattern[DITHER_SOFT + 1][DITHER_PATTERN_PERIOD][DITHER_PATTERN_PERIOD];
static bool s_dither_lut_ready = false;

void rawdraw_dither_lut_init(void)
{
    for (int token = 0; token <= DITHER_SOFT; ++token) {
        for (int y = 0; y < DITHER_PATTERN_PERIOD; ++y) {
            for (int x = 0; x < DITHER_PATTERN_PERIOD; ++x)
                dither_pattern[token][y][x] = DitherPixel((rawdraw_dither_token_t)token, x, y);
        }
    }
    s_dither_lut_ready = true;
}

static inline void ensure_dither_lut_ready(void)
{
    if (!s_dither_lut_ready)
        rawdraw_dither_lut_init();
}

/* Fill a single horizontal run [x_start, x_end) on scanline `y` with the dither
 * pattern for (token, style).  Colour selection uses the precomputed LUT (no
 * per-pixel switch) and pixels are packed into 2bpp framebuffer bytes: fully
 * aligned interior bytes are written directly while only the sub-byte edges need
 * a read-modify-write, mirroring the kernel cfb_fillrect batched-write approach. */
static void fill_dithered_row(uint8_t *fb, int width, int y, int x_start, int x_end, rawdraw_dither_token_t token,
                              rawdraw_paint_style_t style)
{
    if (x_end <= x_start)
        return;

    const int bytes_per_row = (width * 2 + 7) >> 3;
    uint8_t *const row = fb + (size_t)y * (size_t)bytes_per_row;

    rawdraw_color_t on_color, off_color;
    dither_colors(token, style, &on_color, &off_color);

    int first_byte = x_start >> 2;
    int last_byte = (x_end - 1) >> 2;

    for (int b = first_byte; b <= last_byte; ++b) {
        int px0 = b << 2; /* first pixel index in this byte */
        uint8_t out = 0;
        for (int sub = 0; sub < 4; ++sub) {
            bool on = dither_pattern[token][y & 7][(px0 + sub) & 7];
            rawdraw_color_t c = on ? on_color : off_color;
            out = (uint8_t)((out << 2) | (c & 0x03));
        }

        int lo = x_start - px0; /* first in-range sub-pixel, clamped >= 0 */
        if (lo < 0)
            lo = 0;
        int hi = x_end - px0; /* one past last in-range sub-pixel */
        if (hi > 4)
            hi = 4;

        if (lo == 0 && hi == 4) {
            row[b] = out; /* fully aligned interior byte */
        } else {
            uint8_t mask = 0; /* 2bpp bits covering in-range pixels */
            for (int sub = lo; sub < hi; ++sub)
                mask |= (uint8_t)(0x03 << ((3 - sub) << 1));
            row[b] = (uint8_t)((row[b] & (uint8_t)~mask) | (out & mask));
        }
    }
}

rawdraw_theme_id_t rawdraw_theme_current_id(void)
{
    return g_current_theme;
}

const rawdraw_theme_definition_t *rawdraw_theme_current(void)
{
    return ThemeById(g_current_theme);
}

const rawdraw_theme_definition_t *rawdraw_theme_get(rawdraw_theme_id_t id)
{
    return ThemeById(id);
}

bool rawdraw_theme_set(rawdraw_theme_id_t id)
{
    int theme_count = (int)(sizeof(themes) / sizeof(themes[0]));
    if (id < 0 || id >= theme_count)
        return false;
    g_current_theme = id;
    return true;
}

bool rawdraw_theme_set_by_key(const char *key)
{
    if (!key)
        return false;
    rawdraw_theme_id_t id = rawdraw_theme_from_key(key, THEME_INDUSTRIAL);
    if (id == THEME_INDUSTRIAL) {
        // verify if the key matches the industrial theme's key or display name/fallback
        if (strcmp(key, "nintendo_pop") != 0 && strcmp(key, "industrial") != 0) {
            return false;
        }
    }
    return rawdraw_theme_set(id);
}

rawdraw_paint_style_t rawdraw_theme_style(rawdraw_theme_token_t token)
{
    if (token < 0 || token >= THEME_TOKEN_COUNT) {
        return NormalizeForPanel(rawdraw_theme_current()->tokens[0]);
    }
    return NormalizeForPanel(rawdraw_theme_current()->tokens[token]);
}

rawdraw_paint_style_t rawdraw_theme_component(rawdraw_component_role_t role)
{
    switch (role) {
    case ROLE_BUTTON_NORMAL:
        return rawdraw_theme_style(THEME_TOKEN_ACCENT);
    case ROLE_BUTTON_SELECTED:
    case ROLE_TODO_SELECTED:
    case ROLE_SETTINGS_SELECTED:
    case ROLE_QUICK_SWITCH_ROW:
        return rawdraw_theme_style(THEME_TOKEN_SELECTED);
    case ROLE_BUTTON_DISABLED:
    case ROLE_TODO_COMPLETED:
        return rawdraw_theme_style(THEME_TOKEN_DISABLED);
    case ROLE_BUTTON_DANGER:
    case ROLE_TODO_OVERDUE:
        return rawdraw_theme_style(THEME_TOKEN_DANGER);
    case ROLE_CARD_ELEVATED:
        return rawdraw_theme_style(THEME_TOKEN_BACKGROUND_SECONDARY);
    case ROLE_CARD_WARNING:
        return rawdraw_theme_style(THEME_TOKEN_WARNING);
    case ROLE_PROGRESS:
        return rawdraw_theme_style(THEME_TOKEN_PROGRESS_FILL);
    case ROLE_PANEL:
    case ROLE_CARD_DEFAULT:
    case ROLE_TODO_NORMAL:
        return rawdraw_theme_style(THEME_TOKEN_BACKGROUND_SECONDARY);
    case ROLE_STATUS_BAR:
    case ROLE_MODAL:
    case ROLE_SETTINGS_ROW:
    default:
        return rawdraw_theme_style(THEME_TOKEN_BACKGROUND_PRIMARY);
    }
}

rawdraw_color_t rawdraw_theme_color_for(rawdraw_theme_token_t token)
{
    return rawdraw_theme_style(token).fg;
}

int rawdraw_theme_count(void)
{
    return (int)(sizeof(themes) / sizeof(themes[0]));
}

rawdraw_theme_id_t rawdraw_theme_at(int index)
{
    int theme_count = (int)(sizeof(themes) / sizeof(themes[0]));
    if (index < 0 || index >= theme_count)
        return THEME_INDUSTRIAL;
    return themes[index].id;
}

const char *rawdraw_theme_key(rawdraw_theme_id_t id)
{
    return ThemeById(id)->key;
}

const char *rawdraw_theme_display_name(rawdraw_theme_id_t id)
{
    return ThemeById(id)->display_name;
}

rawdraw_theme_id_t rawdraw_theme_from_key(const char *key, rawdraw_theme_id_t fallback)
{
    if (!key || key[0] == '\0')
        return fallback;
    int theme_count = (int)(sizeof(themes) / sizeof(themes[0]));
    for (int i = 0; i < theme_count; i++) {
        if (strcmp(key, themes[i].key) == 0) {
            return themes[i].id;
        }
    }
    if (strcmp(key, "industrial") == 0) {
        return THEME_INDUSTRIAL;
    }
    return fallback;
}

rawdraw_paint_style_t rawdraw_make_paint(rawdraw_color_t fg, rawdraw_color_t bg, rawdraw_color_t border,
                                         rawdraw_dither_token_t dither, uint8_t border_width,
                                         rawdraw_refresh_cost_t refresh_cost)
{
    rawdraw_paint_style_t s;
    s.fg = fg;
    s.bg = bg;
    s.border = border;
    s.dither = dither;
    s.border_width = border_width;
    s.invert_text = false;
    s.refresh_cost = refresh_cost;
    return s;
}

void rawdraw_draw_styled_rect(uint8_t *fb, int width, int height, rawdraw_rect_t r, const rawdraw_paint_style_t *style)
{
    if (!fb || !style || r.w <= 0 || r.h <= 0)
        return;
    const rawdraw_paint_style_t panel_style = NormalizeForPanel(*style);
    if (panel_style.dither == DITHER_NONE) {
        rawdraw_fill_rect(fb, width, height, r, panel_style.bg);
        return;
    }
    int x_start = r.x;
    int y_start = r.y;
    int x_end = r.x + r.w;
    int y_end = r.y + r.h;
    if (x_start < 0)
        x_start = 0;
    if (y_start < 0)
        y_start = 0;
    if (x_end > width)
        x_end = width;
    if (y_end > height)
        y_end = height;
    ensure_dither_lut_ready();
    for (int y = y_start; y < y_end; ++y)
        fill_dithered_row(fb, width, y, x_start, x_end, panel_style.dither, panel_style);
}

void rawdraw_draw_styled_round_rect(uint8_t *fb, int width, int height, rawdraw_rect_t r, int radius,
                                    const rawdraw_paint_style_t *style)
{
    if (!fb || !style || r.w <= 0 || r.h <= 0)
        return;
    const rawdraw_paint_style_t panel_style = NormalizeForPanel(*style);
    if (panel_style.dither == DITHER_NONE) {
        rawdraw_draw_round_rect(fb, width, height, r.x, r.y, r.w, r.h, radius, (int)panel_style.bg,
                                (int)panel_style.border, (int)panel_style.border_width);
        return;
    }
    rawdraw_draw_round_rect(fb, width, height, r.x, r.y, r.w, r.h, radius, (int)panel_style.bg, (int)panel_style.border,
                            (int)panel_style.border_width);

    int bw = (int)panel_style.border_width;
    rawdraw_rect_t inner = {r.x + bw, r.y + bw, r.w - bw * 2, r.h - bw * 2};
    int inner_radius = radius - bw;
    if (inner_radius < 0)
        inner_radius = 0;

    int x_start = inner.x;
    int y_start = inner.y;
    int x_end = inner.x + inner.w;
    int y_end = inner.y + inner.h;

    if (x_start < 0)
        x_start = 0;
    if (y_start < 0)
        y_start = 0;
    if (x_end > width)
        x_end = width;
    if (y_end > height)
        y_end = height;

    ensure_dither_lut_ready();
    rawdraw_color_t on_color, off_color;
    dither_colors(panel_style.dither, panel_style, &on_color, &off_color);

    for (int y = y_start; y < y_end; ++y) {
        for (int x = x_start; x < x_end; ++x) {
            if (!rawdraw_point_in_rounded_rect(x, y, inner, inner_radius))
                continue;
            bool on = dither_pattern[panel_style.dither][y & 7][x & 7];
            rawdraw_color_t color = on ? on_color : off_color;
            rawdraw_set_pixel_unchecked(fb, width, height, x, y, (int)color);
        }
    }
}

void rawdraw_draw_styled_border(uint8_t *fb, int width, int height, rawdraw_rect_t r,
                                const rawdraw_paint_style_t *style)
{
    if (!fb || !style || r.w <= 0 || r.h <= 0)
        return;
    const rawdraw_paint_style_t panel_style = NormalizeForPanel(*style);
    rawdraw_draw_rect_border(fb, width, height, r, panel_style.border_width, panel_style.border);
}

void rawdraw_draw_styled_text(uint8_t *fb, int width, int height, int x, int y, const char *text, const lv_font_t *font,
                              const rawdraw_paint_style_t *style)
{
    if (!fb || !text || !font || !style)
        return;
    const rawdraw_paint_style_t panel_style = NormalizeForPanel(*style);
    rawdraw_draw_text(fb, width, height, x, y, text, font, (int)panel_style.fg);
}

void rawdraw_draw_styled_icon(uint8_t *fb, int width, int height, int x, int y, const char *icon_code,
                              const lv_font_t *font, const rawdraw_paint_style_t *style)
{
    if (!fb || !icon_code || !font || !style)
        return;
    const rawdraw_paint_style_t panel_style = NormalizeForPanel(*style);
    rawdraw_draw_text(fb, width, height, x, y, icon_code, font, (int)panel_style.fg);
}

void rawdraw_draw_styled_progress(uint8_t *fb, int width, int height, rawdraw_rect_t r, int value_pct,
                                  const rawdraw_paint_style_t *style, int radius)
{
    if (!fb || !style || r.w <= 0 || r.h <= 0)
        return;
    const rawdraw_paint_style_t panel_style = NormalizeForPanel(*style);
    rawdraw_draw_progress(fb, width, height, r, value_pct, panel_style.bg, panel_style.fg, radius);
}
