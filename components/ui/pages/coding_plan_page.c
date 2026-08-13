/**
 * @file coding_plan_page.c
 * @brief Coding Plan usage page renderer — local pure-C implementation
 *        (计划 Task 6.10, 本地全新).
 *
 * Renders two quota progress bars (5-hour window and weekly cap), the quota
 * reset time, the trailing-7-day token total, a per-model token breakdown,
 * and a local bar chart drawn directly from the 7-day hourly usage series
 * (no LittleFS bitmap cache).
 */
#include "coding_plan_page.h"
#include "page_registry.h"
#include "data_refresh.h"

#include "rawdraw_ext.h"
#include "theme.h"
#include "style.h"
#include "layout.h"
#include "progress_bar.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Quota defaults (configurable; mirror Claude Code Pro plan caps).    */
/* ------------------------------------------------------------------ */

#define CODING_PLAN_5H_QUOTA_TOKENS 2000000ULL /* 2M tokens / 5h window */
#define CODING_PLAN_WEEK_QUOTA_TOKENS 10000000ULL /* 10M tokens / week     */

static const lv_font_t *const kCodingPlanFont = &SourceHanSansSC_Regular_slim;
static const lv_font_t *const kCodingPlanTitleFont = &SourceHanSansSC_Medium_slim;

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static void format_tokens(uint64_t tokens, char *out, int out_size)
{
    if (tokens >= 1000000ULL) {
        uint64_t m = (tokens + 500000ULL) / 1000000ULL;
        snprintf(out, out_size, "%lluM", (unsigned long long)m);
    } else if (tokens >= 1000ULL) {
        uint64_t k = (tokens + 500ULL) / 1000ULL;
        snprintf(out, out_size, "%lluK", (unsigned long long)k);
    } else {
        snprintf(out, out_size, "%llu", (unsigned long long)tokens);
    }
}

/* ------------------------------------------------------------------ */
/* Local bar chart (drawn from the 7-day hourly usage series)         */
/* ------------------------------------------------------------------ */

/* Aggregate the hourly series into 7 daily buckets and draw them as
 * accent-coloured bars inside the chart frame. Each hour h maps to bar
 * index (h * kBARS) / hours, so a full 168-point series collapses to one
 * 24-hour bar per day and shorter series still spread evenly. */
static void render_chart_from_data(page_renderer_t *self, uint8_t *fb, int width, int height, int panel_y, int panel_h)
{
    coding_plan_page_t *r = (coding_plan_page_t *)self;
    const rawdraw_color_t border = rawdraw_theme_color_for(THEME_TOKEN_BORDER);
    const rawdraw_color_t secondary = rawdraw_theme_color_for(THEME_TOKEN_TEXT_SECONDARY);
    const rawdraw_color_t accent = rawdraw_theme_style(THEME_TOKEN_ACCENT).bg;

    const int chart_x = 16;
    const int chart_w = width - 32; /* 368 */
    const int chart_y = panel_y + 8;
    const int chart_h = panel_h - 14;

    const int hours = r->data.hourly_count;
    enum {
        kBARS = 7
    };

    int bars_num = 0;
    uint64_t bar_values[16];
    memset(bar_values, 0, sizeof(bar_values));

    if (r->view_mode == 0) {
        if (hours <= 0) {
            const char *cap =
                "\xe6\x9a\x82\xe6\x97\xa0\xe7\x94\xa8\xe9\x87\x8f\xe8\xb6\x8b\xe5\x8a\xbf"; /* 暂无用量趋势 */
            const int cap_w = rawdraw_measure_text_width(cap, r->font);
            rawdraw_draw_text(fb, width, height, chart_x + (chart_w - cap_w) / 2,
                              rawdraw_layout_ink_centered_text_top_y_in_box(r->font, cap, chart_y, chart_h, 0), cap,
                              r->font, secondary);
            return;
        }
        bars_num = kBARS;
        const int limit = hours < CODING_PLAN_HOURS_7D ? hours : CODING_PLAN_HOURS_7D;
        for (int i = 0; i < limit; ++i) {
            int d = (i * kBARS) / limit;
            if (d < 0)
                d = 0;
            if (d >= kBARS)
                d = kBARS - 1;
            bar_values[d] += r->data.hourly_tokens[i];
        }
    } else {
        bars_num = r->data.per_model_count;
        if (bars_num > 16) {
            bars_num = 16;
        }
        if (bars_num <= 0) {
            const char *cap =
                "\xe6\x9a\x82\xe6\x97\xa0\xe6\xa8\xa1\xe5\x9e\x8b\xe7\x94\xa8\xe9\x87\x8f"; /* 暂无模型用量 */
            const int cap_w = rawdraw_measure_text_width(cap, r->font);
            rawdraw_draw_text(fb, width, height, chart_x + (chart_w - cap_w) / 2,
                              rawdraw_layout_ink_centered_text_top_y_in_box(r->font, cap, chart_y, chart_h, 0), cap,
                              r->font, secondary);
            return;
        }
        for (int i = 0; i < bars_num; ++i) {
            bar_values[i] = r->data.per_model[i].tokens;
        }
    }

    uint64_t max_tokens = 0;
    for (int d = 0; d < bars_num; ++d) {
        if (bar_values[d] > max_tokens)
            max_tokens = bar_values[d];
    }
    if (max_tokens == 0)
        max_tokens = 1;

    const int inset = 6;
    const int usable_w = chart_w - 2 * inset;
    int bar_gap = 12;
    int bar_w = 40;
    if (bars_num > 1) {
        bar_w = (usable_w - bar_gap * (bars_num - 1)) / bars_num;
        if (bar_w > 40) {
            bar_w = 40;
        }
    }

    const int base_y = chart_y + chart_h - 4;
    const int max_h = chart_h - 18;

    for (int d = 0; d < bars_num; ++d) {
        int bh = (int)((double)bar_values[d] * (double)max_h / (double)max_tokens);
        if (bar_values[d] > 0 && bh < 1)
            bh = 1;
        if (bh > 0) {
            const int bx = chart_x + inset + d * (bar_w + bar_gap);
            const int by = base_y - bh;
            rawdraw_fill_rect(fb, width, height, (rawdraw_rect_t){bx, by, bar_w, bh}, accent);

            /* Render token value above or inside the bar */
            char val_buf[24];
            format_tokens(bar_values[d], val_buf, sizeof(val_buf));
            const int val_w = rawdraw_measure_text_width(val_buf, r->font);
            const int val_x = bx + (bar_w - val_w) / 2;
            const int text_h = r->font->line_height;

            /* Check space above bar */
            const int space_above = by - chart_y;
            if (space_above >= text_h + 2) {
                /* Draw text above bar */
                rawdraw_draw_text(fb, width, height, val_x, by - text_h - 2, val_buf, r->font, secondary);
            } else {
                /* Draw text inside bar (contrasting color) */
                const rawdraw_color_t accent_fg = rawdraw_theme_style(THEME_TOKEN_ACCENT).fg;
                rawdraw_draw_text(fb, width, height, val_x, by + 2, val_buf, r->font, accent_fg);
            }
        }
    }

    /* Baseline */
    rawdraw_draw_hline(fb, width, height, base_y, chart_x + inset - 2,
                       chart_x + inset + (bar_w + bar_gap) * bars_num - bar_gap + 2, border);
}

/* ------------------------------------------------------------------ */
/* PageRenderer vtable                                                 */
/* ------------------------------------------------------------------ */

void coding_plan_page_init(page_renderer_t *self, int width, int height)
{
    coding_plan_page_t *r = (coding_plan_page_t *)self;
    r->base.width = width;
    r->base.height = height;
    r->base.needs_full_refresh_flag = true;
    r->font = kCodingPlanFont;
    r->title_font = kCodingPlanTitleFont;
    r->view_mode = 0;
    if (!r->has_data) {
        memset(&r->data, 0, sizeof(r->data));
    }
}

void coding_plan_page_render(page_renderer_t *self, uint8_t *fb, int width, int height)
{
    coding_plan_page_t *r = (coding_plan_page_t *)self;
    if (!fb)
        return;

    const rawdraw_paint_style_t bg_style = rawdraw_theme_style(THEME_TOKEN_BACKGROUND_PRIMARY);
    const rawdraw_paint_style_t card_style = rawdraw_theme_component(ROLE_CARD_DEFAULT);
    const rawdraw_color_t text = rawdraw_theme_color_for(THEME_TOKEN_TEXT_PRIMARY);
    const rawdraw_color_t secondary = rawdraw_theme_color_for(THEME_TOKEN_TEXT_SECONDARY);
    const rawdraw_color_t accent = rawdraw_theme_style(THEME_TOKEN_ACCENT).bg;
    const rawdraw_color_t track_color = RAWDRAW_COLOR_WHITE;

    const int content_top = STYLE_STATUS_BAR_HEIGHT + 2;
    rawdraw_draw_styled_rect(fb, width, height,
                             (rawdraw_rect_t){0, STYLE_STATUS_BAR_HEIGHT, width, height - STYLE_STATUS_BAR_HEIGHT},
                             &bg_style);

    if (!r->has_data) {
        const char *empty_text = "暂无用量数据";
        const int ew = rawdraw_measure_text_width(empty_text, r->font);
        rawdraw_draw_text(fb, width, height, (width - ew) / 2,
                          rawdraw_layout_ink_centered_text_top_y(r->font, empty_text, content_top + 90, 0), empty_text,
                          r->font, secondary);
    } else {
        /* Two side-by-side quota cards */
        const int card_w = (width - 16 * 3) / 2;
        const int card_h = 82;
        const int card_y = content_top + 6;

        /* Card A: 5-hour quota */
        const int ax = 16;
        rawdraw_draw_styled_round_rect(fb, width, height, (rawdraw_rect_t){ax, card_y, card_w, card_h},
                                       STYLE_BORDER_RADIUS_MD, &card_style);
        {
            char title_buf[64];
            snprintf(title_buf, sizeof(title_buf), "每5小时额度 %d%%", r->data.five_hour_pct);
            rawdraw_draw_text(fb, width, height, ax + 12, card_y + 8, title_buf, r->font, text);

            const int bar_x = ax + 12;
            const int bar_w = card_w - 24;
            const int bar_y = card_y + 32;
            rawdraw_draw_round_rect(fb, width, height, bar_x, bar_y, bar_w, STYLE_PROGRESS_HEIGHT,
                                    STYLE_BORDER_RADIUS_SM, track_color, RAWDRAW_COLOR_BLACK, 1);
            widget_progress_bar_t bar1;
            widget_progress_bar_init(&bar1, bar_x, bar_y, bar_w, STYLE_PROGRESS_HEIGHT);
            widget_progress_bar_set_value(&bar1, r->data.five_hour_pct);
            widget_progress_bar_set_radius(&bar1, STYLE_BORDER_RADIUS_SM);
            widget_progress_bar_set_bg_color(&bar1, track_color);
            widget_progress_bar_set_fg_color(&bar1, accent);
            widget_progress_bar_render(&bar1, fb, width, height);

            char reset_buf[64];
            const char *rt = r->data.five_hour_reset_time[0] ? r->data.five_hour_reset_time : "--:--";
            const char *rt_short = rt;
            if (strlen(rt) >= 11 && rt[5] == ' ') {
                rt_short = &rt[6];
            }
            snprintf(reset_buf, sizeof(reset_buf), "重置：%s", rt_short);
            rawdraw_draw_text(fb, width, height, ax + 12, card_y + 58, reset_buf, r->font, secondary);
        }

        /* Card B: weekly quota */
        const int bx = 16 + card_w + 16;
        rawdraw_draw_styled_round_rect(fb, width, height, (rawdraw_rect_t){bx, card_y, card_w, card_h},
                                       STYLE_BORDER_RADIUS_MD, &card_style);
        {
            char title_buf[64];
            snprintf(title_buf, sizeof(title_buf), "每周额度 %d%%", r->data.week_pct);
            rawdraw_draw_text(fb, width, height, bx + 12, card_y + 8, title_buf, r->font, text);

            const int bar_x = bx + 12;
            const int bar_w = card_w - 24;
            const int bar_y = card_y + 32;
            rawdraw_draw_round_rect(fb, width, height, bar_x, bar_y, bar_w, STYLE_PROGRESS_HEIGHT,
                                    STYLE_BORDER_RADIUS_SM, track_color, RAWDRAW_COLOR_BLACK, 1);
            widget_progress_bar_t bar2;
            widget_progress_bar_init(&bar2, bar_x, bar_y, bar_w, STYLE_PROGRESS_HEIGHT);
            widget_progress_bar_set_value(&bar2, r->data.week_pct);
            widget_progress_bar_set_radius(&bar2, STYLE_BORDER_RADIUS_SM);
            widget_progress_bar_set_bg_color(&bar2, track_color);
            widget_progress_bar_set_fg_color(&bar2, accent);
            widget_progress_bar_render(&bar2, fb, width, height);

            char reset_buf[64];
            const char *rt = r->data.week_reset_time[0] ? r->data.week_reset_time : "--:--";
            snprintf(reset_buf, sizeof(reset_buf), "重置：%s", rt);
            rawdraw_draw_text(fb, width, height, bx + 12, card_y + 58, reset_buf, r->font, secondary);
        }

        /* Bottom panel: combined per-model breakdown and bar chart */
        const int panel_y = card_y + card_h + 8;
        const int panel_h = height - panel_y - 8;
        rawdraw_paint_style_t white_card_style = card_style;
        white_card_style.bg = RAWDRAW_COLOR_WHITE;
        rawdraw_draw_styled_round_rect(fb, width, height, (rawdraw_rect_t){16, panel_y, width - 32, panel_h},
                                       STYLE_BORDER_RADIUS_MD, &white_card_style);

        render_chart_from_data(self, fb, width, height, panel_y, panel_h);

        /* Draw model list on top of chart space */
        {
            const int model_row_y = panel_y + 12;
            const int max_rows = 2;
            const int model_rows = r->data.per_model_count > max_rows ? max_rows : r->data.per_model_count;
            for (int i = 0; i < model_rows; ++i) {
                char model_buf[64];
                char tok_buf[24];
                format_tokens(r->data.per_model[i].tokens, tok_buf, sizeof(tok_buf));
                snprintf(model_buf, sizeof(model_buf), "%.10s", r->data.per_model[i].name);

                rawdraw_draw_text(fb, width, height, 16 + 12,
                                  rawdraw_layout_ink_centered_text_top_y(r->font, model_buf, model_row_y + i * 20, 0),
                                  model_buf, r->font, secondary);

                rawdraw_draw_text(fb, width, height, 16 + 12 + 84,
                                  rawdraw_layout_ink_centered_text_top_y(r->font, tok_buf, model_row_y + i * 20, 0),
                                  tok_buf, r->font, text);
            }
        }
    }

    r->base.needs_full_refresh_flag = false;
}

bool coding_plan_page_handle_input(page_renderer_t *self, const ui_button_event_t *event)
{
    coding_plan_page_t *r = (coding_plan_page_t *)self;
    switch (event->type) {
    case BTN_BOOT_CLICK:
        data_refresh_request(UI_PAGE_CODING_PLAN);
        r->base.needs_full_refresh_flag = true;
        return true;
    case BTN_UP_CLICK:
    case BTN_DOWN_CLICK:
        r->view_mode = (r->view_mode == 0) ? 1 : 0;
        r->base.needs_full_refresh_flag = true;
        return true;
    default:
        return false;
    }
}

/* ------------------------------------------------------------------ */
/* Data interface                                                      */
/* ------------------------------------------------------------------ */

void coding_plan_page_update(page_renderer_t *self, const coding_plan_data_t *data)
{
    coding_plan_page_t *r = (coding_plan_page_t *)self;
    if (!data)
        return;
    r->data = *data;
    if (r->data.per_model_count < 0)
        r->data.per_model_count = 0;
    if (r->data.per_model_count > CODING_PLAN_MAX_MODELS) {
        r->data.per_model_count = CODING_PLAN_MAX_MODELS;
    }
    if (r->data.hourly_count < 0)
        r->data.hourly_count = 0;
    if (r->data.hourly_count > CODING_PLAN_HOURS_7D) {
        r->data.hourly_count = CODING_PLAN_HOURS_7D;
    }
    r->has_data = true;
    r->base.needs_full_refresh_flag = true;
}

/* ------------------------------------------------------------------ */
/* vtable instance                                                     */
/* ------------------------------------------------------------------ */

EXT_RAM_BSS_ATTR coding_plan_page_t s_coding_plan_instance;

const page_renderer_ops_t coding_plan_page_ops = {
    .init = coding_plan_page_init,
    .render = coding_plan_page_render,
    .handle_input = coding_plan_page_handle_input,
    .get_dirty_rect = NULL,
    .needs_full_refresh = NULL,
    .mark_full_refresh = NULL,
    .clear_full_refresh_flag = NULL,
    .append_text = NULL,
    .begin_stream = NULL,
    .end_stream = NULL,
};

PAGE_REGISTER(UI_PAGE_CODING_PLAN, "用量统计", NULL, true, 40, &coding_plan_page_ops, &s_coding_plan_instance.base);
