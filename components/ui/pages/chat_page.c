/**
 * @file chat_renderer.c
 * @brief Chat page renderer — C port of C++ rawdraw::ChatRenderer.
 *
 * Flat-text chat: ">" (user) / "[AI]" (AI) prefixes, system messages
 * centered, horizontal dividers, scroll indicator, streaming dots,
 * volume dialog overlay.
 */
#include "chat_page.h"
#include "page_registry.h"

#include "rawdraw_ext.h"
#include "theme.h"
#include "style.h"
#include "layout.h"
#include "ui_text_util.h"

#include <stdio.h>
#include <string.h>

static const lv_font_t *const kChatFont      = &SourceHanSansSC_Regular_slim;
static const lv_font_t *const kChatTitleFont = &SourceHanSansSC_Medium_slim;

#define kChatContentY (STYLE_STATUS_BAR_HEIGHT + 10)
#define kChatBottomReserve 2

/* Bubble layout metrics produced by build_bubble_metrics. */
typedef struct {
    char lines[8][128];
    int  line_count;
    int  width;
    int  height;
    int  line_box_h;
    int  line_gap;
} bubble_metrics_t;

static bubble_metrics_t build_bubble_metrics(const chat_message_t *entry, const lv_font_t *font, int width)
{
    bubble_metrics_t m;
    memset(&m, 0, sizeof(m));
    const int bubble_max_w = (width * STYLE_BUBBLE_MAX_WIDTH_PCT) / 100;
    const int text_max_w   = bubble_max_w - STYLE_BUBBLE_PADDING * 2;
    ui_text_wrap_lines(font, entry->text, text_max_w, m.lines, 128, 8, &m.line_count);

    int longest   = 0;
    int max_ink_h = 0;
    for (int i = 0; i < m.line_count; ++i) {
        int w                         = rawdraw_measure_text_width(m.lines[i], font);
        longest                       = RD_MAX(longest, w);
        rawdraw_text_ink_bounds_t ink = rawdraw_layout_measure_text_ink_bounds(font, m.lines[i]);
        max_ink_h                     = RD_MAX(max_ink_h, ink.valid ? ink.height : (int)font->line_height);
    }

    m.line_box_h = RD_MAX(max_ink_h + 4, 20);
    m.line_gap   = RD_MAX(2, STYLE_BUBBLE_LINE_SPACING);
    m.height     = STYLE_BUBBLE_PADDING * 2 + m.line_count * m.line_box_h + RD_MAX(0, m.line_count - 1) * m.line_gap;
    m.width      = RD_MIN(bubble_max_w, RD_MAX(longest + STYLE_BUBBLE_PADDING * 2, 48));
    return m;
}

static rawdraw_rect_t get_bubble_rect(const chat_message_t *entry, const bubble_metrics_t *m, int width)
{
    rawdraw_rect_t r = {0, entry->y_pos, m->width, m->height};
    if (entry->role == CHAT_ROLE_USER) {
        r.x = width - STYLE_BUBBLE_MARGIN - m->width;
    } else if (entry->role == CHAT_ROLE_SYSTEM) {
        r.x = (width - m->width) / 2;
    } else {
        r.x = STYLE_BUBBLE_MARGIN;
    }
    return r;
}

/* ------------------------------------------------------------------ */
/* Rendering                                                           */
/* ------------------------------------------------------------------ */

static void draw_scroll_indicator(page_renderer_t *self, uint8_t *fb, int width, int content_y, int content_height)
{
    chat_page_t *r = (chat_page_t *)self;
    if (r->max_scroll_offset <= 0)
        return;

    const int                   bar_width   = STYLE_SCROLLBAR_WIDTH;
    const int                   bar_x       = width - bar_width - STYLE_SCROLL_MARGIN;
    const rawdraw_paint_style_t track_style = rawdraw_theme_style(THEME_TOKEN_BACKGROUND_SECONDARY);
    rawdraw_draw_styled_rect(fb, width, r->base.height, (rawdraw_rect_t){bar_x, content_y, bar_width, content_height},
                             &track_style);

    const int total_height = content_height + r->max_scroll_offset;
    int       thumb_height = (content_height * content_height) / total_height;
    if (thumb_height < STYLE_SCROLLBAR_MIN_H)
        thumb_height = STYLE_SCROLLBAR_MIN_H;

    int thumb_offset = (r->scroll_offset * content_height) / total_height;
    int thumb_y      = content_y + thumb_offset;
    if (thumb_y + thumb_height > content_y + content_height) {
        thumb_height = content_y + content_height - thumb_y;
    }

    rawdraw_draw_round_rect(fb, width, r->base.height, bar_x, thumb_y, bar_width, thumb_height, STYLE_BORDER_RADIUS_SM,
                            rawdraw_theme_color_for(THEME_TOKEN_SELECTED),
                            rawdraw_theme_color_for(THEME_TOKEN_SELECTED), 0);
}

static void draw_streaming_indicator(page_renderer_t *self, uint8_t *fb, int width, int content_bottom)
{
    chat_page_t *r           = (chat_page_t *)self;
    const int    indicator_y = content_bottom - STYLE_SPACING_XL;
    const int    padding     = STYLE_SPACING_SM;

    r->stream_frame++;
    const int dot_count = (r->stream_frame / 10) % 3 + 1;
    char      dots[8];
    dots[0] = '.';
    dots[1] = dot_count > 1 ? '.' : ' ';
    dots[2] = dot_count > 2 ? '.' : ' ';
    dots[3] = '\0';

    char buf[64];
    snprintf(buf, sizeof(buf), "思考中%s", dots);

    int text_w = rawdraw_measure_text_width(buf, r->font);
    int pill_w = text_w + padding * 2;
    int pill_h = r->font->line_height + padding;
    int pill_x = (width - pill_w) / 2;
    int pill_y = indicator_y;

    const rawdraw_paint_style_t pill_style = rawdraw_theme_style(THEME_TOKEN_BADGE);
    rawdraw_draw_styled_round_rect(fb, width, r->base.height, (rawdraw_rect_t){pill_x, pill_y, pill_w, pill_h},
                                   STYLE_BORDER_RADIUS_PILL, &pill_style);

    int text_x = pill_x + padding;
    rawdraw_draw_text(fb, width, r->base.height, text_x,
                      rawdraw_layout_ink_centered_text_top_y_in_box(r->font, buf, pill_y, pill_h, 0), buf, r->font,
                      pill_style.fg);
}

static void layout_messages(page_renderer_t *self)
{
    chat_page_t *r = (chat_page_t *)self;
    if (r->message_count == 0) {
        r->max_scroll_offset = 0;
        return;
    }

    int y = 0;
    for (int i = 0; i < r->message_count; ++i) {
        chat_message_t *entry = &r->messages[i];
        if (entry->text[0] == '\0') {
            entry->y_pos   = y;
            entry->block_h = 0;
            continue;
        }
        bubble_metrics_t m      = build_bubble_metrics(entry, r->font, r->base.width);
        rawdraw_rect_t   bubble = get_bubble_rect(entry, &m, r->base.width);
        entry->y_pos            = y;
        entry->block_h          = bubble.h + STYLE_BUBBLE_GAP;
        y += entry->block_h;
    }

    const int total_height   = y;
    const int visible_height = RD_MAX(40, r->base.height - kChatBottomReserve - kChatContentY);
    r->max_scroll_offset     = total_height - visible_height;
    if (r->max_scroll_offset < 0)
        r->max_scroll_offset = 0;

    if (total_height <= visible_height && (r->follow_latest || r->is_streaming)) {
        const int offset_y = visible_height - total_height;
        for (int i = 0; i < r->message_count; ++i) {
            r->messages[i].y_pos += offset_y;
        }
        r->max_scroll_offset = 0;
    }

    if (r->follow_latest || r->is_streaming) {
        r->scroll_offset = r->max_scroll_offset;
    } else if (r->scroll_offset > r->max_scroll_offset) {
        r->scroll_offset = r->max_scroll_offset;
    }
}

static void render_volume_dialog(page_renderer_t *self, uint8_t *fb, int width, int height)
{
    chat_page_t *r = (chat_page_t *)self;
    if (!r->showing_volume_dialog)
        return;
    const rawdraw_paint_style_t bg_style       = rawdraw_theme_style(THEME_TOKEN_BACKGROUND_PRIMARY);
    const rawdraw_paint_style_t modal_style    = rawdraw_theme_component(ROLE_MODAL);
    const rawdraw_paint_style_t shadow_style   = rawdraw_theme_style(THEME_TOKEN_SHADOW);
    const rawdraw_paint_style_t progress_style = rawdraw_theme_component(ROLE_PROGRESS);
    const rawdraw_color_t       text           = rawdraw_theme_color_for(THEME_TOKEN_TEXT_PRIMARY);
    const rawdraw_color_t       border         = rawdraw_theme_color_for(THEME_TOKEN_BORDER);

    const int dialog_w = STYLE_DIALOG_W;
    const int dialog_h = STYLE_DIALOG_H_MD;
    const int dialog_x = (width - dialog_w) / 2;
    const int dialog_y = (height - dialog_h) / 2;
    const int inner_x  = dialog_x + 18;
    const int inner_w  = dialog_w - 36;

    rawdraw_draw_styled_round_rect(fb, width, height,
                                   (rawdraw_rect_t){dialog_x - 4, dialog_y - 4, dialog_w + 8, dialog_h + 8},
                                   STYLE_BORDER_RADIUS_LG, &bg_style);
    rawdraw_draw_styled_round_rect(fb, width, height, (rawdraw_rect_t){dialog_x + 2, dialog_y + 2, dialog_w, dialog_h},
                                   STYLE_BORDER_RADIUS_LG, &shadow_style);
    rawdraw_draw_styled_round_rect(fb, width, height, (rawdraw_rect_t){dialog_x, dialog_y, dialog_w, dialog_h},
                                   STYLE_BORDER_RADIUS_LG, &modal_style);

    const char *title   = "音量调整";
    const int   title_w = rawdraw_measure_text_width(title, r->font);
    rawdraw_draw_text(fb, width, height, dialog_x + (dialog_w - title_w) / 2,
                      rawdraw_layout_ink_centered_text_top_y(r->font, title, dialog_y + 24, 0), title, r->font, text);
    rawdraw_draw_hline(fb, width, height, dialog_y + 42, dialog_x + 14, dialog_x + dialog_w - 14, border);

    char volume_buf[16];
    snprintf(volume_buf, sizeof(volume_buf), "%d%%", r->volume_dialog_value);
    const int value_w = rawdraw_measure_text_width(volume_buf, r->title_font);
    rawdraw_draw_text(fb, width, height, dialog_x + (dialog_w - value_w) / 2,
                      rawdraw_layout_ink_centered_text_top_y(r->title_font, volume_buf, dialog_y + 70, 0), volume_buf,
                      r->title_font, text);

    const int track_x = inner_x;
    const int track_y = dialog_y + 102;
    const int track_w = inner_w;
    const int track_h = 16;
    rawdraw_draw_styled_round_rect(fb, width, height, (rawdraw_rect_t){track_x, track_y, track_w, track_h},
                                   STYLE_BORDER_RADIUS_PILL, &progress_style);

    int fill_w = (track_w - 4) * r->volume_dialog_value / 100;
    if (fill_w > 0) {
        rawdraw_fill_rect(fb, width, height, (rawdraw_rect_t){track_x + 2, track_y + 2, fill_w, track_h - 4},
                          progress_style.fg);
    }

    for (int i = 0; i <= 4; ++i) {
        const int tick_x = track_x + 2 + (track_w - 4) * i / 4;
        rawdraw_draw_vline(fb, width, height, tick_x, track_y + track_h + 4, track_y + track_h + 8, border);
    }

    const int hint_center_y = dialog_y + dialog_h - 20;
    rawdraw_draw_text(fb, width, height, inner_x + 6,
                      rawdraw_layout_ink_centered_text_top_y(r->font, "UP/DN 调整  BOOT 保存", hint_center_y, 0),
                      "UP/DN 调整  BOOT 保存", r->font, rawdraw_theme_color_for(THEME_TOKEN_TEXT_SECONDARY));
}

/* ------------------------------------------------------------------ */
/* PageRenderer vtable                                                 */
/* ------------------------------------------------------------------ */

void chat_page_init(page_renderer_t *self, int width, int height)
{
    chat_page_t *r                  = (chat_page_t *)self;
    r->base.width                   = width;
    r->base.height                  = height;
    r->base.needs_full_refresh_flag = true;
    r->scroll_offset                = 0;
    r->max_scroll_offset            = 0;
    r->follow_latest                = true;
    r->message_count                = 0;
    r->is_streaming                 = false;
    r->is_listening                 = false;
    r->stream_frame                 = 0;
    r->showing_volume_dialog        = false;
    r->volume_dialog_value          = 70;
    r->font                         = kChatFont;
    r->title_font                   = kChatTitleFont;
    r->volume_dialog_handler        = NULL;
    r->volume_dialog_ctx            = NULL;
    r->bottom_status_text[0]        = '\0';
}

/* Page gained focus: request a redraw but keep the message history. */
static void chat_page_enter(page_renderer_t *self)
{
    chat_page_t *r = (chat_page_t *)self;
    if (!r)
        return;
    r->base.needs_full_refresh_flag = true;
}

void chat_page_render(page_renderer_t *self, uint8_t *fb, int width, int height)
{
    chat_page_t *r = (chat_page_t *)self;
    if (!fb)
        return;
    const rawdraw_paint_style_t bg_style     = rawdraw_theme_style(THEME_TOKEN_BACKGROUND_PRIMARY);
    const rawdraw_paint_style_t card_style   = rawdraw_theme_component(ROLE_CARD_DEFAULT);
    const rawdraw_paint_style_t user_style   = rawdraw_theme_component(ROLE_TODO_SELECTED);
    const rawdraw_paint_style_t system_style = rawdraw_theme_style(THEME_TOKEN_TEXT_SECONDARY);
    const rawdraw_color_t       text         = rawdraw_theme_color_for(THEME_TOKEN_TEXT_PRIMARY);

    const int content_y      = kChatContentY;
    const int content_bottom = height - kChatBottomReserve;
    const int content_height = RD_MAX(40, content_bottom - content_y);

    rawdraw_draw_styled_rect(fb, width, height,
                             (rawdraw_rect_t){0, STYLE_STATUS_BAR_HEIGHT, width, height - STYLE_STATUS_BAR_HEIGHT},
                             &bg_style);

    layout_messages(self);

    for (int i = 0; i < r->message_count; ++i) {
        const chat_message_t *entry = &r->messages[i];
        if (entry->text[0] == '\0')
            continue;
        bubble_metrics_t m      = build_bubble_metrics(entry, r->font, width);
        rawdraw_rect_t   bubble = get_bubble_rect(entry, &m, width);
        bubble.y                = entry->y_pos - r->scroll_offset + content_y;

        if (bubble.y + bubble.h < content_y)
            continue;
        if (bubble.y > content_bottom)
            continue;

        const int visible_y      = RD_MAX(bubble.y, content_y);
        const int visible_bottom = RD_MIN(bubble.y + bubble.h, content_bottom);
        const int visible_h      = visible_bottom - visible_y;
        if (visible_h <= 0)
            continue;

        if (entry->role == CHAT_ROLE_SYSTEM) {
            for (int li = 0; li < m.line_count; ++li) {
                const int line_box_y = bubble.y + STYLE_BUBBLE_PADDING + li * (m.line_box_h + m.line_gap);
                if (line_box_y + m.line_box_h >= content_y && line_box_y <= content_bottom) {
                    int text_w = rawdraw_measure_text_width(m.lines[li], r->font);
                    int text_x = (width - text_w) / 2;
                    rawdraw_draw_text(fb, width, height, text_x,
                                      rawdraw_layout_ink_centered_text_top_y_in_box(r->font, m.lines[li], line_box_y,
                                                                                    m.line_box_h, 0),
                                      m.lines[li], r->font, system_style.fg);
                }
            }
        } else if (entry->role == CHAT_ROLE_USER) {
            rawdraw_draw_styled_round_rect(fb, width, height,
                                           (rawdraw_rect_t){bubble.x, visible_y, bubble.w, visible_h},
                                           STYLE_BUBBLE_RADIUS, &user_style);
            for (int li = 0; li < m.line_count; ++li) {
                const int line_box_y = bubble.y + STYLE_BUBBLE_PADDING + li * (m.line_box_h + m.line_gap);
                if (line_box_y + m.line_box_h >= content_y && line_box_y <= content_bottom) {
                    rawdraw_draw_text(fb, width, height, bubble.x + STYLE_BUBBLE_PADDING,
                                      rawdraw_layout_ink_centered_text_top_y_in_box(r->font, m.lines[li], line_box_y,
                                                                                    m.line_box_h, 0),
                                      m.lines[li], r->font, user_style.fg);
                }
            }
        } else {
            rawdraw_draw_styled_round_rect(fb, width, height,
                                           (rawdraw_rect_t){bubble.x, visible_y, bubble.w, visible_h},
                                           STYLE_BUBBLE_RADIUS, &card_style);
            for (int li = 0; li < m.line_count; ++li) {
                const int line_box_y = bubble.y + STYLE_BUBBLE_PADDING + li * (m.line_box_h + m.line_gap);
                if (line_box_y + m.line_box_h >= content_y && line_box_y <= content_bottom) {
                    rawdraw_draw_text(fb, width, height, bubble.x + STYLE_BUBBLE_PADDING,
                                      rawdraw_layout_ink_centered_text_top_y_in_box(r->font, m.lines[li], line_box_y,
                                                                                    m.line_box_h, 0),
                                      m.lines[li], r->font, text);
                }
            }
        }
    }

    draw_scroll_indicator(self, fb, width, content_y, content_height);
    if (r->is_streaming) {
        draw_streaming_indicator(self, fb, width, content_bottom);
    }

    if (r->is_listening || r->bottom_status_text[0] != '\0') {
        const char *status_text = r->bottom_status_text[0] != '\0' ? r->bottom_status_text : "正在聆听...";
        const int   indicator_y = content_bottom - STYLE_SPACING_XL;
        const int   padding     = STYLE_SPACING_SM;
        int text_w = rawdraw_measure_text_width(status_text, r->font);
        int pill_w = text_w + padding * 2;
        int pill_h = r->font->line_height + padding;
        int pill_x = (width - pill_w) / 2;
        int pill_y = indicator_y;

        const rawdraw_paint_style_t pill_style = rawdraw_theme_style(THEME_TOKEN_BADGE);
        rawdraw_draw_styled_round_rect(fb, width, r->base.height, (rawdraw_rect_t){pill_x, pill_y, pill_w, pill_h},
                                       STYLE_BORDER_RADIUS_PILL, &pill_style);

        int text_x = pill_x + padding;
        rawdraw_draw_text(fb, width, r->base.height, text_x,
                          rawdraw_layout_ink_centered_text_top_y_in_box(r->font, status_text, pill_y, pill_h, 0),
                          status_text, r->font, pill_style.fg);
    }
    if (r->showing_volume_dialog) {
        render_volume_dialog(self, fb, width, height);
    }

    r->base.needs_full_refresh_flag = false;
}

bool chat_page_handle_input(page_renderer_t *self, const ui_button_event_t *event)
{
    chat_page_t *r = (chat_page_t *)self;
    if (r->showing_volume_dialog) {
        switch (event->type) {
        case BTN_UP_CLICK:
            chat_page_show_volume_dialog(self, r->volume_dialog_value + 10);
            if (r->volume_dialog_handler)
                r->volume_dialog_handler(r->volume_dialog_value, false, r->volume_dialog_ctx);
            r->base.needs_full_refresh_flag = true;
            return true;
        case BTN_DOWN_CLICK:
            chat_page_show_volume_dialog(self, r->volume_dialog_value - 10);
            if (r->volume_dialog_handler)
                r->volume_dialog_handler(r->volume_dialog_value, false, r->volume_dialog_ctx);
            r->base.needs_full_refresh_flag = true;
            return true;
        case BTN_UP_LONG_PRESS:
            r->volume_dialog_value = 100;
            if (r->volume_dialog_handler)
                r->volume_dialog_handler(r->volume_dialog_value, false, r->volume_dialog_ctx);
            r->base.needs_full_refresh_flag = true;
            return true;
        case BTN_DOWN_LONG_PRESS:
            r->volume_dialog_value = 0;
            if (r->volume_dialog_handler)
                r->volume_dialog_handler(r->volume_dialog_value, false, r->volume_dialog_ctx);
            r->base.needs_full_refresh_flag = true;
            return true;
        case BTN_BOOT_CLICK:
            if (r->volume_dialog_handler)
                r->volume_dialog_handler(r->volume_dialog_value, true, r->volume_dialog_ctx);
            r->showing_volume_dialog        = false;
            r->base.needs_full_refresh_flag = true;
            return true;
        default:
            return true; /* consume all input while dialog open */
        }
    }

    switch (event->type) {
    case BTN_UP_CLICK:
        if (r->scroll_offset > 0) {
            r->scroll_offset -= STYLE_SPACING_XL * 2;
            if (r->scroll_offset < 0)
                r->scroll_offset = 0;
            r->follow_latest                = (r->scroll_offset >= r->max_scroll_offset);
            r->base.needs_full_refresh_flag = true;
            return true;
        }
        break;
    case BTN_DOWN_CLICK:
        if (r->scroll_offset < r->max_scroll_offset) {
            r->scroll_offset += STYLE_SPACING_XL * 2;
            if (r->scroll_offset > r->max_scroll_offset) {
                r->scroll_offset = r->max_scroll_offset;
            }
            r->follow_latest                = (r->scroll_offset >= r->max_scroll_offset);
            r->base.needs_full_refresh_flag = true;
            return true;
        }
        break;
    case BTN_UP_LONG_PRESS:
        r->scroll_offset                = 0;
        r->follow_latest                = false;
        r->base.needs_full_refresh_flag = true;
        return true;
    case BTN_DOWN_LONG_PRESS:
        r->scroll_offset                = r->max_scroll_offset;
        r->follow_latest                = true;
        r->base.needs_full_refresh_flag = true;
        return true;
    case BTN_BOOT_CLICK:
        r->showing_volume_dialog        = true;
        r->base.needs_full_refresh_flag = true;
        return true;
    default:
        break;
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* Streaming vtable                                                    */
/* ------------------------------------------------------------------ */

bool chat_page_append_text(page_renderer_t *self, const char *chunk)
{
    chat_page_t *r = (chat_page_t *)self;
    if (!chunk || !r->is_streaming)
        return false;
    if (r->message_count == 0)
        return false;

    chat_message_t *last = &r->messages[r->message_count - 1];
    if (last->role != CHAT_ROLE_AI)
        return false;

    strncat(last->text, chunk, CHAT_MSG_TEXT_LEN - strlen(last->text) - 1);
    last->text[CHAT_MSG_TEXT_LEN - 1] = '\0';
    r->base.needs_full_refresh_flag   = true;

    r->follow_latest = true;
    layout_messages(self);
    r->scroll_offset = r->max_scroll_offset;
    return true;
}

void chat_page_begin_stream(page_renderer_t *self)
{
    chat_page_t *r   = (chat_page_t *)self;
    r->is_streaming  = true;
    r->stream_frame  = 0;
    r->follow_latest = true;
    if (r->message_count < CHAT_MAX_MESSAGES) {
        chat_message_t *m = &r->messages[r->message_count++];
        memset(m, 0, sizeof(*m));
        m->role = CHAT_ROLE_AI;
    }
    r->base.needs_full_refresh_flag = true;
}

void chat_page_end_stream(page_renderer_t *self)
{
    chat_page_t *r  = (chat_page_t *)self;
    r->is_streaming = false;
    if (r->message_count > 0) {
        chat_message_t *last = &r->messages[r->message_count - 1];
        if (last->role == CHAT_ROLE_AI && last->text[0] == '\0') {
            --r->message_count;
        }
    }
    r->follow_latest                = true;
    r->base.needs_full_refresh_flag = true;
}

/* ------------------------------------------------------------------ */
/* Data interface                                                      */
/* ------------------------------------------------------------------ */

void chat_page_clear(page_renderer_t *self)
{
    chat_page_t *r                  = (chat_page_t *)self;
    r->message_count                = 0;
    r->scroll_offset                = 0;
    r->max_scroll_offset            = 0;
    r->is_streaming                 = false;
    r->follow_latest                = true;
    r->base.needs_full_refresh_flag = true;
}

void chat_page_add_message(page_renderer_t *self, const char *text, chat_role_t role)
{
    chat_page_t *r = (chat_page_t *)self;
    if (!text)
        return;
    if (r->message_count >= CHAT_MAX_MESSAGES)
        return;
    chat_message_t *m = &r->messages[r->message_count++];
    memset(m, 0, sizeof(*m));
    strncpy(m->text, text, CHAT_MSG_TEXT_LEN - 1);
    m->text[CHAT_MSG_TEXT_LEN - 1] = '\0';
    m->role                        = role;
    if (r->message_count <= 1) {
        r->follow_latest = true;
    }
    r->base.needs_full_refresh_flag = true;
}

void chat_page_show_status(page_renderer_t *self, const char *status, chat_role_t role)
{
    chat_page_t *r = (chat_page_t *)self;
    if (!status)
        return;
    if (r->message_count > 0) {
        chat_message_t *last = &r->messages[r->message_count - 1];
        if (last->role == role && strcmp(last->text, status) == 0) {
            strncpy(r->bottom_status_text, status, sizeof(r->bottom_status_text) - 1);
            r->bottom_status_text[sizeof(r->bottom_status_text) - 1] = '\0';
            r->base.needs_full_refresh_flag                          = true;
            return;
        }
    }
    chat_page_add_message(self, status, role);
    strncpy(r->bottom_status_text, status, sizeof(r->bottom_status_text) - 1);
    r->bottom_status_text[sizeof(r->bottom_status_text) - 1] = '\0';
    r->base.needs_full_refresh_flag                          = true;
}

void chat_page_hide_status(page_renderer_t *self)
{
    chat_page_t *r                  = (chat_page_t *)self;
    r->base.needs_full_refresh_flag = true;
}

void chat_page_set_listening(page_renderer_t *self, bool listening)
{
    chat_page_t *r  = (chat_page_t *)self;
    r->is_listening = listening;
    if (listening) {
        strcpy(r->bottom_status_text, "正在聆听...");
    } else {
        r->bottom_status_text[0] = '\0';
    }
    r->base.needs_full_refresh_flag = true;
}

void chat_page_set_bottom_status(page_renderer_t *self, const char *status)
{
    chat_page_t *r = (chat_page_t *)self;
    if (status) {
        strncpy(r->bottom_status_text, status, sizeof(r->bottom_status_text) - 1);
        r->bottom_status_text[sizeof(r->bottom_status_text) - 1] = '\0';
    } else {
        r->bottom_status_text[0] = '\0';
    }
    r->base.needs_full_refresh_flag = true;
}

int chat_page_get_message_count(const page_renderer_t *self)
{
    const chat_page_t *r = (const chat_page_t *)self;
    return r->message_count;
}

void chat_page_show_volume_dialog(page_renderer_t *self, int volume)
{
    chat_page_t *r                  = (chat_page_t *)self;
    r->volume_dialog_value          = RD_CLAMP(volume, 0, 100);
    r->showing_volume_dialog        = true;
    r->base.needs_full_refresh_flag = true;
}

void chat_page_set_volume_dialog_handler(page_renderer_t *self, void (*handler)(int, bool, void *), void *ctx)
{
    chat_page_t *r           = (chat_page_t *)self;
    r->volume_dialog_handler = handler;
    r->volume_dialog_ctx     = ctx;
}

bool chat_page_is_volume_dialog_showing(const page_renderer_t *self)
{
    const chat_page_t *r = (const chat_page_t *)self;
    return r->showing_volume_dialog;
}

void chat_page_hide_volume_dialog(page_renderer_t *self)
{
    chat_page_t *r                  = (chat_page_t *)self;
    r->showing_volume_dialog        = false;
    r->base.needs_full_refresh_flag = true;
}

/* ------------------------------------------------------------------ */
/* vtable instance                                                     */
/* ------------------------------------------------------------------ */

EXT_RAM_BSS_ATTR chat_page_t s_chat_instance;

const page_renderer_ops_t chat_page_ops = {
    .init                    = chat_page_init,
    .enter                   = chat_page_enter,
    .render                  = chat_page_render,
    .handle_input            = chat_page_handle_input,
    .get_dirty_rect          = NULL,
    .needs_full_refresh      = NULL,
    .mark_full_refresh       = NULL,
    .clear_full_refresh_flag = NULL,
    .append_text             = chat_page_append_text,
    .begin_stream            = chat_page_begin_stream,
    .end_stream              = chat_page_end_stream,
};

PAGE_REGISTER(UI_PAGE_CHAT, "对话", NULL, false, 999, &chat_page_ops, &s_chat_instance.base);
