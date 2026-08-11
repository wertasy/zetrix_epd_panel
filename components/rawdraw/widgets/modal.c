#include "../include/rawdraw_util.h"
#include "modal.h"
#include "../include/theme.h"
#include "../include/layout.h"
#include "../include/rawdraw_ext.h"
#include <string.h>

void widget_modal_init(widget_modal_t *modal)
{
    if (!modal)
        return;
    modal->bounds.x     = STYLE_MODAL_INSET;
    modal->bounds.y     = 44;
    modal->bounds.w     = STYLE_SCREEN_WIDTH - STYLE_MODAL_INSET * 2;
    modal->bounds.h     = STYLE_SCREEN_HEIGHT - 88;
    modal->title[0]     = '\0';
    modal->footer[0]    = '\0';
    modal->title_font   = &BUILTIN_TEXT_FONT;
    modal->radius       = STYLE_BORDER_RADIUS_LG;
    modal->border_width = STYLE_BORDER_THIN;
}

void widget_modal_set_bounds(widget_modal_t *modal, rawdraw_rect_t bounds)
{
    if (!modal)
        return;
    modal->bounds = bounds;
}

void widget_modal_set_bounds_xy(widget_modal_t *modal, int x, int y, int w, int h)
{
    if (!modal)
        return;
    modal->bounds.x = x;
    modal->bounds.y = y;
    modal->bounds.w = w;
    modal->bounds.h = h;
}

void widget_modal_center_in_screen(widget_modal_t *modal, int screen_width, int screen_height, int inset)
{
    if (!modal)
        return;
    modal->bounds.x = inset;
    modal->bounds.y = inset + 8;
    modal->bounds.w = screen_width - inset * 2;
    modal->bounds.h = screen_height - inset * 2 - 16;
}

void widget_modal_set_title(widget_modal_t *modal, const char *title)
{
    if (!modal)
        return;
    if (title) {
        strncpy(modal->title, title, sizeof(modal->title) - 1);
        modal->title[sizeof(modal->title) - 1] = '\0';
    } else {
        modal->title[0] = '\0';
    }
}

void widget_modal_set_title_font(widget_modal_t *modal, const lv_font_t *font)
{
    if (!modal)
        return;
    modal->title_font = font;
}

void widget_modal_set_footer(widget_modal_t *modal, const char *footer)
{
    if (!modal)
        return;
    if (footer) {
        strncpy(modal->footer, footer, sizeof(modal->footer) - 1);
        modal->footer[sizeof(modal->footer) - 1] = '\0';
    } else {
        modal->footer[0] = '\0';
    }
}

void widget_modal_set_radius(widget_modal_t *modal, int radius)
{
    if (!modal)
        return;
    modal->radius = radius;
}

void widget_modal_set_border_width(widget_modal_t *modal, int border_width)
{
    if (!modal)
        return;
    modal->border_width = border_width;
}

rawdraw_rect_t widget_modal_get_bounds(const widget_modal_t *modal)
{
    if (!modal)
        return (rawdraw_rect_t){0, 0, 0, 0};
    return modal->bounds;
}

rawdraw_rect_t widget_modal_get_title_bounds(const widget_modal_t *modal)
{
    if (!modal)
        return (rawdraw_rect_t){0, 0, 0, 0};
    return (rawdraw_rect_t){modal->bounds.x, modal->bounds.y, modal->bounds.w, STYLE_MODAL_TITLE_HEIGHT};
}

rawdraw_rect_t widget_modal_get_content_bounds(const widget_modal_t *modal)
{
    if (!modal)
        return (rawdraw_rect_t){0, 0, 0, 0};
    int title_h  = (modal->title[0] != '\0') ? STYLE_MODAL_TITLE_HEIGHT : 0;
    int footer_h = (modal->footer[0] != '\0') ? STYLE_MODAL_FOOTER_HEIGHT : 0;
    return (rawdraw_rect_t){modal->bounds.x + STYLE_CARD_PADDING, modal->bounds.y + title_h + STYLE_CARD_PADDING,
                            modal->bounds.w - STYLE_CARD_PADDING * 2,
                            modal->bounds.h - title_h - footer_h - STYLE_CARD_PADDING * 2};
}

rawdraw_rect_t widget_modal_get_footer_bounds(const widget_modal_t *modal)
{
    if (!modal)
        return (rawdraw_rect_t){0, 0, 0, 0};
    return (rawdraw_rect_t){modal->bounds.x, modal->bounds.y + modal->bounds.h - STYLE_MODAL_FOOTER_HEIGHT,
                            modal->bounds.w, STYLE_MODAL_FOOTER_HEIGHT};
}

void widget_modal_render(const widget_modal_t *modal, uint8_t *fb, int width, int height)
{
    if (!modal || !fb)
        return;

    rawdraw_rect_t bounds = rawdraw_clamp_rect(modal->bounds, width, height);
    if (bounds.w <= 0 || bounds.h <= 0)
        return;

    rawdraw_paint_style_t modal_style = rawdraw_theme_component(ROLE_MODAL);
    modal_style.border_width          = modal->border_width;

    rawdraw_draw_styled_round_rect(fb, width, height, bounds, modal->radius, &modal_style);

    if (modal->title[0] != '\0') {
        rawdraw_rect_t title_bounds = widget_modal_get_title_bounds(modal);
        rawdraw_draw_styled_round_rect(fb, width, height, title_bounds, modal->radius, &modal_style);
        rawdraw_draw_hline(fb, width, height, title_bounds.y + title_bounds.h - 1, title_bounds.x,
                           title_bounds.x + title_bounds.w - 1, modal_style.border);

        int text_y = rawdraw_layout_ink_centered_text_top_y_in_box(modal->title_font, modal->title, title_bounds.y,
                                                                   title_bounds.h, 0);
        int text_w = rawdraw_measure_text_width(modal->title, modal->title_font);
        int text_x = title_bounds.x + RD_MAX(0, (title_bounds.w - text_w) / 2);
        rawdraw_draw_styled_text(fb, width, height, text_x, text_y, modal->title, modal->title_font, &modal_style);
    }

    if (modal->footer[0] != '\0') {
        rawdraw_rect_t footer_bounds = widget_modal_get_footer_bounds(modal);
        rawdraw_draw_hline(fb, width, height, footer_bounds.y, footer_bounds.x, footer_bounds.x + footer_bounds.w - 1,
                           modal_style.border);

        int text_y = rawdraw_layout_ink_centered_text_top_y_in_box(modal->title_font, modal->footer, footer_bounds.y,
                                                                   footer_bounds.h, 0);
        int text_w = rawdraw_measure_text_width(modal->footer, modal->title_font);
        int text_x = footer_bounds.x + RD_MAX(0, (footer_bounds.w - text_w) / 2);
        rawdraw_draw_styled_text(fb, width, height, text_x, text_y, modal->footer, modal->title_font, &modal_style);
    }
}
