/**
 * @file log_renderer.c
 * @brief Log page renderer — C port of C++ rawdraw::LogRenderer.
 *
 * Shows boot events, connection status, memory stats, and recent
 * activity log entries. Scrollable list with UP/DOWN navigation.
 */
#include "log_page.h"
#include "page_registry.h"

#include "rawdraw_ext.h"
#include "theme.h"
#include "style.h"
#include "layout.h"

#include <stdio.h>
#include <string.h>

#ifdef ESP_PLATFORM
#    include "esp_heap_caps.h"
#endif

#ifndef PROJECT_VER
#    define PROJECT_VER "3.8.0"
#endif

static const lv_font_t *const kLogFont = &SourceHanSansSC_Regular_slim;
static const lv_font_t *const kLogTitleFont = &SourceHanSansSC_Medium_slim;
static const lv_font_t *const kLogIconFont = &font_zectrix_16_1;

/* ------------------------------------------------------------------ */
/* Internal helpers                                                    */
/* ------------------------------------------------------------------ */

static void add_log_entry(log_page_t *r, const char *tag, const char *message)
{
    log_page_entry_t *entry = &r->entries[r->head];
    entry->time = time(NULL);
    strncpy(entry->tag, tag, sizeof(entry->tag) - 1);
    entry->tag[sizeof(entry->tag) - 1] = '\0';
    strncpy(entry->message, message, sizeof(entry->message) - 1);
    entry->message[sizeof(entry->message) - 1] = '\0';

    if (r->count < LOG_PAGE_MAX_ENTRIES) {
        r->count++;
    }
    r->head = (r->head + 1) % LOG_PAGE_MAX_ENTRIES;
}

static void collect_log_entries(log_page_t *r)
{
    r->count = 0;
    r->head = 0;

    /* Boot event */
    add_log_entry(r, "BOOT", "系统启动");

    /* Memory stats */
    size_t free_heap = 0;
    size_t free_psram = 0;
#ifdef ESP_PLATFORM
    free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
#endif
    char mem_buf[64];
    snprintf(mem_buf, sizeof(mem_buf), "可用内存: %zu KB", free_heap / 1024);
    add_log_entry(r, "MEM", mem_buf);

    if (free_psram > 0) {
        snprintf(mem_buf, sizeof(mem_buf), "PSRAM: %zu KB", free_psram / 1024);
        add_log_entry(r, "PSRAM", mem_buf);
    }

    /* Chip info */
    add_log_entry(r, "CHIP", "芯片: ESP32-S3");

    /* Firmware version */
    add_log_entry(r, "FW", "v" PROJECT_VER);

    /* RTC status */
    add_log_entry(r, "RTC", "RTC 已初始化");

    /* WiFi / LAN placeholders (could be wired to real state) */
    add_log_entry(r, "WIFI", "等待连接...");
    add_log_entry(r, "LAN", "等待服务器...");
}

static void clamp_scroll_offset(log_page_t *r)
{
    if (r->count == 0) {
        r->scroll_offset = 0;
        return;
    }
    const int content_h = r->base.height - STYLE_STATUS_BAR_HEIGHT - STYLE_SPACING_XXS;
    const int line_h = r->font->line_height + STYLE_SPACING_XS;
    const int visible = content_h / line_h;
    int max_offset = r->count - visible;
    if (max_offset < 0)
        max_offset = 0;
    r->scroll_offset = RD_MAX(0, RD_MIN(r->scroll_offset, max_offset));
}

static void draw_title_bar(log_page_t *r, uint8_t *fb, int width, int height)
{
    const rawdraw_paint_style_t bar_style = rawdraw_theme_style(THEME_TOKEN_BACKGROUND_SECONDARY);
    const rawdraw_color_t text = rawdraw_theme_color_for(THEME_TOKEN_TEXT_PRIMARY);
    const rawdraw_color_t secondary = rawdraw_theme_color_for(THEME_TOKEN_TEXT_SECONDARY);
    const rawdraw_color_t border = rawdraw_theme_color_for(THEME_TOKEN_BORDER);
    const int title_y_start = STYLE_STATUS_BAR_HEIGHT;
    const int title_bar_h = LOG_PAGE_TITLE_BAR_H;

    /* Clear title bar area (separate from status bar above) */
    rawdraw_draw_styled_rect(fb, width, height, (rawdraw_rect_t){0, title_y_start, width, title_bar_h}, &bar_style);

    /* Bottom separator (2px) */
    const int line_y = title_y_start + title_bar_h - 2;
    rawdraw_draw_hline(fb, width, height, line_y, 0, width, border);
    rawdraw_draw_hline(fb, width, height, line_y + 1, 0, width, border);

    /* Use ink-centered layout so CJK text sits optically centered */
    const int title_text_y =
        rawdraw_layout_ink_centered_text_top_y_in_box(r->font, "日志", title_y_start, title_bar_h, 1);
    rawdraw_draw_text(fb, width, height, STYLE_SPACING_LG, title_text_y, "日志", r->font, text);

    /* Entry count (right-aligned) */
    if (r->count > 0) {
        char count_buf[16];
        snprintf(count_buf, sizeof(count_buf), "%d条", r->count);
        const int count_w = rawdraw_measure_text_width(count_buf, r->font);
        const int count_x = width - count_w - STYLE_SPACING_LG;
        rawdraw_draw_text(fb, width, height, count_x, title_text_y, count_buf, r->font, secondary);
    }
}

/* ------------------------------------------------------------------ */
/* PageRenderer vtable                                                 */
/* ------------------------------------------------------------------ */

void log_page_init(page_renderer_t *self, int width, int height)
{
    log_page_t *r = (log_page_t *)self;
    r->base.width = width;
    r->base.height = height;
    r->base.needs_full_refresh_flag = true;
    r->selected_index = 0;
    r->scroll_offset = 0;
    r->font = kLogFont;
    r->title_font = kLogTitleFont;
    r->icon_font = kLogIconFont;
    r->count = 0;
    r->head = 0;
    collect_log_entries(r);
    clamp_scroll_offset(r);
}

void log_page_render(page_renderer_t *self, uint8_t *fb, int width, int height)
{
    log_page_t *r = (log_page_t *)self;
    if (!fb)
        return;

    /* === Title bar === */
    draw_title_bar(r, fb, width, height);

    /* === Content area === */
    const int content_top = STYLE_STATUS_BAR_HEIGHT + LOG_PAGE_TITLE_BAR_H + STYLE_SPACING_XS;
    const int content_bottom = height - STYLE_SPACING_SM;
    const int content_height = content_bottom - content_top;
    const int content_left = STYLE_SPACING_MD;
    const int content_right = width - STYLE_SPACING_MD;

    /* Collect fresh log entries */
    collect_log_entries(r);

    if (r->count == 0) {
        const char *empty_text = "暂无日志";
        const int text_w = rawdraw_measure_text_width(empty_text, r->font);
        const int text_x = (width - text_w) / 2;
        const int text_y = content_top + (content_height / 2);
        rawdraw_draw_text(fb, width, height, text_x, text_y, empty_text, r->font,
                          rawdraw_theme_color_for(THEME_TOKEN_TEXT_SECONDARY));
        r->base.needs_full_refresh_flag = false;
        return;
    }

    const int line_h = r->font->line_height + STYLE_SPACING_XS;
    const int tag_w = rawdraw_measure_text_width("WWWWW", r->font) + STYLE_SPACING_SM;
    const int visible_items = content_height / line_h;

    const rawdraw_color_t text = rawdraw_theme_color_for(THEME_TOKEN_TEXT_PRIMARY);
    const rawdraw_paint_style_t selected_style = rawdraw_theme_component(ROLE_SETTINGS_SELECTED);

    int y = content_top;
    for (int i = r->scroll_offset; i < r->count; i++) {
        if (y + line_h > content_bottom)
            break;

        const log_page_entry_t *entry = &r->entries[i];
        const bool selected = (i == r->selected_index);

        /* Selected: inverted background */
        if (selected) {
            rawdraw_draw_styled_rect(fb, width, height,
                                     (rawdraw_rect_t){content_left, y, content_right - content_left, line_h},
                                     &selected_style);
        }

        /* Tag (left-aligned, monospace style) */
        const rawdraw_color_t fg = selected ? selected_style.fg : text;
        rawdraw_draw_text(fb, width, height, content_left, y, entry->tag, r->font, fg);

        /* Message (right of tag) */
        rawdraw_draw_text(fb, width, height, content_left + tag_w, y, entry->message, r->font, fg);

        y += line_h;
    }

    /* === Scroll indicator === */
    if (r->count > visible_items) {
        const int bar_w = STYLE_SCROLLBAR_WIDTH;
        const int bar_x = width - bar_w - STYLE_SCROLL_MARGIN;
        int bar_h = (content_height * visible_items) / r->count;
        if (bar_h < STYLE_SCROLLBAR_MIN_H)
            bar_h = STYLE_SCROLLBAR_MIN_H;
        const int bar_offset = (r->scroll_offset * content_height) / r->count;
        int bar_y = content_top + bar_offset;
        if (bar_y + bar_h > content_bottom)
            bar_h = content_bottom - bar_y;

        const rawdraw_color_t thumb = rawdraw_theme_color_for(THEME_TOKEN_SELECTED);
        const rawdraw_paint_style_t thumb_style =
            rawdraw_make_paint(thumb, thumb, thumb, DITHER_NONE, 0, REFRESH_STATIC_SAFE);
        rawdraw_draw_styled_round_rect(fb, width, height, (rawdraw_rect_t){bar_x, bar_y, bar_w, bar_h},
                                       STYLE_BORDER_RADIUS_SM, &thumb_style);
    }

    r->base.needs_full_refresh_flag = false;
}

bool log_page_handle_input(page_renderer_t *self, const ui_button_event_t *event)
{
    log_page_t *r = (log_page_t *)self;
    if (r->count == 0)
        return false;

    switch (event->type) {
    case BTN_UP_CLICK:
        if (r->selected_index > 0) {
            r->selected_index--;
            if (r->selected_index < r->scroll_offset) {
                r->scroll_offset = r->selected_index;
            }
            r->base.needs_full_refresh_flag = true;
            return true;
        }
        break;

    case BTN_DOWN_CLICK:
        if (r->selected_index < r->count - 1) {
            r->selected_index++;
            const int content_h = r->base.height - STYLE_STATUS_BAR_HEIGHT - STYLE_SPACING_XXS;
            const int line_h = r->font->line_height + STYLE_SPACING_XS;
            int visible = content_h / line_h;
            if (visible < 1)
                visible = 1;
            int max_offset = r->count - visible;
            if (max_offset < 0)
                max_offset = 0;
            if (r->selected_index >= r->scroll_offset + visible) {
                r->scroll_offset = r->selected_index - visible + 1;
            }
            r->base.needs_full_refresh_flag = true;
            return true;
        }
        break;

    case BTN_BOOT_LONG_PRESS:
        /* Refresh log data */
        collect_log_entries(r);
        r->selected_index = 0;
        r->scroll_offset = 0;
        r->base.needs_full_refresh_flag = true;
        return true;

    default:
        break;
    }

    return false;
}

/* ------------------------------------------------------------------ */
/* Data interface                                                      */
/* ------------------------------------------------------------------ */

void log_page_refresh(page_renderer_t *self)
{
    log_page_t *r = (log_page_t *)self;
    collect_log_entries(r);
    r->selected_index = 0;
    r->scroll_offset = 0;
    r->base.needs_full_refresh_flag = true;
}

/* ------------------------------------------------------------------ */
/* vtable instance                                                     */
/* ------------------------------------------------------------------ */

EXT_RAM_BSS_ATTR log_page_t s_log_instance;

const page_renderer_ops_t log_page_ops = {
    .init = log_page_init,
    .render = log_page_render,
    .handle_input = log_page_handle_input,
    .get_dirty_rect = NULL,
    .needs_full_refresh = NULL,
    .mark_full_refresh = NULL,
    .clear_full_refresh_flag = NULL,
    .append_text = NULL,
    .begin_stream = NULL,
    .end_stream = NULL,
};

PAGE_REGISTER(UI_PAGE_LOG, "日志", NULL, true, 130, &log_page_ops, &s_log_instance.base);
