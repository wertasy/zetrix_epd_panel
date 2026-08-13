#ifndef RAWDRAW_THEME_H
#define RAWDRAW_THEME_H

#include <stdint.h>
#include <stdbool.h>
#include "rawdraw.h"
#include "font_engine.h"

typedef enum {
    THEME_INDUSTRIAL = 0,
    THEME_BRIGHT_LEMON,
    THEME_CONSOLE,
    THEME_PEACH_PAPER,
    THEME_STICKER,
    THEME_CANDY_POP
} rawdraw_theme_id_t;

typedef enum {
    THEME_TOKEN_TEXT_PRIMARY = 0,
    THEME_TOKEN_TEXT_SECONDARY,
    THEME_TOKEN_BACKGROUND_PRIMARY,
    THEME_TOKEN_BACKGROUND_SECONDARY,
    THEME_TOKEN_ACCENT,
    THEME_TOKEN_WARNING,
    THEME_TOKEN_DANGER,
    THEME_TOKEN_SUCCESS_LIKE,
    THEME_TOKEN_SELECTED,
    THEME_TOKEN_DISABLED,
    THEME_TOKEN_BORDER,
    THEME_TOKEN_SHADOW,
    THEME_TOKEN_FOCUS,
    THEME_TOKEN_BADGE,
    THEME_TOKEN_PROGRESS_FILL,
    THEME_TOKEN_COUNT
} rawdraw_theme_token_t;

typedef enum {
    DITHER_NONE = 0,
    DITHER_GRAY,
    DITHER_LIGHT_GRAY,
    DITHER_ORANGE,
    DITHER_PEACH,
    DITHER_GOLD,
    DITHER_NOISE,
    DITHER_SOFT
} rawdraw_dither_token_t;

typedef enum {
    ROLE_BUTTON_NORMAL = 0,
    ROLE_BUTTON_SELECTED,
    ROLE_BUTTON_DISABLED,
    ROLE_BUTTON_DANGER,
    ROLE_CARD_DEFAULT,
    ROLE_CARD_ELEVATED,
    ROLE_CARD_WARNING,
    ROLE_TODO_NORMAL,
    ROLE_TODO_SELECTED,
    ROLE_TODO_COMPLETED,
    ROLE_TODO_OVERDUE,
    ROLE_MODAL,
    ROLE_PANEL,
    ROLE_STATUS_BAR,
    ROLE_PROGRESS,
    ROLE_SETTINGS_ROW,
    ROLE_SETTINGS_SELECTED,
    ROLE_QUICK_SWITCH_ROW
} rawdraw_component_role_t;

typedef enum {
    REFRESH_STATIC_SAFE = 0,
    REFRESH_SMALL_ACCENT,
    REFRESH_AVOID_FREQUENT,
    REFRESH_AVOID_LARGE_AREA
} rawdraw_refresh_cost_t;

typedef struct {
    rawdraw_color_t fg;
    rawdraw_color_t bg;
    rawdraw_color_t border;
    rawdraw_dither_token_t dither;
    uint8_t border_width;
    bool invert_text;
    rawdraw_refresh_cost_t refresh_cost;
} rawdraw_paint_style_t;

typedef struct {
    rawdraw_theme_id_t id;
    const char *key;
    const char *display_name;
    rawdraw_paint_style_t tokens[THEME_TOKEN_COUNT];
} rawdraw_theme_definition_t;

// Theme Manager C APIs
rawdraw_theme_id_t rawdraw_theme_current_id(void);
const rawdraw_theme_definition_t *rawdraw_theme_current(void);
const rawdraw_theme_definition_t *rawdraw_theme_get(rawdraw_theme_id_t id);
bool rawdraw_theme_set(rawdraw_theme_id_t id);
bool rawdraw_theme_set_by_key(const char *key);
rawdraw_paint_style_t rawdraw_theme_style(rawdraw_theme_token_t token);
rawdraw_paint_style_t rawdraw_theme_component(rawdraw_component_role_t role);
rawdraw_color_t rawdraw_theme_color_for(rawdraw_theme_token_t token);
int rawdraw_theme_count(void);
rawdraw_theme_id_t rawdraw_theme_at(int index);
const char *rawdraw_theme_key(rawdraw_theme_id_t id);
const char *rawdraw_theme_display_name(rawdraw_theme_id_t id);
rawdraw_theme_id_t rawdraw_theme_from_key(const char *key, rawdraw_theme_id_t fallback);
rawdraw_paint_style_t rawdraw_make_paint(rawdraw_color_t fg, rawdraw_color_t bg, rawdraw_color_t border,
                                         rawdraw_dither_token_t dither, uint8_t border_width,
                                         rawdraw_refresh_cost_t refresh_cost);
void rawdraw_dither_lut_init(void);

// Styled drawing APIs (always passing height parameter)
void rawdraw_draw_styled_rect(uint8_t *fb, int width, int height, rawdraw_rect_t r, const rawdraw_paint_style_t *style);
void rawdraw_draw_styled_round_rect(uint8_t *fb, int width, int height, rawdraw_rect_t r, int radius,
                                    const rawdraw_paint_style_t *style);
void rawdraw_draw_styled_border(uint8_t *fb, int width, int height, rawdraw_rect_t r,
                                const rawdraw_paint_style_t *style);
void rawdraw_draw_styled_text(uint8_t *fb, int width, int height, int x, int y, const char *text, const lv_font_t *font,
                              const rawdraw_paint_style_t *style);
void rawdraw_draw_styled_icon(uint8_t *fb, int width, int height, int x, int y, const char *icon_code,
                              const lv_font_t *font, const rawdraw_paint_style_t *style);
void rawdraw_draw_styled_progress(uint8_t *fb, int width, int height, rawdraw_rect_t r, int value_pct,
                                  const rawdraw_paint_style_t *style, int radius);

#endif // RAWDRAW_THEME_H
