/**
 * @file font_debug_renderer.c
 * @brief Large-row text alignment diagnostics for 400x300 rawdraw EPD —
 *        C port of C++ rawdraw::FontDebugRenderer.
 */
#include "font_debug_page.h"
#include "page_registry.h"

#include "rawdraw_ext.h"
#include "layout.h"
#include "style.h"

#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Diagnostic helpers                                                  */
/* ------------------------------------------------------------------ */

static void dashed_hline(uint8_t *fb, int width, int height, int y, int x1, int x2, rawdraw_color_t c)
{
    for (int x = x1; x <= x2; x += 5) {
        rawdraw_set_pixel(fb, width, height, x, y, c);
        if (x + 1 <= x2)
            rawdraw_set_pixel(fb, width, height, x + 1, y, c);
    }
}

static void draw_diagnostic_row(uint8_t *fb, int width, int height, int x, int y, int w, int h, const char *label,
                                const char *text, const lv_font_t *font, bool ink_centered)
{
    rawdraw_draw_rect_border(fb, width, height, (rawdraw_rect_t){x, y, w, h}, 1, RAWDRAW_COLOR_BLACK);
    const int center_y = y + h / 2;
    dashed_hline(fb, width, height, center_y, x + 1, x + w - 2, RAWDRAW_COLOR_BLACK);

    const int text_x = x + 8;
    const int text_y = ink_centered ? rawdraw_layout_ink_centered_text_top_y(font, text, center_y, 0)
                                    : rawdraw_layout_center_text_top_y(font, y, h, 0);
    rawdraw_draw_text(fb, width, height, text_x, text_y, text, font, RAWDRAW_COLOR_BLACK);

    const rawdraw_text_ink_bounds_t ink = rawdraw_layout_measure_text_ink_bounds(font, text);
    if (ink.valid) {
        rawdraw_draw_hline(fb, width, height, text_y + ink.top, text_x, text_x + 34, RAWDRAW_COLOR_BLACK);
        rawdraw_draw_hline(fb, width, height, text_y + ink.bottom, text_x, text_x + 34, RAWDRAW_COLOR_BLACK);
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "%s y=%d", label, text_y);
    rawdraw_draw_text(fb, width, height, x + w - 122, y + 4, buf, &SourceHanSansSC_Regular_slim, RAWDRAW_COLOR_BLACK);
}

/* ------------------------------------------------------------------ */
/* PageRenderer vtable                                                 */
/* ------------------------------------------------------------------ */

void font_debug_page_init(page_renderer_t *self, int width, int height)
{
    font_debug_page_t *r = (font_debug_page_t *)self;
    r->base.width = width;
    r->base.height = height;
    r->base.needs_full_refresh_flag = true;
    r->font = &SourceHanSansSC_Regular_slim;
    r->title_font = &SourceHanSansSC_Medium_slim;
}

void font_debug_page_render(page_renderer_t *self, uint8_t *fb, int width, int height)
{
    font_debug_page_t *r = (font_debug_page_t *)self;
    if (!fb)
        return;

    rawdraw_fill_rect(fb, width, height,
                      (rawdraw_rect_t){0, STYLE_STATUS_BAR_HEIGHT, width, height - STYLE_STATUS_BAR_HEIGHT},
                      RAWDRAW_COLOR_WHITE);

    const int x = 10;
    const int w = STYLE_SCREEN_WIDTH - 20;
    int y = STYLE_STATUS_BAR_HEIGHT + 10;

    const char *hint = "虚线=框中心 黑短线=真实字形上下界";
    rawdraw_draw_text(fb, width, height, x, y, hint, r->font, RAWDRAW_COLOR_BLACK);
    y += 22;

    draw_diagnostic_row(fb, width, height, x, y, w, 42, "line", "识别中...", r->font, false);
    y += 50;
    draw_diagnostic_row(fb, width, height, x, y, w, 42, "ink", "识别中...", r->font, true);
    y += 50;
    draw_diagnostic_row(fb, width, height, x, y, w, 42, "line", "发送", r->font, false);
    y += 50;
    draw_diagnostic_row(fb, width, height, x, y, w, 48, "inkM", "Macintosh 关于", r->title_font, true);

    const rawdraw_text_ink_bounds_t regular = rawdraw_layout_measure_text_ink_bounds(r->font, "识别中...");
    char footer[96];
    snprintf(footer, sizeof(footer), "Regular lh=%d bl=%d ink=%d..%d h=%d", (int)r->font->line_height,
             (int)r->font->base_line, regular.top, regular.bottom, regular.height);
    rawdraw_draw_text(fb, width, height, x, STYLE_SCREEN_HEIGHT - 20, footer, r->font, RAWDRAW_COLOR_BLACK);

    r->base.needs_full_refresh_flag = false;
}

bool font_debug_page_handle_input(page_renderer_t *self, const ui_button_event_t *event)
{
    font_debug_page_t *r = (font_debug_page_t *)self;
    switch (event->type) {
    case BTN_BOOT_CLICK:
    case BTN_UP_CLICK:
    case BTN_DOWN_CLICK:
        r->base.needs_full_refresh_flag = true;
        return true;
    default:
        return false;
    }
}

/* ------------------------------------------------------------------ */
/* vtable instance                                                     */
/* ------------------------------------------------------------------ */

EXT_RAM_BSS_ATTR font_debug_page_t s_font_debug_instance;

const page_renderer_ops_t font_debug_page_ops = {
    .init = font_debug_page_init,
    .render = font_debug_page_render,
    .handle_input = font_debug_page_handle_input,
    .get_dirty_rect = NULL,
    .needs_full_refresh = NULL,
    .mark_full_refresh = NULL,
    .clear_full_refresh_flag = NULL,
    .append_text = NULL,
    .begin_stream = NULL,
    .end_stream = NULL,
};

PAGE_REGISTER(UI_PAGE_FONT_DEBUG, "对齐测试", NULL, true, 140, &font_debug_page_ops, &s_font_debug_instance.base);
