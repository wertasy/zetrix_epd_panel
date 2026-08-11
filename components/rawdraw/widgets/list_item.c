/**
 * @file list_item.c
 * @brief List item widget implementation.
 */
#include "../include/rawdraw_util.h"
#include "list_item.h"
#include "../include/rawdraw_ext.h"
#include "../include/theme.h"
#include "../include/style.h"
#include <string.h>

/* ============================================================
 * Local helpers
 * ============================================================ */

static void copy_str(char *dst, size_t cap, const char *src)
{
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

/** Draw a right-pointing chevron (>) using two line segments. */
static void draw_chevron(uint8_t *fb, int width, int height, int x, int y, rawdraw_color_t color)
{
    const int       size = 5;
    rawdraw_point_t p1   = {x, y - size}; /* top-left  */
    rawdraw_point_t p2   = {x + size, y}; /* tip       */
    rawdraw_point_t p3   = {x, y + size}; /* bottom-left */
    rawdraw_draw_line(fb, width, height, p1, p2, color);
    rawdraw_draw_line(fb, width, height, p2, p3, color);
}

/* ============================================================
 * Lifecycle
 * ============================================================ */

void widget_list_item_init(widget_list_item_t *item, int x, int y, int w, int h)
{
    if (!item)
        return;
    memset(item, 0, sizeof(*item));
    item->x                  = x;
    item->y                  = y;
    item->w                  = w;
    item->h                  = h;
    item->padding            = STYLE_LIST_ITEM_PADDING;
    item->show_chevron       = false;
    item->show_separator     = true;
    item->pressed            = false;
    item->callback           = NULL;
    item->callback_user_data = NULL;
    item->bg_color           = RAWDRAW_COLOR_WHITE;
    item->text_color         = RAWDRAW_COLOR_BLACK;
    item->value_text_color   = RAWDRAW_COLOR_BLACK;
    item->separator_color    = RAWDRAW_COLOR_BLACK;
    item->custom_colors      = false;
}

/* ============================================================
 * Configuration
 * ============================================================ */

void widget_list_item_set_bounds(widget_list_item_t *item, int x, int y, int w, int h)
{
    if (!item)
        return;
    item->x = x;
    item->y = y;
    item->w = w;
    item->h = h;
}

void widget_list_item_set_label(widget_list_item_t *item, const char *label)
{
    if (!item)
        return;
    copy_str(item->label, WIDGET_LIST_ITEM_LABEL_LEN, label);
}

void widget_list_item_set_label_font(widget_list_item_t *item, const lv_font_t *font)
{
    if (!item)
        return;
    item->label_font = font;
}

void widget_list_item_set_value(widget_list_item_t *item, const char *value)
{
    if (!item)
        return;
    copy_str(item->value, WIDGET_LIST_ITEM_VALUE_LEN, value);
}

void widget_list_item_set_value_font(widget_list_item_t *item, const lv_font_t *font)
{
    if (!item)
        return;
    item->value_font = font;
}

void widget_list_item_set_icon(widget_list_item_t *item, const char *icon_code)
{
    if (!item)
        return;
    copy_str(item->icon_code, WIDGET_LIST_ITEM_ICON_LEN, icon_code);
}

void widget_list_item_set_icon_font(widget_list_item_t *item, const lv_font_t *font)
{
    if (!item)
        return;
    item->icon_font = font;
}

void widget_list_item_set_show_chevron(widget_list_item_t *item, bool show)
{
    if (!item)
        return;
    item->show_chevron = show;
}

void widget_list_item_set_show_separator(widget_list_item_t *item, bool show)
{
    if (!item)
        return;
    item->show_separator = show;
}

void widget_list_item_set_padding(widget_list_item_t *item, int padding)
{
    if (!item)
        return;
    item->padding = padding;
}

void widget_list_item_set_callback(widget_list_item_t *item, widget_list_item_callback_t cb, void *user_data)
{
    if (!item)
        return;
    item->callback           = cb;
    item->callback_user_data = user_data;
}

void widget_list_item_set_colors(widget_list_item_t *item, rawdraw_color_t bg, rawdraw_color_t text,
                                 rawdraw_color_t value_text, rawdraw_color_t separator)
{
    if (!item)
        return;
    item->bg_color         = bg;
    item->text_color       = text;
    item->value_text_color = value_text;
    item->separator_color  = separator;
    item->custom_colors    = true;
}

/* ============================================================
 * State / input
 * ============================================================ */

bool widget_list_item_contains(const widget_list_item_t *item, int px, int py)
{
    if (!item)
        return false;
    return px >= item->x && px < item->x + item->w && py >= item->y && py < item->y + item->h;
}

void widget_list_item_set_pressed(widget_list_item_t *item, bool pressed)
{
    if (!item)
        return;
    item->pressed = pressed;
}

bool widget_list_item_is_pressed(const widget_list_item_t *item)
{
    return item ? item->pressed : false;
}

void widget_list_item_handle_tap(widget_list_item_t *item)
{
    if (!item)
        return;
    item->pressed = true;
    if (item->callback) {
        item->callback(item->callback_user_data);
    }
}

bool widget_list_item_handle_input(widget_list_item_t *item, const ui_button_event_t *event)
{
    if (!item || !event)
        return false;
    /* The boot button is the confirm/select key; it activates the item. */
    if (event->type == BTN_BOOT_CLICK) {
        widget_list_item_handle_tap(item);
        return true;
    }
    return false;
}

/* ============================================================
 * Geometry / rendering
 * ============================================================ */

rawdraw_rect_t widget_list_item_get_bounds(const widget_list_item_t *item)
{
    rawdraw_rect_t r = {0, 0, 0, 0};
    if (!item)
        return r;
    r.x = item->x;
    r.y = item->y;
    r.w = item->w;
    r.h = item->h;
    return r;
}

void widget_list_item_render(const widget_list_item_t *item, uint8_t *fb, int fb_width, int fb_height)
{
    if (!item || !fb || item->w <= 0 || item->h <= 0)
        return;

    rawdraw_rect_t bounds = widget_list_item_get_bounds(item);
    bounds                = rawdraw_clamp_rect(bounds, fb_width, fb_height);
    if (rawdraw_rect_area(bounds) <= 0)
        return;

    /* Determine whether the caller is relying on theme defaults. */
    const bool default_colors = !item->custom_colors;

    rawdraw_paint_style_t row_style =
        rawdraw_theme_component(item->pressed ? ROLE_SETTINGS_SELECTED : ROLE_SETTINGS_ROW);

    /* Resolve effective colors. */
    rawdraw_color_t bg, fg, val_fg, sep;
    if (item->pressed) {
        bg     = RAWDRAW_COLOR_BLACK;
        fg     = RAWDRAW_COLOR_WHITE;
        val_fg = RAWDRAW_COLOR_WHITE;
        sep    = RAWDRAW_COLOR_WHITE;
    } else {
        bg     = item->bg_color;
        fg     = item->text_color;
        val_fg = item->value_text_color;
        sep    = item->separator_color;
    }
    if (default_colors) {
        bg     = row_style.bg;
        fg     = row_style.fg;
        val_fg = item->pressed ? row_style.fg : rawdraw_theme_color_for(THEME_TOKEN_TEXT_SECONDARY);
        sep    = row_style.border;
    }

    /* ---- background fill ---- */
    if (default_colors) {
        rawdraw_draw_styled_rect(fb, fb_width, fb_height, bounds, &row_style);
    } else {
        rawdraw_fill_rect(fb, fb_width, fb_height, bounds, bg);
    }

    /* ---- layout positions ---- */
    int cur_x    = item->x + item->padding;
    int center_y = item->y + item->h / 2;

    /* 1. Icon (left side) */
    if (item->icon_code[0] != '\0' && item->icon_font) {
        int icon_size = (int)item->icon_font->line_height;
        int icon_y    = center_y - icon_size / 2;
        rawdraw_draw_text(fb, fb_width, fb_height, cur_x, icon_y, item->icon_code, item->icon_font, fg);
        cur_x += icon_size + STYLE_LIST_ITEM_ICON_GAP;
    }

    /* 2. Label text */
    if (item->label[0] != '\0' && item->label_font) {
        int label_w = rawdraw_measure_text_width(item->label, item->label_font);
        int text_y  = center_y - (int)item->label_font->line_height / 2;
        rawdraw_draw_text(fb, fb_width, fb_height, cur_x, text_y, item->label, item->label_font, fg);
        cur_x += label_w + STYLE_SPACING_SM;
    }

    /* 3. Reserve chevron space on the right side. */
    int chevron_w = item->show_chevron ? STYLE_LIST_ITEM_CHEVRON_W : 0;

    /* 4. Value text (right-aligned, before chevron) */
    if (item->value[0] != '\0' && item->value_font) {
        int val_w  = rawdraw_measure_text_width(item->value, item->value_font);
        int val_x  = item->x + item->w - item->padding - val_w - chevron_w;
        int text_y = center_y - (int)item->value_font->line_height / 2;
        rawdraw_draw_text(fb, fb_width, fb_height, val_x, text_y, item->value, item->value_font, val_fg);
    }

    /* 5. Chevron arrow */
    if (item->show_chevron) {
        int chev_x = item->x + item->w - item->padding - 5;
        draw_chevron(fb, fb_width, fb_height, chev_x, center_y, fg);
    }

    /* 6. Separator line at bottom */
    if (item->show_separator) {
        int sep_y = item->y + item->h - 1;
        if (sep_y >= item->y && sep_y < fb_height) {
            rawdraw_draw_hline(fb, fb_width, fb_height, sep_y, item->x, item->x + item->w - 1, sep);
        }
    }
}
