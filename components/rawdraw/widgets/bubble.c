#include "../include/rawdraw_util.h"
#include "bubble.h"
#include "../include/theme.h"
#include "../include/layout.h"
#include "../include/rawdraw_ext.h"
#include <string.h>

static void apply_default_style(widget_bubble_t *bubble)
{
    if (bubble->custom_colors)
        return;

    switch (bubble->align) {
    case WIDGET_BUBBLE_ALIGN_LEFT: {
        bubble->fill_color = rawdraw_theme_color_for(THEME_TOKEN_BACKGROUND_PRIMARY);
        bubble->text_color = rawdraw_theme_color_for(THEME_TOKEN_TEXT_PRIMARY);
        bubble->border_color = rawdraw_theme_color_for(THEME_TOKEN_BORDER);
        bubble->border_width = rawdraw_theme_component(ROLE_CARD_DEFAULT).border_width;
        break;
    }
    case WIDGET_BUBBLE_ALIGN_RIGHT: {
        bubble->fill_color = rawdraw_theme_color_for(THEME_TOKEN_SELECTED);
        bubble->text_color = rawdraw_theme_component(ROLE_BUTTON_SELECTED).fg;
        bubble->border_color = rawdraw_theme_component(ROLE_BUTTON_SELECTED).border;
        bubble->border_width = 0;
        break;
    }
    case WIDGET_BUBBLE_ALIGN_CENTER: {
        bubble->fill_color = rawdraw_theme_color_for(THEME_TOKEN_BACKGROUND_PRIMARY);
        bubble->text_color = rawdraw_theme_color_for(THEME_TOKEN_TEXT_SECONDARY);
        bubble->border_color = bubble->fill_color;
        bubble->border_width = 0;
        break;
    }
    }
}

void widget_bubble_init(widget_bubble_t *bubble, widget_bubble_align_t align, int margin, int max_width, int radius)
{
    if (!bubble)
        return;
    bubble->align = align;
    bubble->margin = margin;
    bubble->max_width = max_width;
    bubble->radius = radius;
    bubble->font = NULL;
    bubble->line_spacing = 0;
    bubble->padding = 4;
    bubble->text[0] = '\0';
    bubble->y = 0;
    bubble->fill_color = RAWDRAW_COLOR_WHITE;
    bubble->text_color = RAWDRAW_COLOR_BLACK;
    bubble->border_color = RAWDRAW_COLOR_BLACK;
    bubble->border_width = 1;
    bubble->custom_colors = false;
    apply_default_style(bubble);
}

void widget_bubble_set_align(widget_bubble_t *bubble, widget_bubble_align_t align)
{
    if (!bubble)
        return;
    bubble->align = align;
    apply_default_style(bubble);
}

void widget_bubble_set_margin(widget_bubble_t *bubble, int margin)
{
    if (!bubble)
        return;
    bubble->margin = margin;
}

void widget_bubble_set_max_width(widget_bubble_t *bubble, int max_width)
{
    if (!bubble)
        return;
    bubble->max_width = max_width;
}

void widget_bubble_set_radius(widget_bubble_t *bubble, int radius)
{
    if (!bubble)
        return;
    bubble->radius = radius;
}

void widget_bubble_set_font(widget_bubble_t *bubble, const lv_font_t *font)
{
    if (!bubble)
        return;
    bubble->font = font;
}

void widget_bubble_set_line_spacing(widget_bubble_t *bubble, int spacing)
{
    if (!bubble)
        return;
    bubble->line_spacing = spacing;
}

void widget_bubble_set_padding(widget_bubble_t *bubble, int padding)
{
    if (!bubble)
        return;
    bubble->padding = padding;
}

void widget_bubble_set_colors(widget_bubble_t *bubble, rawdraw_color_t fill, rawdraw_color_t text,
                              rawdraw_color_t border, int border_width)
{
    if (!bubble)
        return;
    bubble->fill_color = fill;
    bubble->text_color = text;
    bubble->border_color = border;
    bubble->border_width = border_width;
    bubble->custom_colors = true;
}

void widget_bubble_set_text(widget_bubble_t *bubble, const char *text)
{
    if (!bubble)
        return;
    if (text) {
        strncpy(bubble->text, text, sizeof(bubble->text) - 1);
        bubble->text[sizeof(bubble->text) - 1] = '\0';
    } else {
        bubble->text[0] = '\0';
    }
}

void widget_bubble_append_text(widget_bubble_t *bubble, const char *chunk)
{
    if (!bubble || !chunk)
        return;
    size_t len = strlen(bubble->text);
    if (len < sizeof(bubble->text) - 1) {
        strncat(bubble->text, chunk, sizeof(bubble->text) - len - 1);
    }
}

void widget_bubble_clear(widget_bubble_t *bubble)
{
    if (!bubble)
        return;
    bubble->text[0] = '\0';
}

const char *widget_bubble_get_text(const widget_bubble_t *bubble)
{
    if (!bubble)
        return "";
    return bubble->text;
}

bool widget_bubble_has_content(const widget_bubble_t *bubble)
{
    return bubble && (bubble->text[0] != '\0');
}

void widget_bubble_set_y(widget_bubble_t *bubble, int y)
{
    if (!bubble)
        return;
    bubble->y = y;
}

static rawdraw_rect_t calculate_text_bounds(const widget_bubble_t *bubble)
{
    if (!bubble->font || bubble->text[0] == '\0') {
        return (rawdraw_rect_t){0, 0, 0, 0};
    }
    int max_text_w = bubble->max_width - 2 * bubble->padding;
    return rawdraw_measure_text_bounds(bubble->text, bubble->font, max_text_w);
}

int widget_bubble_calculate_height(const widget_bubble_t *bubble)
{
    if (!bubble)
        return 0;
    rawdraw_rect_t text_bounds = calculate_text_bounds(bubble);
    int line_count = 1;
    if (bubble->text[0] == '\0') {
        line_count = 0;
    } else {
        for (const char *p = bubble->text; *p; p++) {
            if (*p == '\n')
                line_count++;
        }
    }

    if (text_bounds.h > 0 && bubble->font) {
        int measured_lines = text_bounds.h / bubble->font->line_height;
        if (measured_lines > line_count)
            line_count = measured_lines;
    }

    int line_height = bubble->font ? bubble->font->line_height : 16;
    int line_step = line_height + bubble->line_spacing;
    if (line_step < 24)
        line_step = 24;

    int height =
        (line_count > 0) ? (line_step * line_count + 2 * bubble->padding) : (2 * bubble->padding + line_height);

    int min_height = line_height + 2 * bubble->padding;
    if (height < min_height)
        height = min_height;

    return height;
}

int widget_bubble_calculate_width(const widget_bubble_t *bubble)
{
    if (!bubble)
        return 0;
    rawdraw_rect_t text_bounds = calculate_text_bounds(bubble);
    int width = text_bounds.w + 2 * bubble->padding;

    int min_width = 2 * bubble->padding + 20;
    if (width < min_width) {
        width = min_width;
    }

    if (bubble->max_width > 0 && width > bubble->max_width) {
        width = bubble->max_width;
    }

    return width;
}

rawdraw_rect_t widget_bubble_get_bounds(const widget_bubble_t *bubble, int screen_width)
{
    if (!bubble)
        return (rawdraw_rect_t){0, 0, 0, 0};
    int w = widget_bubble_calculate_width(bubble);
    int h = widget_bubble_calculate_height(bubble);

    int x;
    switch (bubble->align) {
    case WIDGET_BUBBLE_ALIGN_LEFT:
        x = bubble->margin;
        break;
    case WIDGET_BUBBLE_ALIGN_RIGHT:
        x = screen_width - bubble->margin - w;
        break;
    case WIDGET_BUBBLE_ALIGN_CENTER:
        x = (screen_width - w) / 2;
        break;
    default:
        x = bubble->margin;
    }

    if (bubble->align == WIDGET_BUBBLE_ALIGN_RIGHT) {
        if (x + w > screen_width - bubble->margin) {
            x = screen_width - bubble->margin - w;
        }
    }
    if (bubble->align == WIDGET_BUBBLE_ALIGN_LEFT) {
        if (x < bubble->margin) {
            x = bubble->margin;
        }
    }
    if (bubble->align == WIDGET_BUBBLE_ALIGN_CENTER) {
        if (x + w > screen_width - bubble->margin) {
            x = screen_width - bubble->margin - w;
        }
        if (x < bubble->margin) {
            x = bubble->margin;
        }
    }

    return (rawdraw_rect_t){x, bubble->y, w, h};
}

void widget_bubble_render(widget_bubble_t *bubble, uint8_t *fb, int width, int height)
{
    if (!fb || !bubble || !widget_bubble_has_content(bubble))
        return;

    if (!bubble->custom_colors) {
        apply_default_style(bubble);
    }

    rawdraw_rect_t bounds = widget_bubble_get_bounds(bubble, width);
    bounds = rawdraw_clamp_rect(bounds, width, height);
    if (rawdraw_rect_area(bounds) <= 0)
        return;

    // === Background ===
    if (bubble->align == WIDGET_BUBBLE_ALIGN_RIGHT) {
        if (bubble->radius > 0) {
            rawdraw_draw_round_rect(fb, width, height, bounds.x, bounds.y, bounds.w, bounds.h, bubble->radius,
                                    (int)bubble->fill_color, (int)bubble->border_color, 0);
        } else {
            rawdraw_fill_rect(fb, width, height, bounds, bubble->fill_color);
        }
    } else if (bubble->border_width > 0) {
        if (bubble->radius > 0) {
            rawdraw_draw_round_rect(fb, width, height, bounds.x, bounds.y, bounds.w, bounds.h, bubble->radius,
                                    (int)bubble->fill_color, (int)bubble->border_color, bubble->border_width);
        } else {
            rawdraw_fill_rect(fb, width, height, bounds, bubble->fill_color);
            rawdraw_draw_rect_border(fb, width, height, bounds, bubble->border_width, bubble->border_color);
        }
    }

    // === Text rendering with character wrapping ===
    if (bubble->font) {
        int text_x = bounds.x + bubble->padding;
        int text_y = bounds.y + bubble->padding;

        int line_step = bubble->font->line_height + bubble->line_spacing;
        if (line_step < 24)
            line_step = 24;

        rawdraw_color_t draw_color = bubble->text_color;
        const char *p = bubble->text;
        int current_y = text_y;

        const int letter_spacing = (bubble->font->line_height + 8) / 16;
        const int space_width = bubble->font->line_height / 4;

        char line_buf[256];
        int line_idx = 0;
        int current_line_w = 0;
        int max_text_w = bubble->max_width - 2 * bubble->padding;

        while (*p && current_y < bounds.y + bounds.h) {
            const char *prev_p = p;
            uint32_t ch = utf8_next(&p);
            if (ch == 0)
                break;

            if (ch == '\n') {
                line_buf[line_idx] = '\0';
                if (line_idx > 0) {
                    rawdraw_draw_text(fb, width, height, text_x, current_y, line_buf, bubble->font, (int)draw_color);
                }
                current_y += line_step;
                line_idx = 0;
                current_line_w = 0;
                continue;
            }

            int char_w = 0;
            if (ch == ' ') {
                char_w = space_width;
            } else {
                lv_font_glyph_dsc_t g = {0};
                g.resolved_font = bubble->font;
                if (lv_font_get_glyph_dsc(bubble->font, &g, ch, 0)) {
                    if (ch >= 0x20 && ch <= 0x7E) {
                        char_w = (int)g.box_w + (int)g.ofs_x + letter_spacing;
                        if (char_w < 2)
                            char_w = 2;
                    } else {
                        char_w = g.adv_w;
                    }
                } else {
                    char_w = bubble->font->line_height / 2;
                }
            }

            if (max_text_w > 0 && current_line_w + char_w > max_text_w && current_line_w > 0) {
                // Wrap to next line
                line_buf[line_idx] = '\0';
                if (line_idx > 0) {
                    rawdraw_draw_text(fb, width, height, text_x, current_y, line_buf, bubble->font, (int)draw_color);
                }
                current_y += line_step;

                // Start new line with current character
                line_idx = 0;
                int bytes = p - prev_p;
                for (int b = 0; b < bytes && line_idx < 255; b++) {
                    line_buf[line_idx++] = prev_p[b];
                }
                current_line_w = char_w;
            } else {
                // Append character bytes
                int bytes = p - prev_p;
                if (line_idx + bytes < 255) {
                    for (int b = 0; b < bytes; b++) {
                        line_buf[line_idx++] = prev_p[b];
                    }
                    current_line_w += char_w;
                }
            }
        }

        // Draw remaining text
        if (line_idx > 0 && current_y < bounds.y + bounds.h) {
            line_buf[line_idx] = '\0';
            rawdraw_draw_text(fb, width, height, text_x, current_y, line_buf, bubble->font, (int)draw_color);
        }
    }
}
