#ifndef MAIN_FONT_ENGINE_H_
#define MAIN_FONT_ENGINE_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef LV_FONT_DECLARE
    #define RAWDRAW_HAS_LVGL 1
#else
    #define RAWDRAW_HAS_LVGL 0
#endif

#if !RAWDRAW_HAS_LVGL

struct _lv_draw_buf_t;
typedef void lv_cache_entry_t;

typedef enum {
    LV_FONT_GLYPH_FORMAT_A1 = 0x01,
    LV_FONT_GLYPH_FORMAT_A8 = 0x08,
} lv_font_glyph_format_t;

typedef struct {
    const struct _lv_font_t * resolved_font;
    uint16_t adv_w;
    uint16_t box_w;
    uint16_t box_h;
    int16_t  ofs_x;
    int16_t  ofs_y;
    uint16_t stride;
    lv_font_glyph_format_t format;
    uint8_t is_placeholder : 1;
    uint8_t req_raw_bitmap : 1;
    int32_t outline_stroke_width;
    union {
        uint32_t index;
        const void * src;
    } gid;
    lv_cache_entry_t * entry;
} lv_font_glyph_dsc_t;

typedef struct _lv_font_t {
    bool (*get_glyph_dsc)(const struct _lv_font_t * font, lv_font_glyph_dsc_t * dsc_out,
                          uint32_t letter, uint32_t letter_next);
    const void * (*get_glyph_bitmap)(lv_font_glyph_dsc_t * g_dsc, struct _lv_draw_buf_t * draw_buf);
    int32_t line_height;
    int32_t base_line;
    uint8_t subpx : 2;
    uint8_t kerning : 1;
    uint8_t static_bitmap : 1;
    int8_t underline_position;
    int8_t underline_thickness;
    const void * dsc;
    const struct _lv_font_t * fallback;
    void * user_data;
} lv_font_t;

#define LV_FONT_FMT_PLAIN       0
#define LV_FONT_FMT_COMPRESSED  1

static inline bool lv_font_get_glyph_dsc(const lv_font_t* font, lv_font_glyph_dsc_t* dsc_out,
                                         uint32_t letter, uint32_t letter_next) {
    if (!font || !font->get_glyph_dsc) return false;
    return font->get_glyph_dsc(font, dsc_out, letter, letter_next);
}

static inline const void* lv_font_get_glyph_bitmap(lv_font_glyph_dsc_t* g_dsc,
                                                   struct _lv_draw_buf_t* draw_buf) {
    if (!g_dsc || !g_dsc->resolved_font || !g_dsc->resolved_font->get_glyph_bitmap) return NULL;
    return g_dsc->resolved_font->get_glyph_bitmap(g_dsc, draw_buf);
}

#ifndef LV_FONT_DECLARE
#define LV_FONT_DECLARE(font_name) extern const lv_font_t font_name;
#endif

#endif /* !RAWDRAW_HAS_LVGL */

#ifndef FONT_DECLARE
#define FONT_DECLARE LV_FONT_DECLARE
#endif

static inline uint32_t utf8_next(const char** pp) {
    const uint8_t* p = (const uint8_t*)*pp;
    if (*p == 0) return 0;

    uint32_t c;
    int len;

    if (*p < 0x80) {
        c = *p;
        len = 1;
    } else if ((*p & 0xE0) == 0xC0) {
        c = *p & 0x1F;
        len = 2;
    } else if ((*p & 0xF0) == 0xE0) {
        c = *p & 0x0F;
        len = 3;
    } else if ((*p & 0xF8) == 0xF0) {
        c = *p & 0x07;
        len = 4;
    } else {
        *pp += 1;
        return 0xFFFD;
    }

    for (int i = 1; i < len; i++) {
        if ((p[i] & 0xC0) != 0x80) {
            *pp += 1;
            return 0xFFFD;
        }
        c = (c << 6) | (p[i] & 0x3F);
    }

    *pp += len;
    return c;
}

FONT_DECLARE(SourceHanSansSC_Regular_slim);
FONT_DECLARE(SourceHanSansSC_Medium_slim);
FONT_DECLARE(font_zectrix_16_1);
FONT_DECLARE(font_zectrix_48_1);
FONT_DECLARE(weather_icons_16);
FONT_DECLARE(weather_icons_48);

#define BUILTIN_TEXT_FONT SourceHanSansSC_Regular_slim

#endif // MAIN_FONT_ENGINE_H_
