/**
 * @file photo_detail_renderer.c
 * @brief Photo detail page renderer — C port of C++
 *        rawdraw::PhotoDetailRenderer.
 *
 * Fullscreen photo view with cover-crop scaling and a metadata modal.
 */
#include "photo_detail_page.h"
#include "page_registry.h"
#include "photo_storage.h"

#include "rawdraw_ext.h"
#include "theme.h"
#include "style.h"
#include "layout.h"
#include "ui_text_util.h"
#include "modal.h"
#include "photo_blit.h"
#include <esp_heap_caps.h>

#include <stdlib.h>
#include <string.h>

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

static const lv_font_t *const kDetailFont = &SourceHanSansSC_Regular_slim;
static const lv_font_t *const kDetailTitleFont = &SourceHanSansSC_Medium_slim;

/* Data management                                                     */
/* ------------------------------------------------------------------ */

static void pd_load_photo_data(page_renderer_t *self, int index)
{
    photo_detail_page_t *r = (photo_detail_page_t *)self;
    if (r->current_photo_data) {
        free(r->current_photo_data);
        r->current_photo_data = NULL;
    }
    r->current_photo_size = 0;

    if (index < 0 || index >= r->photo_count)
        return;
    const photo_info_t *info = &r->photos[index];
    r->current_photo_data = (uint8_t *)heap_caps_malloc(info->file_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!r->current_photo_data)
        r->current_photo_data = (uint8_t *)malloc(info->file_size);
    if (!r->current_photo_data)
        return;
    const int bytes_read = photo_load(info->id, r->current_photo_data, info->file_size);
    if (bytes_read > 0) {
        r->current_photo_size = (uint32_t)bytes_read;
        r->current_photo_width = info->width;
        r->current_photo_height = info->height;
    } else {
        free(r->current_photo_data);
        r->current_photo_data = NULL;
    }
}

static void pd_clamp_selection(page_renderer_t *self)
{
    photo_detail_page_t *r = (photo_detail_page_t *)self;
    if (r->photo_count == 0) {
        r->selected_index = 0;
    } else {
        r->selected_index = MAX(0, MIN(r->selected_index, r->photo_count - 1));
    }
}

static void pd_draw_metadata_modal(page_renderer_t *self, uint8_t *fb, int width, int height)
{
    photo_detail_page_t *r = (photo_detail_page_t *)self;
    const photo_info_t *info = &r->photos[r->selected_index];
    const rawdraw_color_t text = rawdraw_theme_color_for(THEME_TOKEN_TEXT_PRIMARY);
    const rawdraw_color_t secondary = rawdraw_theme_color_for(THEME_TOKEN_TEXT_SECONDARY);

    widget_modal_t modal;
    widget_modal_init(&modal);
    widget_modal_set_title(&modal, "照片信息");
    widget_modal_set_footer(&modal, "BOOT关闭");
    widget_modal_center_in_screen(&modal, width, height, 40);
    widget_modal_render(&modal, fb, width, height);

    const rawdraw_rect_t body = widget_modal_get_content_bounds(&modal);
    int y = body.y;
    char fit[256];
    ui_text_fit_to_width(info->title, r->title_font, body.w, fit, sizeof(fit));
    rawdraw_draw_text(fb, width, height, body.x, y, fit, r->title_font, text);
    y += r->title_font->line_height + 8;
    ui_text_fit_to_width(info->date, r->font, body.w, fit, sizeof(fit));
    rawdraw_draw_text(fb, width, height, body.x, y, fit, r->font, secondary);
    y += r->font->line_height + 4;
    ui_text_fit_to_width(info->location, r->font, body.w, fit, sizeof(fit));
    rawdraw_draw_text(fb, width, height, body.x, y, fit, r->font, secondary);
    y += r->font->line_height + 8;
    ui_text_fit_to_width(info->body, r->font, body.w, fit, sizeof(fit));
    rawdraw_draw_text(fb, width, height, body.x, y, fit, r->font, text);
}

/* ------------------------------------------------------------------ */
/* PageRenderer vtable                                                 */
/* ------------------------------------------------------------------ */

void photo_detail_page_init(page_renderer_t *self, int width, int height)
{
    photo_detail_page_t *r = (photo_detail_page_t *)self;
    r->base.width = width;
    r->base.height = height;
    r->metadata_open = false;
    r->selected_index = 0;
    r->photo_count = 0;
    r->current_photo_data = NULL;
    r->current_photo_size = 0;
    r->current_photo_width = STYLE_SCREEN_WIDTH;
    r->current_photo_height = STYLE_SCREEN_HEIGHT;
    r->font = kDetailFont;
    r->title_font = kDetailTitleFont;
    photo_detail_page_refresh_photo_list(self);
    pd_load_photo_data(self, r->selected_index);
    r->base.needs_full_refresh_flag = true;
}

void photo_detail_page_render(page_renderer_t *self, uint8_t *fb, int width, int height)
{
    photo_detail_page_t *r = (photo_detail_page_t *)self;
    if (!fb)
        return;
    const rawdraw_paint_style_t bg_style = rawdraw_theme_style(THEME_TOKEN_BACKGROUND_PRIMARY);
    rawdraw_draw_styled_rect(fb, width, height,
                             (rawdraw_rect_t){0, STYLE_STATUS_BAR_HEIGHT, width, height - STYLE_STATUS_BAR_HEIGHT},
                             &bg_style);

    if (r->photo_count == 0 || !r->current_photo_data || r->current_photo_size == 0) {
        widget_modal_t modal;
        widget_modal_init(&modal);
        widget_modal_set_title(&modal, "暂无照片");
        widget_modal_set_footer(&modal, "等待推送");
        widget_modal_center_in_screen(&modal, width, height, 52);
        widget_modal_render(&modal, fb, width, height);
    } else {
        const int frame_x = 8;
        const int frame_y = STYLE_STATUS_BAR_HEIGHT + 4;
        const int frame_w = width - 16;
        const int frame_h = height - frame_y - 4;
        const rawdraw_paint_style_t card_style = rawdraw_theme_component(ROLE_CARD_DEFAULT);
        rawdraw_draw_styled_round_rect(fb, width, height, (rawdraw_rect_t){frame_x, frame_y, frame_w, frame_h},
                                       STYLE_BORDER_RADIUS_MD, &card_style);

        const bool bwry2bpp =
            photo_is_bwry_2bpp(r->current_photo_width, r->current_photo_height, r->current_photo_size);
        const int photo_byte_width = bwry2bpp ? photo_bytes_per_row_2bpp(r->current_photo_width)
                                              : photo_bytes_per_row_1bpp(r->current_photo_width);
        const int expected_rows =
            (bwry2bpp || photo_is_mono_1bpp(r->current_photo_width, r->current_photo_height, r->current_photo_size))
                ? MIN(r->current_photo_height, (int)(r->current_photo_size / photo_byte_width))
                : 0;
        const int inner_x = frame_x + 6;
        const int inner_y = frame_y + 6;
        const int inner_w = frame_w - 12;
        const int inner_h = frame_h - 12;

        for (int ty = 0; ty < inner_h; ++ty) {
            const int src_y = (ty * r->current_photo_height) / MAX(1, inner_h);
            if (src_y >= expected_rows)
                break;
            for (int tx = 0; tx < inner_w; ++tx) {
                const int src_x = (tx * r->current_photo_width) / MAX(1, inner_w);
                const rawdraw_color_t src_color = photo_read_pixel(r->current_photo_data, r->current_photo_size,
                                                                   photo_byte_width, bwry2bpp, src_x, src_y);
                rawdraw_set_pixel(fb, width, height, inner_x + tx, inner_y + ty, src_color);
            }
        }
    }

    if (r->metadata_open && r->photo_count > 0) {
        pd_draw_metadata_modal(self, fb, width, height);
    }

    r->base.needs_full_refresh_flag = false;
}

bool photo_detail_page_handle_input(page_renderer_t *self, const ui_button_event_t *event)
{
    photo_detail_page_t *r = (photo_detail_page_t *)self;
    if (r->metadata_open) {
        if (event->type == BTN_BOOT_CLICK || event->type == BTN_BOOT_LONG_PRESS) {
            r->metadata_open = false;
            r->base.needs_full_refresh_flag = true;
            return true;
        }
        return true;
    }

    switch (event->type) {
    case BTN_UP_CLICK:
        if (r->selected_index > 0) {
            r->selected_index--;
            pd_load_photo_data(self, r->selected_index);
            r->base.needs_full_refresh_flag = true;
            return true;
        }
        break;
    case BTN_DOWN_CLICK:
        if (r->selected_index < r->photo_count - 1) {
            r->selected_index++;
            pd_load_photo_data(self, r->selected_index);
            r->base.needs_full_refresh_flag = true;
            return true;
        }
        break;
    case BTN_BOOT_CLICK:
        if (r->photo_count > 0) {
            r->metadata_open = true;
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

void photo_detail_page_refresh_photo_list(page_renderer_t *self)
{
    photo_detail_page_t *r = (photo_detail_page_t *)self;
    r->photo_count = 0;
    photo_info_t info;
    const int count = photo_get_count();
    for (int i = 0; i < count && i < PHOTO_MAX_PHOTOS; ++i) {
        if (photo_get_by_index(i, &info) == 0) {
            r->photos[r->photo_count++] = info;
        }
    }
    pd_clamp_selection(self);
}

void photo_detail_page_set_selection(page_renderer_t *self, int index)
{
    photo_detail_page_t *r = (photo_detail_page_t *)self;
    r->selected_index = index;
    pd_clamp_selection(self);
    pd_load_photo_data(self, r->selected_index);
    r->base.needs_full_refresh_flag = true;
}

bool photo_detail_page_is_metadata_open(const page_renderer_t *self)
{
    const photo_detail_page_t *r = (const photo_detail_page_t *)self;
    return r->metadata_open;
}

bool photo_detail_page_is_current_photo_bwry2bpp(const page_renderer_t *self)
{
    const photo_detail_page_t *r = (const photo_detail_page_t *)self;
    return photo_is_bwry_2bpp(r->current_photo_width, r->current_photo_height, r->current_photo_size);
}

const uint8_t *photo_detail_page_get_current_photo_data(const page_renderer_t *self)
{
    const photo_detail_page_t *r = (const photo_detail_page_t *)self;
    return r->current_photo_data;
}

uint32_t photo_detail_page_get_current_photo_size(const page_renderer_t *self)
{
    const photo_detail_page_t *r = (const photo_detail_page_t *)self;
    return r->current_photo_size;
}

int photo_detail_page_get_current_photo_width(const page_renderer_t *self)
{
    const photo_detail_page_t *r = (const photo_detail_page_t *)self;
    return r->current_photo_width;
}

int photo_detail_page_get_current_photo_height(const page_renderer_t *self)
{
    const photo_detail_page_t *r = (const photo_detail_page_t *)self;
    return r->current_photo_height;
}

/* ------------------------------------------------------------------ */
/* vtable instance                                                     */
/* ------------------------------------------------------------------ */

EXT_RAM_BSS_ATTR photo_detail_page_t s_photo_detail_instance;

const page_renderer_ops_t photo_detail_page_ops = {
    .init = photo_detail_page_init,
    .render = photo_detail_page_render,
    .handle_input = photo_detail_page_handle_input,
    .get_dirty_rect = NULL,
    .needs_full_refresh = NULL,
    .mark_full_refresh = NULL,
    .clear_full_refresh_flag = NULL,
    .append_text = NULL,
    .begin_stream = NULL,
    .end_stream = NULL,
};

PAGE_REGISTER(UI_PAGE_PHOTO_DETAIL, "照片详情", NULL, false, 999, &photo_detail_page_ops,
              &s_photo_detail_instance.base);
