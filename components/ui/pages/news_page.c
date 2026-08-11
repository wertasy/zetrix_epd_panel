/**
 * @file news_renderer.c
 * @brief News feed page renderer — C port of C++ rawdraw::NewsRenderer.
 *
 * Shows a scrollable news list; BOOT opens a preview modal with a
 * close / read-aloud footer action.
 */
#include "news_page.h"
#include "page_registry.h"
#include "fa_settings.h"

#include "rawdraw_ext.h"
#include "theme.h"
#include "style.h"
#include "layout.h"
#include "ui_text_util.h"
#include "modal.h"

#include <stdio.h>
#include <string.h>

#define NEWS_PANEL_X 6
#define NEWS_PANEL_Y (STYLE_STATUS_BAR_HEIGHT + 4)
#define NEWS_PANEL_W (STYLE_SCREEN_WIDTH - 12)
#define NEWS_PANEL_H 256
#define NEWS_VISIBLE_ROWS 7
#define NEWS_FOOTER_Y 264
#define NEWS_FOOTER_H 26
#define NEWS_ITEM_H (NEWS_PANEL_H / NEWS_VISIBLE_ROWS) /* 36 */
#define NEWS_ITEM_GAP 0

/* Preview modal line storage: title(2) + meta(1) + summary(20). */
#define NEWS_MAX_LINES 24
#define NEWS_LINE_LEN 128
#define NEWS_ROW_H 20

static const lv_font_t *const kNewsFont      = &SourceHanSansSC_Regular_slim;
static const lv_font_t *const kNewsTitleFont = &SourceHanSansSC_Medium_slim;

/* ------------------------------------------------------------------ */
/* PageRenderer vtable                                                 */
/* ------------------------------------------------------------------ */

void news_page_init(page_renderer_t *self, int width, int height)
{
    news_page_t *r                  = (news_page_t *)self;
    r->base.width                   = width;
    r->base.height                  = height;
    r->count                        = 0;
    r->selected_index               = 0;
    r->scroll_offset                = 0;
    r->preview_open                 = false;
    r->footer_focus                 = 0;
    r->preview_scroll               = 0;
    r->font                         = kNewsFont;
    r->title_font                   = kNewsTitleFont;
    r->tts_request_cb               = NULL;
    r->tts_ctx                      = NULL;
    r->base.needs_full_refresh_flag = true;
}

/* Page gained focus: request a redraw but keep the loaded news list. */
static void news_page_enter(page_renderer_t *self)
{
    news_page_t *r = (news_page_t *)self;
    if (!r)
        return;
    r->base.needs_full_refresh_flag = true;
}

static void news_render_item(page_renderer_t *self, uint8_t *fb, int width, int height, int y, int index, bool selected)
{
    news_page_t                *r              = (news_page_t *)self;
    const news_item_t          *item           = &r->items[index];
    const rawdraw_paint_style_t selected_style = rawdraw_theme_component(ROLE_SETTINGS_SELECTED);
    const rawdraw_color_t       text           = rawdraw_theme_color_for(THEME_TOKEN_TEXT_PRIMARY);
    const rawdraw_color_t       secondary      = rawdraw_theme_color_for(THEME_TOKEN_TEXT_SECONDARY);
    const rawdraw_color_t       border         = rawdraw_theme_color_for(THEME_TOKEN_BORDER);
    const rawdraw_rect_t        row            = {NEWS_PANEL_X + 1, y, NEWS_PANEL_W - 2, NEWS_ITEM_H};

    const int center_y = row.y + row.h / 2;
    const int text_y   = rawdraw_layout_ink_centered_text_top_y(r->font, "字", center_y, 0);

    char index_buf[16];
    snprintf(index_buf, sizeof(index_buf), "%d", index + 1);
    rawdraw_draw_text(fb, width, height, row.x + 8, text_y, index_buf, r->font,
                      selected ? selected_style.border : secondary);
    if (selected) {
        rawdraw_draw_rect(fb, width, height, row.x + 4, center_y - 7, 3, 14, selected_style.border);
    }
    const int index_w = rawdraw_measure_text_width(index_buf, r->font);
    const int title_x = row.x + index_w + 12;
    char      title[NEWS_TITLE_LEN + 8];
    ui_text_fit_to_width(item->title, r->title_font, row.w - index_w - 12 - 60, title, sizeof(title));
    rawdraw_draw_text(fb, width, height, title_x,
                      rawdraw_layout_ink_centered_text_top_y(r->title_font, title, center_y, 0), title, r->title_font,
                      text);

    const char *time = item->time_label[0] != '\0' ? item->time_label : item->source;
    char        fit_time[NEWS_TIME_LEN + 8];
    ui_text_fit_to_width(time, r->font, 54, fit_time, sizeof(fit_time));
    const int time_w = rawdraw_measure_text_width(fit_time, r->font);
    rawdraw_draw_text(fb, width, height, row.x + row.w - time_w - 6, text_y, fit_time, r->font, secondary);

    if (row.y + row.h < NEWS_PANEL_Y + NEWS_PANEL_H - 1) {
        for (int x = NEWS_PANEL_X + 8; x < NEWS_PANEL_X + NEWS_PANEL_W - 8; x += 4) {
            rawdraw_set_pixel(fb, width, height, x, row.y + row.h - 1, border);
        }
    }
}

static void news_draw_preview_modal(page_renderer_t *self, uint8_t *fb, int width, int height)
{
    news_page_t          *r         = (news_page_t *)self;
    const news_item_t    *item      = &r->items[r->selected_index];
    const rawdraw_color_t text      = rawdraw_theme_color_for(THEME_TOKEN_TEXT_PRIMARY);
    const rawdraw_color_t secondary = rawdraw_theme_color_for(THEME_TOKEN_TEXT_SECONDARY);
    const rawdraw_color_t accent    = rawdraw_theme_color_for(THEME_TOKEN_ACCENT);

    widget_modal_t modal;
    widget_modal_init(&modal);
    widget_modal_set_title(&modal, "新闻预览");
    widget_modal_set_footer(&modal, r->footer_focus == 1 ? "朗读" : "关闭");
    widget_modal_center_in_screen(&modal, width, height, 36);
    widget_modal_render(&modal, fb, width, height);

    const rawdraw_rect_t body         = widget_modal_get_content_bounds(&modal);
    const int            visible_rows = body.h / NEWS_ROW_H;

    /* Build all content lines: title (may wrap) / meta / summary. */
    char content_lines[NEWS_MAX_LINES][NEWS_LINE_LEN];
    int  line_count  = 0;
    int  title_lines = 0;

    char title_wrapped[2][NEWS_LINE_LEN];
    int  title_count = 0;
    ui_text_wrap_lines(r->title_font, item->title, body.w, title_wrapped, NEWS_LINE_LEN, 2, &title_count);
    for (int i = 0; i < title_count && line_count < NEWS_MAX_LINES; ++i) {
        strncpy(content_lines[line_count], title_wrapped[i], NEWS_LINE_LEN - 1);
        content_lines[line_count][NEWS_LINE_LEN - 1] = '\0';
        ++line_count;
    }
    title_lines = title_count;

    char meta[NEWS_LINE_LEN];
    meta[0] = '\0';
    if (item->source[0] != '\0') {
        strncpy(meta, item->source, sizeof(meta) - 1);
        meta[sizeof(meta) - 1] = '\0';
    }
    if (item->time_label[0] != '\0') {
        const size_t meta_len = strlen(meta);
        if (meta_len > 0) {
            snprintf(meta + meta_len, sizeof(meta) - meta_len, " · %s", item->time_label);
        } else {
            strncpy(meta, item->time_label, sizeof(meta) - 1);
            meta[sizeof(meta) - 1] = '\0';
        }
    }
    if (meta[0] != '\0' && line_count < NEWS_MAX_LINES) {
        char fit_meta[NEWS_LINE_LEN];
        ui_text_fit_to_width(meta, r->font, body.w, fit_meta, sizeof(fit_meta));
        strncpy(content_lines[line_count], fit_meta, NEWS_LINE_LEN - 1);
        content_lines[line_count][NEWS_LINE_LEN - 1] = '\0';
        ++line_count;
    }

    char summary_wrapped[NEWS_MAX_LINES][NEWS_LINE_LEN];
    int  summary_count = 0;
    ui_text_wrap_lines(r->font, item->summary, body.w, summary_wrapped, NEWS_LINE_LEN, NEWS_MAX_LINES - line_count,
                       &summary_count);
    for (int i = 0; i < summary_count && line_count < NEWS_MAX_LINES; ++i) {
        strncpy(content_lines[line_count], summary_wrapped[i], NEWS_LINE_LEN - 1);
        content_lines[line_count][NEWS_LINE_LEN - 1] = '\0';
        ++line_count;
    }

    const int total_lines = line_count;
    const int max_scroll  = RD_MAX(0, total_lines - visible_rows);
    if (r->preview_scroll > max_scroll)
        r->preview_scroll = max_scroll;

    int y = body.y;
    for (int i = 0; i < visible_rows && i + r->preview_scroll < total_lines; ++i) {
        const int        line_idx = i + r->preview_scroll;
        const lv_font_t *f        = (line_idx < title_lines) ? r->title_font : r->font;
        const int        center_y = y + NEWS_ROW_H / 2;
        rawdraw_draw_text(fb, width, height, body.x,
                          rawdraw_layout_ink_centered_text_top_y(f, content_lines[line_idx], center_y, 0),
                          content_lines[line_idx], f, f == r->title_font ? text : secondary);
        y += NEWS_ROW_H;
    }

    /* Scroll indicator: small triangle when content continues below/above. */
    if (r->preview_scroll < max_scroll) {
        rawdraw_draw_text(fb, width, height, body.x + body.w - 14, body.y + body.h - 10, "▼", r->font, accent);
    }
    if (r->preview_scroll > 0) {
        rawdraw_draw_text(fb, width, height, body.x + body.w - 14, body.y, "▲", r->font, accent);
    }
}

void news_page_render(page_renderer_t *self, uint8_t *fb, int width, int height)
{
    news_page_t *r = (news_page_t *)self;
    if (!fb)
        return;

    const rawdraw_paint_style_t bg_style    = rawdraw_theme_style(THEME_TOKEN_BACKGROUND_PRIMARY);
    const rawdraw_paint_style_t panel_style = rawdraw_theme_component(ROLE_PANEL);
    const rawdraw_color_t       text        = rawdraw_theme_color_for(THEME_TOKEN_TEXT_PRIMARY);
    const rawdraw_color_t       secondary   = rawdraw_theme_color_for(THEME_TOKEN_TEXT_SECONDARY);

    rawdraw_draw_styled_rect(fb, width, height,
                             (rawdraw_rect_t){0, STYLE_STATUS_BAR_HEIGHT, width, height - STYLE_STATUS_BAR_HEIGHT},
                             &bg_style);

    if (r->count == 0) {
        widget_modal_t modal;
        widget_modal_init(&modal);
        widget_modal_set_title(&modal, "暂无新闻");
        widget_modal_set_footer(&modal, "等待数据");
        widget_modal_center_in_screen(&modal, width, height, 52);
        widget_modal_render(&modal, fb, width, height);
    } else {
        rawdraw_draw_styled_round_rect(fb, width, height,
                                       (rawdraw_rect_t){NEWS_PANEL_X, NEWS_PANEL_Y, NEWS_PANEL_W, NEWS_PANEL_H},
                                       STYLE_BORDER_RADIUS_MD, &panel_style);
        int window_start = RD_MAX(0, r->selected_index - NEWS_VISIBLE_ROWS / 2);
        if (window_start + NEWS_VISIBLE_ROWS > r->count) {
            window_start = RD_MAX(0, r->count - NEWS_VISIBLE_ROWS);
        }
        for (int row = 0; row < NEWS_VISIBLE_ROWS; ++row) {
            const int item_index = window_start + row;
            if (item_index >= r->count)
                break;
            news_render_item(self, fb, width, height, NEWS_PANEL_Y + row * NEWS_ITEM_H + 1, item_index,
                             item_index == r->selected_index);
        }
    }

    if (r->preview_open && r->count > 0) {
        news_draw_preview_modal(self, fb, width, height);
    }

    rawdraw_draw_styled_round_rect(fb, width, height,
                                   (rawdraw_rect_t){NEWS_PANEL_X, NEWS_FOOTER_Y, NEWS_PANEL_W, NEWS_FOOTER_H},
                                   STYLE_BORDER_RADIUS_SM, &panel_style);
    if (r->preview_open) {
        const char *boot_hint = r->footer_focus == 1 ? "▶朗读" : "▶关闭";
        rawdraw_draw_text(
            fb, width, height, 54,
            rawdraw_layout_ink_centered_text_top_y(r->font, "UP/DN 选按钮", NEWS_FOOTER_Y + NEWS_FOOTER_H / 2, 0),
            "UP/DN 选按钮", r->font, secondary);
        rawdraw_draw_text(
            fb, width, height, 262,
            rawdraw_layout_ink_centered_text_top_y(r->font, boot_hint, NEWS_FOOTER_Y + NEWS_FOOTER_H / 2, 0), boot_hint,
            r->font, text);
    } else {
        rawdraw_draw_text(
            fb, width, height, 54,
            rawdraw_layout_ink_centered_text_top_y(r->font, "UP/DN 翻页", NEWS_FOOTER_Y + NEWS_FOOTER_H / 2, 0),
            "UP/DN 翻页", r->font, secondary);
        rawdraw_draw_text(
            fb, width, height, 262,
            rawdraw_layout_ink_centered_text_top_y(r->font, "BOOT 打开", NEWS_FOOTER_Y + NEWS_FOOTER_H / 2, 0),
            "BOOT 打开", r->font, text);
    }

    r->base.needs_full_refresh_flag = false;
}
static int news_preview_get_max_scroll(news_page_t *r)
{
    if (r->count == 0 || r->selected_index < 0 || r->selected_index >= r->count)
        return 0;

    int width = r->base.width;
    int height = r->base.height;

    widget_modal_t modal;
    widget_modal_init(&modal);
    widget_modal_set_title(&modal, "新闻预览");
    widget_modal_set_footer(&modal, r->footer_focus == 1 ? "朗读" : "关闭");
    widget_modal_center_in_screen(&modal, width, height, 36);

    const rawdraw_rect_t body         = widget_modal_get_content_bounds(&modal);
    const int            visible_rows = body.h / NEWS_ROW_H;

    const news_item_t    *item      = &r->items[r->selected_index];
    int  line_count  = 0;

    char title_wrapped[2][NEWS_LINE_LEN];
    int  title_count = 0;
    ui_text_wrap_lines(r->title_font, item->title, body.w, title_wrapped, NEWS_LINE_LEN, 2, &title_count);
    line_count += title_count;

    char meta[NEWS_LINE_LEN];
    meta[0] = '\0';
    if (item->source[0] != '\0') {
        strncpy(meta, item->source, sizeof(meta) - 1);
        meta[sizeof(meta) - 1] = '\0';
    }
    if (item->time_label[0] != '\0') {
        const size_t meta_len = strlen(meta);
        if (meta_len > 0) {
            snprintf(meta + meta_len, sizeof(meta) - meta_len, " · %s", item->time_label);
        } else {
            strncpy(meta, item->time_label, sizeof(meta) - 1);
            meta[sizeof(meta) - 1] = '\0';
        }
    }
    if (meta[0] != '\0' && line_count < NEWS_MAX_LINES) {
        line_count++;
    }

    char summary_wrapped[NEWS_MAX_LINES][NEWS_LINE_LEN];
    int  summary_count = 0;
    ui_text_wrap_lines(r->font, item->summary, body.w, summary_wrapped, NEWS_LINE_LEN, NEWS_MAX_LINES - line_count,
                       &summary_count);
    line_count += summary_count;

    if (line_count > NEWS_MAX_LINES)
        line_count = NEWS_MAX_LINES;

    return RD_MAX(0, line_count - visible_rows);
}

bool news_page_handle_input(page_renderer_t *self, const ui_button_event_t *event)
{
    news_page_t *r = (news_page_t *)self;
    if (r->preview_open) {
        switch (event->type) {
        case BTN_UP_CLICK:
            if (r->preview_scroll > 0) {
                r->preview_scroll--;
                r->base.needs_full_refresh_flag = true;
                return true;
            }
            r->footer_focus                 = (r->footer_focus == 0) ? 1 : 0;
            r->base.needs_full_refresh_flag = true;
            return true;
        case BTN_DOWN_CLICK: {
            int max_scroll = news_preview_get_max_scroll(r);
            if (r->preview_scroll < max_scroll) {
                r->preview_scroll++;
                r->base.needs_full_refresh_flag = true;
                return true;
            }
            r->footer_focus                 = (r->footer_focus == 0) ? 1 : 0;
            r->base.needs_full_refresh_flag = true;
            return true;
        }
        case BTN_BOOT_CLICK:
            if (r->footer_focus == 1 && r->tts_request_cb) {
                const news_item_t *item = &r->items[r->selected_index];
                char               tts_text[NEWS_TITLE_LEN + NEWS_SUMMARY_LEN + 2];
                if (item->summary[0] != '\0') {
                    snprintf(tts_text, sizeof(tts_text), "%s %s", item->title, item->summary);
                } else {
                    snprintf(tts_text, sizeof(tts_text), "%s", item->title);
                }
                r->tts_request_cb(tts_text, r->tts_ctx);
                /* Keep the modal open after requesting speech; closing here
                     * would make BOOT feel like "close" even when DN selected read. */
                r->footer_focus                 = 1;
                r->base.needs_full_refresh_flag = true;
                return true;
            }
            /* footer_focus == 0 → close */
            r->preview_open                 = false;
            r->footer_focus                 = 0;
            r->preview_scroll               = 0;
            r->base.needs_full_refresh_flag = true;
            return true;
        default:
            return true;
        }
    }

    switch (event->type) {
    case BTN_UP_CLICK:
        if (r->selected_index > 0) {
            r->selected_index--;
            if (r->selected_index * (NEWS_ITEM_H + NEWS_ITEM_GAP) < r->scroll_offset) {
                r->scroll_offset = r->selected_index * (NEWS_ITEM_H + NEWS_ITEM_GAP);
            }
            r->base.needs_full_refresh_flag = true;
            return true;
        }
        break;
    case BTN_DOWN_CLICK:
        if (r->selected_index < r->count - 1) {
            r->selected_index++;
            const int content_h   = NEWS_PANEL_H;
            const int item_bottom = r->selected_index * (NEWS_ITEM_H + NEWS_ITEM_GAP) + NEWS_ITEM_H;
            if (item_bottom > r->scroll_offset + content_h) {
                r->scroll_offset = item_bottom - content_h;
            }
            r->base.needs_full_refresh_flag = true;
            return true;
        }
        break;
    case BTN_BOOT_CLICK:
        if (r->count > 0) {
            r->preview_open                 = true;
            r->preview_scroll               = 0;
            r->base.needs_full_refresh_flag = true;
            return true;
        }
        break;
    default:
        break;
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* Data interface                                                      */
/* ------------------------------------------------------------------ */

static bool news_same_items(const news_page_t *r, const news_item_t *items, int count)
{
    if (r->count != count)
        return false;
    for (int i = 0; i < count; ++i) {
        if (strcmp(r->items[i].title, items[i].title) != 0 || strcmp(r->items[i].summary, items[i].summary) != 0 ||
            strcmp(r->items[i].source, items[i].source) != 0 ||
            strcmp(r->items[i].time_label, items[i].time_label) != 0) {
            return false;
        }
    }
    return true;
}

static void news_clamp_selection(news_page_t *r)
{
    if (r->count == 0) {
        r->selected_index = 0;
    } else {
        r->selected_index = RD_MAX(0, RD_MIN(r->selected_index, r->count - 1));
    }
}

static void news_clamp_scroll_offset(news_page_t *r)
{
    const int content_h  = NEWS_PANEL_H;
    int       max_offset = r->count * (NEWS_ITEM_H + NEWS_ITEM_GAP) - NEWS_ITEM_GAP - content_h;
    if (max_offset < 0)
        max_offset = 0;
    r->scroll_offset = RD_MAX(0, RD_MIN(r->scroll_offset, max_offset));
}

void news_page_set_items(page_renderer_t *self, const news_item_t *items, int count)
{
    news_page_t *r = (news_page_t *)self;
    if (count > NEWS_MAX_ITEMS)
        count = NEWS_MAX_ITEMS;
    if (count < 0)
        count = 0;

    const bool same_items         = news_same_items(r, items, count);
    const int  old_selected       = r->selected_index;
    const int  old_scroll         = r->scroll_offset;
    const bool old_preview_open   = r->preview_open;
    const int  old_footer_focus   = r->footer_focus;
    const int  old_preview_scroll = r->preview_scroll;

    r->count = count;
    for (int i = 0; i < count; ++i) {
        r->items[i] = items[i];
    }

    if (same_items) {
        r->selected_index = old_selected;
        r->scroll_offset  = old_scroll;
        r->preview_open   = old_preview_open;
        r->footer_focus   = old_footer_focus;
        r->preview_scroll = old_preview_scroll;
    } else {
        r->selected_index = 0;
        r->scroll_offset  = 0;
        r->preview_open   = false;
        r->footer_focus   = 0;
        r->preview_scroll = 0;
    }
    news_clamp_selection(r);
    news_clamp_scroll_offset(r);
    r->base.needs_full_refresh_flag = true;
}

void news_page_add_item(page_renderer_t *self, const news_item_t *item)
{
    news_page_t *r = (news_page_t *)self;
    if (!item || r->count >= NEWS_MAX_ITEMS)
        return;
    r->items[r->count] = *item;
    ++r->count;
    news_clamp_selection(r);
    news_clamp_scroll_offset(r);
    r->base.needs_full_refresh_flag = true;
}

void news_page_clear(page_renderer_t *self)
{
    news_page_t *r                  = (news_page_t *)self;
    r->count                        = 0;
    r->selected_index               = 0;
    r->scroll_offset                = 0;
    r->preview_open                 = false;
    r->footer_focus                 = 0;
    r->preview_scroll               = 0;
    r->base.needs_full_refresh_flag = true;
}

void news_page_set_tts_request_callback(page_renderer_t *self, void (*cb)(const char *text, void *ctx), void *ctx)
{
    news_page_t *r    = (news_page_t *)self;
    r->tts_request_cb = cb;
    r->tts_ctx        = ctx;
}

/* ------------------------------------------------------------------ */
/* vtable instance                                                     */
/* ------------------------------------------------------------------ */

EXT_RAM_BSS_ATTR news_page_t s_news_instance;

const page_renderer_ops_t news_page_ops = {
    .init                    = news_page_init,
    .enter                   = news_page_enter,
    .render                  = news_page_render,
    .handle_input            = news_page_handle_input,
    .get_dirty_rect          = NULL,
    .needs_full_refresh      = NULL,
    .mark_full_refresh       = NULL,
    .clear_full_refresh_flag = NULL,
    .append_text             = NULL,
    .begin_stream            = NULL,
    .end_stream              = NULL,
};

PAGE_REGISTER(UI_PAGE_NEWS, "热点", FA_SETTINGS_NEWSPAPER, true, 90, &news_page_ops,
              &s_news_instance.base);
