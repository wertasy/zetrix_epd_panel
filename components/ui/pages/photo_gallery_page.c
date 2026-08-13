/**
 * @file photo_gallery.c
 * @brief Memory-style photo gallery renderer — C port of C++
 *        rawdraw::PhotoGalleryRenderer.
 */
#include "photo_gallery_page.h"
#include "page_registry.h"
#include "fa_settings.h"
#include "photo_storage.h"

#include "rawdraw_ext.h"
#include "theme.h"
#include "style.h"
#include "layout.h"
#include "ui_text_util.h"
#include "footer_bar.h"
#include "photo_blit.h"
#include <esp_heap_caps.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <esp_log.h>

#define TAG "PhotoGallery"

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

static const lv_font_t *const kGalleryFont      = &SourceHanSansSC_Regular_slim;
static const lv_font_t *const kGalleryTitleFont = &SourceHanSansSC_Medium_slim;
static const lv_font_t *const kGalleryIconFont  = &font_zectrix_16_1;

/* ------------------------------------------------------------------ */
/* Photo rendering                                                     */
/* ------------------------------------------------------------------ */

static void render_photo_in_rect(page_renderer_t *self, uint8_t *fb, int fb_width, int height,
                                 const photo_gallery_entry_t *entry, int x, int y, int w, int h, bool invert)
{
    photo_gallery_page_t       *r = (photo_gallery_page_t *)self;
    const rawdraw_paint_style_t frame_style =
        invert ? rawdraw_theme_style(THEME_TOKEN_SELECTED) : rawdraw_theme_component(ROLE_CARD_DEFAULT);
    rawdraw_draw_styled_round_rect(fb, fb_width, height, (rawdraw_rect_t){x, y, w, h}, STYLE_BORDER_RADIUS_SM,
                                   &frame_style);

    if (entry->file_size == 0) {
        const char *label = "无图片";
        int         tw    = rawdraw_measure_text_width(label, r->font);
        rawdraw_draw_text(fb, fb_width, height, x + (w - tw) / 2,
                          rawdraw_layout_ink_centered_text_top_y_in_box(r->font, label, y, h, 0), label, r->font,
                          frame_style.fg);
        return;
    }

    uint8_t *photo_buf = (uint8_t *)heap_caps_malloc(entry->file_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!photo_buf)
        photo_buf = (uint8_t *)malloc(entry->file_size);
    if (!photo_buf)
        return;
    int bytes_read = photo_load(entry->id, photo_buf, entry->file_size);
    if (bytes_read <= 0 || entry->width <= 0 || entry->height <= 0) {
        free(photo_buf);
        return;
    }

    const bool bwry2bpp = photo_is_bwry_2bpp(entry->width, entry->height, (uint32_t)bytes_read);
    const bool mono1bpp = photo_is_mono_1bpp(entry->width, entry->height, (uint32_t)bytes_read);
    if (!bwry2bpp && !mono1bpp) {
        free(photo_buf);
        return;
    }
    const int photo_bpr = bwry2bpp ? photo_bytes_per_row_2bpp(entry->width) : photo_bytes_per_row_1bpp(entry->width);

    const int inner_x = x + 4;
    const int inner_y = y + 4;
    const int inner_w = w - 8;
    const int inner_h = h - 8;

    /* Cover mode: photo fills the entire area (may crop overflow). */
    const int draw_w_by_h = entry->height > 0 ? (inner_h * entry->width) / entry->height : inner_w;
    const int draw_h_by_w = entry->width > 0 ? (inner_w * entry->height) / entry->width : inner_h;
    const int draw_w      = MAX(inner_w, draw_w_by_h);
    const int draw_h      = MAX(inner_h, draw_h_by_w);

    const int draw_x = inner_x + (inner_w - draw_w) / 2;
    const int draw_y = inner_y + (inner_h - draw_h) / 2;

    /* Nearest-neighbor scaling. */
    for (int ty = 0; ty < draw_h; ++ty) {
        int src_y = (ty * entry->height) / draw_h;
        if (src_y >= entry->height)
            break;
        for (int tx = 0; tx < draw_w; ++tx) {
            int src_x = (tx * entry->width) / draw_w;
            if (src_x >= entry->width)
                break;
            rawdraw_color_t src_color =
                photo_read_pixel(photo_buf, (uint32_t)bytes_read, photo_bpr, bwry2bpp, src_x, src_y);
            if (draw_x + tx >= inner_x && draw_x + tx < inner_x + inner_w && draw_y + ty >= inner_y &&
                draw_y + ty < inner_y + inner_h) {
                rawdraw_set_pixel(fb, fb_width, height, draw_x + tx, draw_y + ty, src_color);
            }
        }
    }
    free(photo_buf);
}

static void render_memory_card_mode(page_renderer_t *self, uint8_t *fb, int width, int height)
{
    photo_gallery_page_t *r           = (photo_gallery_page_t *)self;
    const int             content_top = STYLE_STATUS_BAR_HEIGHT + STYLE_SPACING_SM;
    const int             footer_h    = STYLE_FOOTER_BAR_HEIGHT;
    const int             body_h      = height - content_top - footer_h;
    const int             gap         = 8;
    const int             info_x      = 12;

    const int left_w_min = 120;
    const int left_w_max = 200;
    int       left_w     = left_w_min;

    if (r->photo_count > 0) {
        const photo_gallery_entry_t *entry         = &r->photo_ids[r->selected_index];
        const char                  *title_text    = entry->title[0] ? entry->title : "那年今日";
        const char                  *body_text     = entry->body[0] ? entry->body : "暂无文案";
        const char                  *date_text     = entry->date[0] ? entry->date : "日期未知";
        const char                  *location_text = entry->location[0] ? entry->location : "地点未知";

        int  max_text_w = 0;
        char lines[5][128];
        int  n = 0;
        ui_text_wrap_lines(r->title_font, title_text, left_w_max - 24, lines, 128, 2, &n);
        for (int i = 0; i < n; ++i) {
            int w      = rawdraw_measure_text_width(lines[i], r->title_font);
            max_text_w = MAX(max_text_w, w);
        }
        ui_text_wrap_lines(r->font, body_text, left_w_max - 24, lines, 128, 5, &n);
        for (int i = 0; i < n; ++i) {
            int w      = rawdraw_measure_text_width(lines[i], r->font);
            max_text_w = MAX(max_text_w, w);
        }
        max_text_w = MAX(max_text_w, rawdraw_measure_text_width(date_text, r->font));
        max_text_w = MAX(max_text_w, rawdraw_measure_text_width(location_text, r->font));

        left_w = MIN(left_w_max, MAX(left_w_min, max_text_w + 24));
    }

    const int photo_x = info_x + left_w + gap;
    const int photo_w = width - photo_x - 12;
    const int card_y  = content_top + 2;
    const int card_h  = body_h - 4;

    const rawdraw_paint_style_t bg_style    = rawdraw_theme_style(THEME_TOKEN_BACKGROUND_PRIMARY);
    const rawdraw_paint_style_t card_style  = rawdraw_theme_component(ROLE_CARD_DEFAULT);
    const rawdraw_paint_style_t badge_style = rawdraw_theme_style(THEME_TOKEN_BADGE);
    const rawdraw_color_t       text        = rawdraw_theme_color_for(THEME_TOKEN_TEXT_PRIMARY);
    const rawdraw_color_t       secondary   = rawdraw_theme_color_for(THEME_TOKEN_TEXT_SECONDARY);

    rawdraw_draw_styled_rect(fb, width, height, (rawdraw_rect_t){0, content_top, width, body_h}, &bg_style);
    rawdraw_draw_styled_round_rect(fb, width, height, (rawdraw_rect_t){info_x, card_y, left_w, card_h},
                                   STYLE_BORDER_RADIUS_MD, &card_style);

    if (r->photo_count == 0) {
        const char *title       = "暂无回忆";
        const char *body        = "通过 /api/push_memory 推送图片和文案";
        const int   safe_x      = info_x + 12;
        const int   safe_w      = left_w - 24;
        const int   title_box_y = card_y + 34;
        const int   body_box_y  = title_box_y + 34;
        rawdraw_draw_text(fb, width, height, safe_x,
                          rawdraw_layout_ink_centered_text_top_y_in_box(r->title_font, title, title_box_y, 24, 0),
                          title, r->title_font, text);
        char lines[4][128];
        int  n = 0;
        ui_text_wrap_lines(r->font, body, safe_w, lines, 128, 4, &n);
        int line_box_y = body_box_y;
        for (int i = 0; i < n; ++i) {
            rawdraw_draw_text(fb, width, height, safe_x,
                              rawdraw_layout_ink_centered_text_top_y_in_box(r->font, lines[i], line_box_y, 22, 0),
                              lines[i], r->font, secondary);
            line_box_y += 24;
        }
        photo_gallery_entry_t empty_entry;
        memset(&empty_entry, 0, sizeof(empty_entry));
        render_photo_in_rect(self, fb, width, height, &empty_entry, photo_x, card_y, photo_w, card_h, false);
        widget_footer_bar_t footer;
        widget_footer_bar_init(&footer, width, height);
        widget_footer_bar_set_text(&footer, "UP上一张", NULL, "BOOT看详情");
        widget_footer_bar_render(&footer, fb, width, height);
        return;
    }

    const photo_gallery_entry_t *entry = &r->photo_ids[r->selected_index];

    const int      text_x      = info_x + 12;
    const int      text_w      = left_w - 24;
    int            y           = card_y + 20;
    const char    *chip_text   = "往年今日";
    const int      chip_text_w = rawdraw_measure_text_width(chip_text, r->font);
    rawdraw_rect_t chip        = {text_x, y, MIN(text_w, chip_text_w + 24), 22};
    rawdraw_draw_styled_round_rect(fb, width, height, chip, STYLE_BORDER_RADIUS_SM, &badge_style);
    rawdraw_draw_styled_text(fb, width, height, chip.x + (chip.w - chip_text_w) / 2,
                             rawdraw_layout_ink_centered_text_top_y_in_box(r->font, "往年今日", chip.y, chip.h, 0),
                             chip_text, r->font, &badge_style);
    y += 32;

    char title_lines[2][128];
    int  title_n = 0;
    ui_text_wrap_lines(r->title_font, entry->title[0] ? entry->title : "那年今日", text_w, title_lines, 128, 2,
                       &title_n);
    const int kTitleLineBoxH = 24;
    for (int i = 0; i < title_n; ++i) {
        rawdraw_draw_text(
            fb, width, height, text_x,
            rawdraw_layout_ink_centered_text_top_y_in_box(r->title_font, title_lines[i], y, kTitleLineBoxH, 0),
            title_lines[i], r->title_font, text);
        y += kTitleLineBoxH + 2;
    }
    y += 4;

    char body_lines[5][128];
    int  body_n = 0;
    ui_text_wrap_lines(r->font, entry->body[0] ? entry->body : "暂无文案", text_w, body_lines, 128, 5, &body_n);
    const int kBodyLineBoxH = 22;
    for (int i = 0; i < body_n; ++i) {
        rawdraw_draw_text(fb, width, height, text_x,
                          rawdraw_layout_ink_centered_text_top_y_in_box(r->font, body_lines[i], y, kBodyLineBoxH, 0),
                          body_lines[i], r->font, text);
        y += kBodyLineBoxH + 1;
    }

    const int                   meta_block_h = 44;
    const int                   meta_y       = card_y + card_h - meta_block_h - 12;
    const rawdraw_paint_style_t meta_style   = rawdraw_theme_style(THEME_TOKEN_BACKGROUND_SECONDARY);
    rawdraw_draw_styled_round_rect(fb, width, height, (rawdraw_rect_t){text_x - 4, meta_y, text_w + 8, meta_block_h},
                                   STYLE_BORDER_RADIUS_SM, &meta_style);
    const char *date_label     = entry->date[0] ? entry->date : "日期未知";
    const char *location_label = entry->location[0] ? entry->location : "地点未知";
    char        date_fit[128];
    char        loc_fit[128];
    ui_text_fit_to_width(date_label, r->font, text_w - 8, date_fit, sizeof(date_fit));
    ui_text_fit_to_width(location_label, r->font, text_w - 8, loc_fit, sizeof(loc_fit));
    const int date_center_y     = meta_y + 14;
    const int location_center_y = meta_y + 30;
    rawdraw_draw_text(fb, width, height, text_x + 4,
                      rawdraw_layout_ink_centered_text_top_y(r->font, date_fit, date_center_y, 0), date_fit, r->font,
                      secondary);
    rawdraw_draw_text(fb, width, height, text_x + 4,
                      rawdraw_layout_ink_centered_text_top_y(r->font, loc_fit, location_center_y, 0), loc_fit, r->font,
                      secondary);

    render_photo_in_rect(self, fb, width, height, entry, photo_x, card_y, photo_w, card_h, false);

    widget_footer_bar_t footer;
    widget_footer_bar_init(&footer, width, height);
    char counter[40];
    snprintf(counter, sizeof(counter), "%d/%d", r->selected_index + 1, r->photo_count);
    widget_footer_bar_set_text(&footer, "UP/DN翻页", counter, "BOOT看详情");
    widget_footer_bar_render(&footer, fb, width, height);
}

static void render_fullscreen_mode(page_renderer_t *self, uint8_t *fb, int width, int height)
{
    photo_gallery_page_t       *r        = (photo_gallery_page_t *)self;
    const rawdraw_paint_style_t bg_style = rawdraw_theme_style(THEME_TOKEN_BACKGROUND_PRIMARY);
    rawdraw_draw_styled_rect(fb, width, height, (rawdraw_rect_t){0, 0, width, height}, &bg_style);

    if (r->photo_count == 0 || !r->current_photo_data || r->current_photo_size == 0) {
        const char *label = "无法加载照片";
        int         tw    = rawdraw_measure_text_width(label, r->font);
        rawdraw_draw_text(fb, width, height, (width - tw) / 2, height / 2, label, r->font,
                          rawdraw_theme_color_for(THEME_TOKEN_TEXT_PRIMARY));
        return;
    }

    const bool bwry2bpp = photo_is_bwry_2bpp(r->current_photo_width, r->current_photo_height, r->current_photo_size);
    const int  photo_byte_width =
        bwry2bpp ? photo_bytes_per_row_2bpp(r->current_photo_width) : photo_bytes_per_row_1bpp(r->current_photo_width);
    const int expected_rows =
        (bwry2bpp || photo_is_mono_1bpp(r->current_photo_width, r->current_photo_height, r->current_photo_size))
            ? MIN(r->current_photo_height, (int)(r->current_photo_size / photo_byte_width))
            : 0;
    int start_y = (height - expected_rows) / 2;
    if (start_y < 0)
        start_y = 0;

    const int draw_w  = MIN(width, r->current_photo_width);
    const int start_x = MAX(0, (width - draw_w) / 2);
    for (int row = 0; row < expected_rows && (start_y + row) < height; row++) {
        for (int tx = 0; tx < draw_w; ++tx) {
            const rawdraw_color_t src_color =
                photo_read_pixel(r->current_photo_data, r->current_photo_size, photo_byte_width, bwry2bpp, tx, row);
            rawdraw_set_pixel(fb, width, height, start_x + tx, start_y + row, src_color);
        }
    }
}

static void render_delete_dialog(page_renderer_t *self, uint8_t *fb, int width, int height)
{
    photo_gallery_page_t *r = (photo_gallery_page_t *)self;
    if (!fb)
        return;
    const rawdraw_paint_style_t bg_style       = rawdraw_theme_style(THEME_TOKEN_BACKGROUND_PRIMARY);
    const rawdraw_paint_style_t modal_style    = rawdraw_theme_component(ROLE_MODAL);
    const rawdraw_paint_style_t shadow_style   = rawdraw_theme_style(THEME_TOKEN_SHADOW);
    const rawdraw_paint_style_t selected_style = rawdraw_theme_component(ROLE_BUTTON_SELECTED);
    const rawdraw_paint_style_t danger_style   = rawdraw_theme_component(ROLE_BUTTON_DANGER);
    const rawdraw_color_t       text           = rawdraw_theme_color_for(THEME_TOKEN_TEXT_PRIMARY);
    const rawdraw_color_t       secondary      = rawdraw_theme_color_for(THEME_TOKEN_TEXT_SECONDARY);
    const rawdraw_color_t       border         = rawdraw_theme_color_for(THEME_TOKEN_BORDER);
    const rawdraw_color_t       danger         = rawdraw_theme_color_for(THEME_TOKEN_DANGER);

    const int dialog_w      = STYLE_DIALOG_W;
    const int dialog_h      = STYLE_DIALOG_H_SM;
    const int dialog_x      = (width - dialog_w) / 2;
    const int dialog_y      = (height - dialog_h) / 2;
    const int titlebar_h    = 28;
    const int shadow_offset = 2;

    rawdraw_draw_styled_round_rect(fb, width, height,
                                   (rawdraw_rect_t){dialog_x - 4, dialog_y - 4, dialog_w + 10, dialog_h + 10},
                                   STYLE_BORDER_RADIUS_MD, &bg_style);
    rawdraw_draw_styled_round_rect(
        fb, width, height, (rawdraw_rect_t){dialog_x + shadow_offset, dialog_y + shadow_offset, dialog_w, dialog_h},
        STYLE_BORDER_RADIUS_MD, &shadow_style);
    rawdraw_draw_styled_round_rect(fb, width, height, (rawdraw_rect_t){dialog_x, dialog_y, dialog_w, dialog_h},
                                   STYLE_BORDER_RADIUS_MD, &modal_style);
    rawdraw_draw_hline(fb, width, height, dialog_y + titlebar_h, dialog_x + 1, dialog_x + dialog_w - 2, border);
    rawdraw_draw_rect_border(fb, width, height, (rawdraw_rect_t){dialog_x + 8, dialog_y + 8, 12, 12}, 1, danger);
    rawdraw_draw_line(fb, width, height, (rawdraw_point_t){dialog_x + 10, dialog_y + 10},
                      (rawdraw_point_t){dialog_x + 18, dialog_y + 18}, danger);
    rawdraw_draw_line(fb, width, height, (rawdraw_point_t){dialog_x + 18, dialog_y + 10},
                      (rawdraw_point_t){dialog_x + 10, dialog_y + 18}, danger);

    const char *title   = "删除照片";
    const int   title_w = rawdraw_measure_text_width(title, r->font);
    rawdraw_draw_text(fb, width, height, dialog_x + (dialog_w - title_w) / 2,
                      rawdraw_layout_ink_centered_text_top_y_in_box(r->font, title, dialog_y, titlebar_h, 0), title,
                      r->font, danger);
    for (int yy = dialog_y + 6; yy < dialog_y + titlebar_h - 5; yy += 4) {
        rawdraw_draw_hline(fb, width, height, yy, dialog_x + 28, dialog_x + (dialog_w - title_w) / 2 - 8, border);
        rawdraw_draw_hline(fb, width, height, yy, dialog_x + (dialog_w + title_w) / 2 + 8, dialog_x + dialog_w - 12,
                           border);
    }

    const char *body   = "确认删除当前照片？";
    const int   body_w = rawdraw_measure_text_width(body, r->title_font);
    rawdraw_draw_text(
        fb, width, height, dialog_x + (dialog_w - body_w) / 2,
        rawdraw_layout_ink_centered_text_top_y_in_box(r->title_font, body, dialog_y + titlebar_h + 18, 28, 0), body,
        r->title_font, text);

    const char *labels[] = {"删除", "取消"};
    const int   button_y = dialog_y + 94;
    const int   button_w = 92;
    const int   button_h = 30;
    const int   gap      = 18;
    const int   start_x  = dialog_x + (dialog_w - button_w * 2 - gap) / 2;
    for (int i = 0; i < 2; ++i) {
        const int                   x        = start_x + i * (button_w + gap);
        const bool                  selected = r->delete_dialog_selected == i;
        const rawdraw_paint_style_t style    = selected ? (i == 0 ? danger_style : selected_style) : modal_style;
        rawdraw_draw_styled_round_rect(fb, width, height, (rawdraw_rect_t){x, button_y, button_w, button_h},
                                       STYLE_BORDER_RADIUS_SM, &style);
        const int label_w = rawdraw_measure_text_width(labels[i], r->font);
        rawdraw_draw_text(fb, width, height, x + (button_w - label_w) / 2,
                          rawdraw_layout_ink_centered_text_top_y_in_box(r->font, labels[i], button_y, button_h, 0),
                          labels[i], r->font, style.fg);
    }

    const char *hint   = "UP/DN 切换  BOOT 确认";
    const int   hint_w = rawdraw_measure_text_width(hint, r->font);
    rawdraw_draw_text(fb, width, height, dialog_x + (dialog_w - hint_w) / 2,
                      rawdraw_layout_ink_centered_text_top_y_in_box(r->font, hint, dialog_y + dialog_h - 24, 20, 0),
                      hint, r->font, secondary);
}

/* ------------------------------------------------------------------ */
/* Data management                                                     */
/* ------------------------------------------------------------------ */

static void load_photo_data(page_renderer_t *self, int index)
{
    photo_gallery_page_t *r = (photo_gallery_page_t *)self;
    if (r->current_photo_data) {
        free(r->current_photo_data);
        r->current_photo_data = NULL;
    }
    r->current_photo_size = 0;

    if (index < 0 || index >= r->photo_count)
        return;

    photo_info_t info;
    if (photo_get_by_index(index, &info) != 0)
        return;

    r->current_photo_data = (uint8_t *)heap_caps_malloc(info.file_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!r->current_photo_data)
        r->current_photo_data = (uint8_t *)malloc(info.file_size);
    if (!r->current_photo_data)
        return;

    int bytes_read = photo_load(info.id, r->current_photo_data, info.file_size);
    if (bytes_read > 0) {
        r->current_photo_size   = (uint32_t)bytes_read;
        r->current_photo_width  = info.width;
        r->current_photo_height = info.height;
    } else {
        free(r->current_photo_data);
        r->current_photo_data = NULL;
    }
}

static void delete_selected_photo(page_renderer_t *self)
{
    photo_gallery_page_t *r = (photo_gallery_page_t *)self;
    if (r->selected_index < 0 || r->selected_index >= r->photo_count)
        return;
    char id[16] = {0};
    strncpy(id, r->photo_ids[r->selected_index].id, sizeof(id) - 1);
    id[sizeof(id) - 1] = '\0';
    if (photo_delete(id) == 0) {
        ESP_LOGI(TAG, "Deleted photo id=%s", id);
        photo_gallery_refresh_photo_list(self);
        photo_gallery_set_selected_index(self, r->selected_index);
        if (r->mode == PHOTO_GALLERY_MODE_FULLSCREEN) {
            if (r->photo_count > 0) {
                load_photo_data(self, r->selected_index);
            } else {
                r->mode = PHOTO_GALLERY_MODE_MEMORY_CARD;
            }
        }
    } else {
        ESP_LOGW(TAG, "Failed to delete photo id=%s", id);
    }
}

static void clamp_selection(page_renderer_t *self)
{
    photo_gallery_page_t *r = (photo_gallery_page_t *)self;
    if (r->photo_count == 0) {
        r->selected_index = 0;
    } else if (r->selected_index >= r->photo_count) {
        r->selected_index = r->photo_count - 1;
    } else if (r->selected_index < 0) {
        r->selected_index = 0;
    }
}

/* ------------------------------------------------------------------ */
/* PageRenderer vtable                                                 */
/* ------------------------------------------------------------------ */

void photo_gallery_init(page_renderer_t *self, int width, int height)
{
    photo_gallery_page_t *r         = (photo_gallery_page_t *)self;
    /* Preserve mode and selection across page switches (init is called
     * every time the page becomes active). Only reset on first init. */
    bool first_init = (r->photo_count == 0 && r->mode == 0);
    r->base.width                   = width;
    r->base.height                  = height;
    r->base.needs_full_refresh_flag = true;
    if (first_init) {
        r->mode                         = PHOTO_GALLERY_MODE_MEMORY_CARD;
        r->selected_index               = 0;
    }
    r->showing_delete_dialog        = false;
    r->delete_dialog_selected       = 1;
    r->current_photo_data           = NULL;
    r->current_photo_size           = 0;
    r->current_photo_width          = STYLE_SCREEN_WIDTH;
    r->current_photo_height         = STYLE_SCREEN_HEIGHT;
    r->font                         = kGalleryFont;
    r->title_font                   = kGalleryTitleFont;
    r->icon_font                    = kGalleryIconFont;
    r->photo_count                  = 0;
    photo_gallery_refresh_photo_list(self);
    /* If returning to fullscreen mode, reload the photo data. */
    if (!first_init && r->mode == PHOTO_GALLERY_MODE_FULLSCREEN && r->photo_count > 0) {
        load_photo_data(self, r->selected_index);
    }
}

void photo_gallery_render(page_renderer_t *self, uint8_t *fb, int width, int height)
{
    photo_gallery_page_t *r = (photo_gallery_page_t *)self;
    if (!fb)
        return;
    switch (r->mode) {
    case PHOTO_GALLERY_MODE_MEMORY_CARD:
        render_memory_card_mode(self, fb, width, height);
        break;
    case PHOTO_GALLERY_MODE_FULLSCREEN:
        render_fullscreen_mode(self, fb, width, height);
        break;
    }
    if (r->showing_delete_dialog) {
        render_delete_dialog(self, fb, width, height);
    }
    r->base.needs_full_refresh_flag = false;
}

bool photo_gallery_handle_input(page_renderer_t *self, const ui_button_event_t *event)
{
    photo_gallery_page_t *r = (photo_gallery_page_t *)self;
    if (r->showing_delete_dialog) {
        switch (event->type) {
        case BTN_UP_CLICK:
        case BTN_DOWN_CLICK:
            r->delete_dialog_selected       = r->delete_dialog_selected == 0 ? 1 : 0;
            r->base.needs_full_refresh_flag = true;
            return true;
        case BTN_BOOT_CLICK:
            if (r->delete_dialog_selected == 0) {
                delete_selected_photo(self);
            }
            r->showing_delete_dialog        = false;
            r->delete_dialog_selected       = 1;
            r->base.needs_full_refresh_flag = true;
            return true;
        case BTN_BOOT_LONG_PRESS:
        case BTN_BOOT_DOUBLE_CLICK:
            r->showing_delete_dialog        = false;
            r->delete_dialog_selected       = 1;
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
            clamp_selection(self);
            if (r->mode == PHOTO_GALLERY_MODE_FULLSCREEN)
                load_photo_data(self, r->selected_index);
            r->base.needs_full_refresh_flag = true;
            return true;
        }
        break;
    case BTN_DOWN_CLICK:
        if (r->selected_index < r->photo_count - 1) {
            r->selected_index++;
            clamp_selection(self);
            if (r->mode == PHOTO_GALLERY_MODE_FULLSCREEN)
                load_photo_data(self, r->selected_index);
            r->base.needs_full_refresh_flag = true;
            return true;
        }
        break;
    case BTN_BOOT_CLICK:
        if (r->photo_count == 0)
            break;
        if (r->mode == PHOTO_GALLERY_MODE_MEMORY_CARD) {
            clamp_selection(self);
            load_photo_data(self, r->selected_index);
            r->mode = PHOTO_GALLERY_MODE_FULLSCREEN;
        } else {
            r->mode = PHOTO_GALLERY_MODE_MEMORY_CARD;
        }
        r->base.needs_full_refresh_flag = true;
        return true;
    case BTN_BOOT_DOUBLE_CLICK:
        if (r->photo_count > 0) {
            r->showing_delete_dialog        = true;
            r->delete_dialog_selected       = 1;
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

void photo_gallery_refresh_photo_list(page_renderer_t *self)
{
    photo_gallery_page_t *r = (photo_gallery_page_t *)self;
    r->photo_count          = 0;

    photo_info_t info;
    int          count = photo_get_count();
    for (int i = 0; i < count && i < PHOTO_MAX_PHOTOS; i++) {
        if (photo_get_by_index(i, &info) == 0) {
            photo_gallery_entry_t *entry = &r->photo_ids[r->photo_count];
            memset(entry, 0, sizeof(*entry));
            strncpy(entry->id, info.id, sizeof(entry->id) - 1);
            entry->id[sizeof(entry->id) - 1] = '\0';
            strncpy(entry->title, info.title, sizeof(entry->title) - 1);
            entry->title[sizeof(entry->title) - 1] = '\0';
            strncpy(entry->date, info.date, sizeof(entry->date) - 1);
            entry->date[sizeof(entry->date) - 1] = '\0';
            strncpy(entry->location, info.location, sizeof(entry->location) - 1);
            entry->location[sizeof(entry->location) - 1] = '\0';
            strncpy(entry->body, info.body, sizeof(entry->body) - 1);
            entry->body[sizeof(entry->body) - 1] = '\0';
            entry->width                         = info.width;
            entry->height                        = info.height;
            entry->file_size                     = info.file_size;
            ++r->photo_count;
        }
    }
    clamp_selection(self);
    ESP_LOGI(TAG, "RefreshPhotoList: storage_count=%d visible_count=%d selected=%d", count, r->photo_count,
             r->selected_index);
}

int photo_gallery_get_photo_count(const page_renderer_t *self)
{
    const photo_gallery_page_t *r = (const photo_gallery_page_t *)self;
    return r->photo_count;
}

int photo_gallery_get_selected_index(const page_renderer_t *self)
{
    const photo_gallery_page_t *r = (const photo_gallery_page_t *)self;
    return r->selected_index;
}

void photo_gallery_set_selected_index(page_renderer_t *self, int index)
{
    photo_gallery_page_t *r = (photo_gallery_page_t *)self;
    if (r->photo_count == 0) {
        r->selected_index = 0;
        return;
    }
    r->selected_index = MAX(0, MIN(index, r->photo_count - 1));
    if (r->mode == PHOTO_GALLERY_MODE_FULLSCREEN) {
        load_photo_data(self, r->selected_index);
    }
}

bool photo_gallery_set_selected_by_id(page_renderer_t *self, const char *id)
{
    photo_gallery_page_t *r = (photo_gallery_page_t *)self;
    if (!id || id[0] == '\0')
        return false;
    for (int i = 0; i < r->photo_count; ++i) {
        if (strcmp(r->photo_ids[i].id, id) == 0) {
            photo_gallery_set_selected_index(self, i);
            return true;
        }
    }
    return false;
}

void photo_gallery_enter_fullscreen_mode(page_renderer_t *self)
{
    photo_gallery_page_t *r = (photo_gallery_page_t *)self;
    if (r->photo_count == 0)
        return;
    r->showing_delete_dialog = false;
    r->mode                  = PHOTO_GALLERY_MODE_FULLSCREEN;
    load_photo_data(self, r->selected_index);
}

bool photo_gallery_select_next(page_renderer_t *self, bool wrap)
{
    photo_gallery_page_t *r = (photo_gallery_page_t *)self;
    if (r->photo_count <= 1)
        return false;

    int next = r->selected_index + 1;
    if (next >= r->photo_count) {
        if (!wrap)
            return false;
        next = 0;
    }

    photo_gallery_set_selected_index(self, next);
    r->base.needs_full_refresh_flag = true;
    ESP_LOGI(TAG, "Slideshow next photo: %d/%d", r->selected_index + 1, r->photo_count);
    return true;
}

bool photo_gallery_is_fullscreen_mode(const page_renderer_t *self)
{
    const photo_gallery_page_t *r = (const photo_gallery_page_t *)self;
    return r->mode == PHOTO_GALLERY_MODE_FULLSCREEN;
}

bool photo_gallery_is_delete_dialog_open(const page_renderer_t *self)
{
    const photo_gallery_page_t *r = (const photo_gallery_page_t *)self;
    return r->showing_delete_dialog;
}

bool photo_gallery_is_current_photo_bwry2bpp(const page_renderer_t *self)
{
    const photo_gallery_page_t *r = (const photo_gallery_page_t *)self;
    return photo_is_bwry_2bpp(r->current_photo_width, r->current_photo_height, r->current_photo_size);
}

const uint8_t *photo_gallery_get_current_photo_data(const page_renderer_t *self)
{
    const photo_gallery_page_t *r = (const photo_gallery_page_t *)self;
    return r->current_photo_data;
}

uint32_t photo_gallery_get_current_photo_size(const page_renderer_t *self)
{
    const photo_gallery_page_t *r = (const photo_gallery_page_t *)self;
    return r->current_photo_size;
}

int photo_gallery_get_current_photo_width(const page_renderer_t *self)
{
    const photo_gallery_page_t *r = (const photo_gallery_page_t *)self;
    return r->current_photo_width;
}

int photo_gallery_get_current_photo_height(const page_renderer_t *self)
{
    const photo_gallery_page_t *r = (const photo_gallery_page_t *)self;
    return r->current_photo_height;
}

/* ------------------------------------------------------------------ */
/* vtable instance                                                     */
/* ------------------------------------------------------------------ */

EXT_RAM_BSS_ATTR photo_gallery_page_t s_gallery_instance;

const page_renderer_ops_t photo_gallery_ops = {
    .init                    = photo_gallery_init,
    .render                  = photo_gallery_render,
    .handle_input            = photo_gallery_handle_input,
    .get_dirty_rect          = NULL,
    .needs_full_refresh      = NULL,
    .mark_full_refresh       = NULL,
    .clear_full_refresh_flag = NULL,
    .append_text             = NULL,
    .begin_stream            = NULL,
    .end_stream              = NULL,
};

PAGE_REGISTER(UI_PAGE_GALLERY, "相册", FA_SETTINGS_IMAGE, true, 10, &photo_gallery_ops, &s_gallery_instance.base);
