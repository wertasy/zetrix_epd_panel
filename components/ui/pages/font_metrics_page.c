/**
 * @file font_metrics_renderer.c
 * @brief Compact font metrics page for diagnosing rawdraw text placement —
 *        C port of C++ rawdraw::FontMetricsRenderer.
 */
#include "font_metrics_page.h"
#include "page_registry.h"

#include "rawdraw_ext.h"
#include "layout.h"
#include "style.h"

#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Helper                                                              */
/* ------------------------------------------------------------------ */

static void draw_metric_line(uint8_t *fb, int width, int height, int x, int *y, const char *text, const lv_font_t *font)
{
    rawdraw_draw_text(fb, width, height, x, *y, text, font, RAWDRAW_COLOR_BLACK);
    *y += (int)font->line_height + 6;
}

/* ------------------------------------------------------------------ */
/* PageRenderer vtable                                                 */
/* ------------------------------------------------------------------ */

void font_metrics_page_init(page_renderer_t *self, int width, int height)
{
    font_metrics_page_t *r          = (font_metrics_page_t *)self;
    r->base.width                   = width;
    r->base.height                  = height;
    r->base.needs_full_refresh_flag = true;
    r->font                         = &SourceHanSansSC_Regular_slim;
    r->title_font                   = &SourceHanSansSC_Medium_slim;
}

void font_metrics_page_render(page_renderer_t *self, uint8_t *fb, int width, int height)
{
    font_metrics_page_t *r = (font_metrics_page_t *)self;
    if (!fb)
        return;

    rawdraw_fill_rect(fb, width, height,
                      (rawdraw_rect_t){0, STYLE_STATUS_BAR_HEIGHT, width, height - STYLE_STATUS_BAR_HEIGHT},
                      RAWDRAW_COLOR_WHITE);

    const int x = 10;
    int       y = STYLE_STATUS_BAR_HEIGHT + 10;
    char      buf[96];

    draw_metric_line(fb, width, height, x, &y, "字体指标页：看公式，不看美观", r->font);

    const rawdraw_text_ink_bounds_t r_ink = rawdraw_layout_measure_text_ink_bounds(r->font, "识别中...");
    snprintf(buf, sizeof(buf), "Regular: lh=%d bl=%d inkTop=%d inkBot=%d inkH=%d", (int)r->font->line_height,
             (int)r->font->base_line, r_ink.top, r_ink.bottom, r_ink.height);
    draw_metric_line(fb, width, height, x, &y, buf, r->font);

    const rawdraw_text_ink_bounds_t s_ink = rawdraw_layout_measure_text_ink_bounds(r->font, "发送");
    snprintf(buf, sizeof(buf), "发送: inkTop=%d inkBot=%d inkH=%d", s_ink.top, s_ink.bottom, s_ink.height);
    draw_metric_line(fb, width, height, x, &y, buf, r->font);

    const rawdraw_text_ink_bounds_t m_ink = rawdraw_layout_measure_text_ink_bounds(r->title_font, "Macintosh");
    snprintf(buf, sizeof(buf), "Medium: lh=%d bl=%d inkTop=%d inkBot=%d inkH=%d", (int)r->title_font->line_height,
             (int)r->title_font->base_line, m_ink.top, m_ink.bottom, m_ink.height);
    draw_metric_line(fb, width, height, x, &y, buf, r->font);

    rawdraw_draw_hline(fb, width, height, y, x, STYLE_SCREEN_WIDTH - x, RAWDRAW_COLOR_BLACK);
    y += 10;

    const int box_y    = y;
    const int box_h    = 42;
    const int center_y = box_y + box_h / 2;
    const int line_y   = rawdraw_layout_center_text_top_y(r->font, box_y, box_h, 0);
    const int ink_y    = rawdraw_layout_ink_centered_text_top_y_in_box(r->font, "识别中...", box_y, box_h, 0);
    snprintf(buf, sizeof(buf), "42px框: lineTop=%d inkTop=%d delta=%d", line_y, ink_y, ink_y - line_y);
    draw_metric_line(fb, width, height, x, &y, buf, r->font);

    snprintf(buf, sizeof(buf), "line公式: top + (h-lh)/2 = %d", line_y);
    draw_metric_line(fb, width, height, x, &y, buf, r->font);

    snprintf(buf, sizeof(buf), "ink公式: center(%d)-inkCenter = %d", center_y, ink_y);
    draw_metric_line(fb, width, height, x, &y, buf, r->font);

    rawdraw_draw_rect_border(fb, width, height, (rawdraw_rect_t){x, y, STYLE_SCREEN_WIDTH - x * 2, box_h}, 1,
                             RAWDRAW_COLOR_BLACK);
    rawdraw_draw_hline(fb, width, height, y + box_h / 2, x, STYLE_SCREEN_WIDTH - x, RAWDRAW_COLOR_BLACK);
    rawdraw_draw_text(fb, width, height, x + 10,
                      rawdraw_layout_ink_centered_text_top_y_in_box(r->font, "识别中...", y, box_h, 0),
                      "识别中...（ink居中）", r->font, RAWDRAW_COLOR_BLACK);

    r->base.needs_full_refresh_flag = false;
}

bool font_metrics_page_handle_input(page_renderer_t *self, const ui_button_event_t *event)
{
    font_metrics_page_t *r = (font_metrics_page_t *)self;
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

EXT_RAM_BSS_ATTR font_metrics_page_t s_font_metrics_instance;

const page_renderer_ops_t font_metrics_page_ops = {
    .init                    = font_metrics_page_init,
    .render                  = font_metrics_page_render,
    .handle_input            = font_metrics_page_handle_input,
    .get_dirty_rect          = NULL,
    .needs_full_refresh      = NULL,
    .mark_full_refresh       = NULL,
    .clear_full_refresh_flag = NULL,
    .append_text             = NULL,
    .begin_stream            = NULL,
    .end_stream              = NULL,
};

PAGE_REGISTER(UI_PAGE_FONT_METRICS, "字体指标", NULL, true, 150, &font_metrics_page_ops,
              &s_font_metrics_instance.base);
