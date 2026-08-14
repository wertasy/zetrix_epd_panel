/**
 * @file ebook_renderer.c
 * @brief Ebook list page and TXT reader renderer — C port of C++
 *        rawdraw::EbookRenderer.
 *
 * Two modes:
 * 1. File list: shows TXT files from SPIFFS, BOOT click selects
 * 2. Reader: paginated TXT display, BOOT click returns to file list
 */
#include "ebook_page.h"
#include "page_registry.h"
#include "fa_settings.h"

#include "rawdraw_ext.h"
#include "theme.h"
#include "style.h"
#include "layout.h"
#include "ui_text_util.h"
#include "settings.h"
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EBOOK_TAG "EbookRenderer"

#define EBOOK_CONTENT_TOP_GAP 8
#define EBOOK_LIST_Y (STYLE_STATUS_BAR_HEIGHT + EBOOK_CONTENT_TOP_GAP)
#define EBOOK_LIST_H 220
#define EBOOK_ITEM_H 32
#define EBOOK_FOOTER_Y 272
#define EBOOK_FOOTER_H 24

/* Reader page wrap storage. */
#define EBOOK_MAX_DISPLAY_LINES 16

static const lv_font_t *const ebook_font = &SourceHanSansSC_Regular_slim;
static const lv_font_t *const ebook_title_font = &SourceHanSansSC_Medium_slim;

static int ebook_chars_per_page(const ebook_page_t *r)
{
    return r->portrait_reader ? EBOOK_PORTRAIT_CHARS_PER_PAGE : EBOOK_LANDSCAPE_CHARS_PER_PAGE;
}

static void ebook_calc_pages(ebook_page_t *r)
{
    const int content_len = (int)strlen(r->reader_content);
    if (content_len == 0) {
        r->total_pages = 1;
    } else {
        const int chars_per_page = ebook_chars_per_page(r);
        r->total_pages = (content_len + chars_per_page - 1) / chars_per_page;
    }
}

static void ebook_set_portrait_reader(ebook_page_t *r, bool portrait);

/* ------------------------------------------------------------------ */
/* PageRenderer vtable                                                 */
/* ------------------------------------------------------------------ */

/* P1: Persist reader position to NVS for deep-sleep recovery. */
static void ebook_save_reader_position(ebook_page_t *r)
{
    if (!r->reader_filename[0])
        return;
    settings_handle_t h = settings_open("ebook", true);
    if (h) {
        settings_set_string(h, "reader_file", r->reader_filename);
        settings_set_int(h, "reader_page", (int32_t)r->current_page);
        settings_close(h);
    }
}

void ebook_page_init(page_renderer_t *self, int width, int height)
{
    ebook_page_t *r = (ebook_page_t *)self;
    r->base.width = width;
    r->base.height = height;
    r->file_count = 0;
    r->selected_index = 0;
    r->reader_mode = false;
    r->portrait_reader = false;
    r->reader_filename[0] = '\0';
    r->reader_content[0] = '\0';
    r->current_page = 0;
    r->total_pages = 0;
    r->font = ebook_font;
    r->title_font = ebook_title_font;
    r->base.needs_full_refresh_flag = true;

    /* P1: Restore reader position from NVS on wake. */
    settings_handle_t h = settings_open("ebook", false);
    if (h) {
        char fname[128];
        settings_get_string(h, "reader_file", fname, sizeof(fname), "");
        int32_t pg = settings_get_int(h, "reader_page", 0);
        settings_close(h);
        if (fname[0] != '\0') {
            strncpy(r->reader_filename, fname, sizeof(r->reader_filename) - 1);
            r->reader_filename[sizeof(r->reader_filename) - 1] = '\0';
            r->current_page = RD_MAX(0, pg);
        }
    }
}

/* Page gained focus: request a redraw but keep reading position. */
static void ebook_page_enter(page_renderer_t *self)
{
    ebook_page_t *r = (ebook_page_t *)self;
    if (!r)
        return;
    r->base.needs_full_refresh_flag = true;
}

static void ebook_render_file_list(page_renderer_t *self, uint8_t *fb, int width, int height)
{
    ebook_page_t *r = (ebook_page_t *)self;
    const rawdraw_paint_style_t bg_style = rawdraw_theme_style(THEME_TOKEN_BACKGROUND_PRIMARY);
    const rawdraw_paint_style_t selected_style = rawdraw_theme_component(ROLE_SETTINGS_SELECTED);
    const rawdraw_paint_style_t footer_style = rawdraw_theme_component(ROLE_PANEL);
    const rawdraw_color_t text = rawdraw_theme_color_for(THEME_TOKEN_TEXT_PRIMARY);
    const rawdraw_color_t secondary = rawdraw_theme_color_for(THEME_TOKEN_TEXT_SECONDARY);

    /* Clear content area. */
    rawdraw_draw_styled_rect(
        fb, width, height,
        (rawdraw_rect_t){0, STYLE_STATUS_BAR_HEIGHT + 1, width, height - STYLE_STATUS_BAR_HEIGHT - 1}, &bg_style);

    if (r->file_count == 0) {
        const char *hint = "暂无TXT文件";
        const int hint_w = rawdraw_measure_text_width(hint, r->font);
        rawdraw_draw_text(fb, width, height, (width - hint_w) / 2, EBOOK_LIST_Y + 80, hint, r->font, text);
        rawdraw_draw_text(fb, width, height, (width - rawdraw_measure_text_width("推送TXT到设备", r->font)) / 2,
                          EBOOK_LIST_Y + 110, "推送TXT到设备", r->font, secondary);
    } else {
        const int visible_start = RD_MAX(0, r->selected_index - 5);
        for (int i = 0; i < 7; ++i) {
            const int idx = visible_start + i;
            if (idx >= r->file_count)
                break;
            const int y = EBOOK_LIST_Y + i * EBOOK_ITEM_H;
            const bool sel = idx == r->selected_index;

            if (sel) {
                rawdraw_draw_styled_round_rect(fb, width, height,
                                               (rawdraw_rect_t){14, y + 2, width - 28, EBOOK_ITEM_H - 4},
                                               STYLE_BORDER_RADIUS_SM, &selected_style);
                rawdraw_draw_text(
                    fb, width, height, 24,
                    rawdraw_layout_ink_centered_text_top_y(r->font, r->files[idx], y + EBOOK_ITEM_H / 2, 0),
                    r->files[idx], r->font, selected_style.fg);
            } else {
                rawdraw_draw_text(
                    fb, width, height, 24,
                    rawdraw_layout_ink_centered_text_top_y(r->font, r->files[idx], y + EBOOK_ITEM_H / 2, 0),
                    r->files[idx], r->font, text);
            }
        }
    }

    /* Footer hints. */
    rawdraw_draw_styled_round_rect(fb, width, height, (rawdraw_rect_t){14, EBOOK_FOOTER_Y, 110, EBOOK_FOOTER_H},
                                   STYLE_BORDER_RADIUS_SM, &footer_style);
    rawdraw_draw_text(
        fb, width, height, 34,
        rawdraw_layout_ink_centered_text_top_y(r->font, "BOOT 选择", EBOOK_FOOTER_Y + EBOOK_FOOTER_H / 2, 0),
        "BOOT 选择", r->font, footer_style.fg);

    rawdraw_draw_styled_round_rect(fb, width, height, (rawdraw_rect_t){142, EBOOK_FOOTER_Y, 130, EBOOK_FOOTER_H},
                                   STYLE_BORDER_RADIUS_SM, &footer_style);
    rawdraw_draw_text(
        fb, width, height, 160,
        rawdraw_layout_ink_centered_text_top_y(r->font, "双击返回", EBOOK_FOOTER_Y + EBOOK_FOOTER_H / 2, 0), "双击返回",
        r->font, footer_style.fg);
}

static void ebook_render_reader_page(page_renderer_t *self, uint8_t *fb, int width, int height, int content_y,
                                     int content_h)
{
    ebook_page_t *r = (ebook_page_t *)self;
    const rawdraw_color_t text = rawdraw_theme_color_for(THEME_TOKEN_TEXT_PRIMARY);
    const rawdraw_color_t secondary = rawdraw_theme_color_for(THEME_TOKEN_TEXT_SECONDARY);

    if (r->reader_content[0] == '\0') {
        const char *empty_hint = "文件为空或读取失败";
        const int hint_w = rawdraw_measure_text_width(empty_hint, r->font);
        rawdraw_draw_text(fb, width, height, (width - hint_w) / 2,
                          rawdraw_layout_ink_centered_text_top_y(r->font, empty_hint, content_y + 42, 0), empty_hint,
                          r->font, text);
        rawdraw_draw_text(fb, width, height, 24,
                          rawdraw_layout_ink_centered_text_top_y(r->font, "请重新推送 TXT 后再打开", content_y + 74, 0),
                          "请重新推送 TXT 后再打开", r->font, secondary);
        return;
    }

    const int chars_per_page = ebook_chars_per_page(r);
    const int content_len = (int)strlen(r->reader_content);
    int start_char = r->current_page * chars_per_page;
    if (start_char >= content_len) {
        start_char = content_len > 0 ? content_len - 1 : 0;
    }
    /* Never start a page mid-codepoint. */
    while (start_char < content_len && ((uint8_t)r->reader_content[start_char] & 0xC0) == 0x80) {
        ++start_char;
    }
    const int end_char = RD_MIN(start_char + chars_per_page, content_len);

    ESP_LOGI(EBOOK_TAG, "RenderReader file=%s portrait=%d bytes=%d page=%d/%d page_bytes=%d", r->reader_filename,
             r->portrait_reader ? 1 : 0, content_len, r->current_page + 1, r->total_pages, end_char - start_char);

    /* Wrap text into display lines first (handles \n and word-wrap).
     * Extract the page slice into a NUL-terminated buffer, then delegate
     * to the shared ui_text_wrap_lines helper. */
    const int margin_x = 14;
    const int max_line_width = width - margin_x * 2;

    char display_lines[EBOOK_MAX_DISPLAY_LINES][128];
    int line_count = 0;

    const int slice_len = end_char - start_char;
    char page_slice[EBOOK_PORTRAIT_CHARS_PER_PAGE + 1];
    const int copy_len = RD_MIN(slice_len, (int)sizeof(page_slice) - 1);
    memcpy(page_slice, r->reader_content + start_char, (size_t)copy_len);
    page_slice[copy_len] = '\0';

    ui_text_wrap_lines(r->font, page_slice, max_line_width, display_lines, 128, EBOOK_MAX_DISPLAY_LINES, &line_count);
    if (line_count == 0) {
        strcpy(display_lines[0], " ");
        line_count = 1;
    }

    /* Same lesson as Chat/Settings: do not use font->line_height as the
     * visible text height. The SourceHan line box is taller than the ink,
     * and treating its top as DrawText y makes multi-line TXT pages look
     * overlapped or glued to the previous row on the 1bpp panel. */
    int max_ink_h = 0;
    for (int i = 0; i < line_count; ++i) {
        const rawdraw_text_ink_bounds_t ink = rawdraw_layout_measure_text_ink_bounds(r->font, display_lines[i]);
        const int h = ink.valid ? ink.height : (int)r->font->line_height;
        if (h > max_ink_h)
            max_ink_h = h;
    }
    const int line_box_h = RD_MAX(max_ink_h + 6, 22);
    const int line_gap = 3;
    const int line_step = line_box_h + line_gap;
    const int max_lines = content_h / line_step;
    const int visible = RD_MIN(line_count, max_lines);

    for (int i = 0; i < visible; ++i) {
        const int line_box_y = content_y + i * line_step;
        rawdraw_draw_text(
            fb, width, height, margin_x,
            rawdraw_layout_ink_centered_text_top_y_in_box(r->font, display_lines[i], line_box_y, line_box_h, 0),
            display_lines[i], r->font, text);
    }
}

static void ebook_render_reader_portrait(page_renderer_t *self, uint8_t *fb, int width, int height)
{
    ebook_page_t *r = (ebook_page_t *)self;
    const int portrait_w = STYLE_SCREEN_HEIGHT; /* portrait = rotated screen */
    const int portrait_h = STYLE_SCREEN_WIDTH;
    const size_t portrait_bytes = ((size_t)portrait_w * 2 + 7) / 8 * (size_t)portrait_h;
    uint8_t *portrait = (uint8_t *)heap_caps_malloc(portrait_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!portrait)
        portrait = (uint8_t *)malloc(portrait_bytes);
    if (!portrait)
        return;
    memset(portrait, 0x55, portrait_bytes);

    const rawdraw_paint_style_t bg_style = rawdraw_theme_style(THEME_TOKEN_BACKGROUND_PRIMARY);
    rawdraw_draw_styled_rect(portrait, portrait_w, portrait_h, (rawdraw_rect_t){0, 0, portrait_w, portrait_h},
                             &bg_style);
    ebook_render_reader_page(self, portrait, portrait_w, portrait_h, 12, portrait_h - 24);

    const rawdraw_color_t secondary = rawdraw_theme_color_for(THEME_TOKEN_TEXT_SECONDARY);
    char page_buf[24];
    snprintf(page_buf, sizeof(page_buf), "%d/%d", r->current_page + 1, r->total_pages);
    const int page_w = rawdraw_measure_text_width(page_buf, r->font);
    rawdraw_draw_text(portrait, portrait_w, portrait_h, portrait_w - page_w - 10, portrait_h - 18, page_buf, r->font,
                      secondary);

    /* Rotate the portrait page into the landscape screen (batched 2bpp blit). */
    rawdraw_draw_styled_rect(fb, width, height, (rawdraw_rect_t){0, 0, width, height}, &bg_style);
    rawdraw_blit_rotated_90(portrait, portrait_w, portrait_h, fb, width, height, 0, 0);
    free(portrait);
}

static void ebook_render_reader(page_renderer_t *self, uint8_t *fb, int width, int height)
{
    ebook_page_t *r = (ebook_page_t *)self;
    if (r->portrait_reader) {
        ebook_render_reader_portrait(self, fb, width, height);
        return;
    }

    const rawdraw_paint_style_t bg_style = rawdraw_theme_style(THEME_TOKEN_BACKGROUND_PRIMARY);
    rawdraw_draw_styled_rect(
        fb, width, height,
        (rawdraw_rect_t){0, STYLE_STATUS_BAR_HEIGHT + 1, width, height - STYLE_STATUS_BAR_HEIGHT - 1}, &bg_style);

    /* Content area (no title bar — filename+page shown in status bar).
     * Start below the 28px status/menu bar with the same top gap as the list. */
    const int content_y = STYLE_STATUS_BAR_HEIGHT + EBOOK_CONTENT_TOP_GAP;
    const int content_h = height - content_y - 4;
    ebook_render_reader_page(self, fb, width, height, content_y, content_h);
}

void ebook_page_render(page_renderer_t *self, uint8_t *fb, int width, int height)
{
    ebook_page_t *r = (ebook_page_t *)self;
    if (!fb)
        return;
    if (r->reader_mode) {
        ebook_render_reader(self, fb, width, height);
    } else {
        ebook_render_file_list(self, fb, width, height);
    }
    r->base.needs_full_refresh_flag = false;
}

bool ebook_page_handle_input(page_renderer_t *self, const ui_button_event_t *event)
{
    ebook_page_t *r = (ebook_page_t *)self;
    if (r->reader_mode) {
        switch (event->type) {
        case BTN_BOOT_CLICK:
            /* Exit reader, return to file list. */
            ebook_page_close_reader(self);
            return true;
        case BTN_BOOT_DOUBLE_CLICK:
            /* Signal to app to exit ebook page entirely. */
            return false;
        case BTN_UP_DOUBLE_CLICK:
            ebook_set_portrait_reader(r, false);
            return true;
        case BTN_DOWN_DOUBLE_CLICK:
            ebook_set_portrait_reader(r, true);
            return true;
        case BTN_UP_CLICK:
            if (r->current_page > 0) {
                r->current_page--;
                ebook_save_reader_position(r);
                r->base.needs_full_refresh_flag = true;
                return true;
            }
            return false;
        case BTN_DOWN_CLICK:
            if (r->current_page < r->total_pages - 1) {
                r->current_page++;
                ebook_save_reader_position(r);
                r->base.needs_full_refresh_flag = true;
                return true;
            }
            return false;
        default:
            break;
        }
        return false;
    }

    /* File list mode. */
    if (r->file_count == 0)
        return false;

    switch (event->type) {
    case BTN_UP_CLICK:
        if (r->selected_index > 0) {
            r->selected_index--;
            r->base.needs_full_refresh_flag = true;
            return true;
        }
        break;
    case BTN_DOWN_CLICK:
        if (r->selected_index < r->file_count - 1) {
            r->selected_index++;
            r->base.needs_full_refresh_flag = true;
            return true;
        }
        break;
    case BTN_BOOT_CLICK:
        /* Caller should handle file opening via GetSelectedFile(). */
        return false;
    default:
        break;
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* Data interface                                                      */
/* ------------------------------------------------------------------ */

void ebook_page_set_file_list(page_renderer_t *self, const char *const *files, int count)
{
    ebook_page_t *r = (ebook_page_t *)self;
    if (count > EBOOK_MAX_FILES)
        count = EBOOK_MAX_FILES;
    if (count < 0)
        count = 0;
    r->file_count = count;
    for (int i = 0; i < count; ++i) {
        if (files[i]) {
            strncpy(r->files[i], files[i], sizeof(r->files[i]) - 1);
            r->files[i][sizeof(r->files[i]) - 1] = '\0';
        } else {
            r->files[i][0] = '\0';
        }
    }
    r->selected_index = 0;
    r->base.needs_full_refresh_flag = true;
}

int ebook_page_get_selected_index(const page_renderer_t *self)
{
    const ebook_page_t *r = (const ebook_page_t *)self;
    return r->selected_index;
}

const char *ebook_page_get_selected_file(const page_renderer_t *self)
{
    const ebook_page_t *r = (const ebook_page_t *)self;
    if (r->selected_index >= 0 && r->selected_index < r->file_count) {
        return r->files[r->selected_index];
    }
    return "";
}

void ebook_page_open_file(page_renderer_t *self, const char *filename, const char *content)
{
    ebook_page_t *r = (ebook_page_t *)self;
    if (filename) {
        strncpy(r->reader_filename, filename, sizeof(r->reader_filename) - 1);
        r->reader_filename[sizeof(r->reader_filename) - 1] = '\0';
    } else {
        r->reader_filename[0] = '\0';
    }
    if (content) {
        strncpy(r->reader_content, content, sizeof(r->reader_content) - 1);
        r->reader_content[sizeof(r->reader_content) - 1] = '\0';
    } else {
        r->reader_content[0] = '\0';
    }
    r->reader_mode = true;
    r->portrait_reader = false;
    r->current_page = 0;
    ebook_calc_pages(r);
    r->base.needs_full_refresh_flag = true;
}

void ebook_page_close_reader(page_renderer_t *self)
{
    ebook_page_t *r = (ebook_page_t *)self;
    /* Clear saved reader position when explicitly closing. */
    settings_handle_t h = settings_open("ebook", true);
    if (h) {
        settings_set_string(h, "reader_file", "");
        settings_set_int(h, "reader_page", 0);
        settings_close(h);
    }
    r->reader_mode = false;
    r->portrait_reader = false;
    r->reader_content[0] = '\0';
    r->reader_filename[0] = '\0';
    r->current_page = 0;
    r->total_pages = 0;
    r->base.needs_full_refresh_flag = true;
}

bool ebook_page_is_reader_mode(const page_renderer_t *self)
{
    const ebook_page_t *r = (const ebook_page_t *)self;
    return r->reader_mode;
}

bool ebook_page_is_portrait_reader(const page_renderer_t *self)
{
    const ebook_page_t *r = (const ebook_page_t *)self;
    return r->reader_mode && r->portrait_reader;
}

const char *ebook_page_get_reader_filename(const page_renderer_t *self)
{
    const ebook_page_t *r = (const ebook_page_t *)self;
    return r->reader_filename;
}

int ebook_page_get_current_page(const page_renderer_t *self)
{
    const ebook_page_t *r = (const ebook_page_t *)self;
    return r->current_page;
}

int ebook_page_get_total_pages(const page_renderer_t *self)
{
    const ebook_page_t *r = (const ebook_page_t *)self;
    return r->total_pages;
}

static void ebook_set_portrait_reader(ebook_page_t *r, bool portrait)
{
    if (r->portrait_reader == portrait) {
        r->base.needs_full_refresh_flag = true;
        return;
    }
    const int old_chars_per_page = ebook_chars_per_page(r);
    const int current_offset = RD_MAX(0, r->current_page) * old_chars_per_page;
    r->portrait_reader = portrait;
    ebook_calc_pages(r);
    r->current_page = r->total_pages > 0 ? RD_MIN(r->total_pages - 1, current_offset / ebook_chars_per_page(r)) : 0;
    r->base.needs_full_refresh_flag = true;
}

/* ------------------------------------------------------------------ */
/* vtable instance                                                     */
/* ------------------------------------------------------------------ */

EXT_RAM_BSS_ATTR ebook_page_t s_ebook_instance;

const page_renderer_ops_t ebook_page_ops = {
    .init = ebook_page_init,
    .enter = ebook_page_enter,
    .render = ebook_page_render,
    .handle_input = ebook_page_handle_input,
    .get_dirty_rect = NULL,
    .needs_full_refresh = NULL,
    .mark_full_refresh = NULL,
    .clear_full_refresh_flag = NULL,
    .append_text = NULL,
    .begin_stream = NULL,
    .end_stream = NULL,
};

PAGE_REGISTER(UI_PAGE_EBOOK, "电子书", FA_SETTINGS_BOOK, true, 60, &ebook_page_ops, &s_ebook_instance.base);
