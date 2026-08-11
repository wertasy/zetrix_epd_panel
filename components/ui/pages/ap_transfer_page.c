/**
 * @file ap_transfer_page.c
 * @brief AP transfer mode renderer — C port of C++
 *        rawdraw::ApTransferRenderer.
 *
 * Display when the user enters AP transfer mode from the photo gallery:
 *  - WiFi AP connection instructions (SSID / password / 192.168.4.1 URL)
 *  - Live status during image upload (uploading / processing / complete)
 */
#include "ap_transfer_page.h"
#include "page_registry.h"

#include "rawdraw_ext.h"
#include "theme.h"
#include "style.h"
#include "layout.h"

#include <stdio.h>
#include <string.h>
#include <esp_log.h>

#define TAG "ApTransferPage"

#define AP_TRANSFER_DEFAULT_AP_IP "192.168.4.1"

static const lv_font_t *const kApTransferFont      = &SourceHanSansSC_Regular_slim;
static const lv_font_t *const kApTransferTitleFont = &SourceHanSansSC_Medium_slim;

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static bool looks_like_ipv4(const char *value)
{
    int dots   = 0;
    int digits = 0;
    if (!value)
        return false;
    for (const char *p = value; *p; ++p) {
        const char ch = *p;
        if (ch >= '0' && ch <= '9') {
            digits++;
        } else if (ch == '.') {
            dots++;
        } else {
            return false;
        }
    }
    return dots == 3 && digits >= 4;
}

/* ------------------------------------------------------------------ */
/* Instruction / status rendering                                      */
/* ------------------------------------------------------------------ */

static void ap_transfer_page_render_instructions(ap_transfer_page_t *r, uint8_t *fb, int width, int height)
{
    const rawdraw_color_t text         = rawdraw_theme_color_for(THEME_TOKEN_TEXT_PRIMARY);
    const rawdraw_color_t secondary    = rawdraw_theme_color_for(THEME_TOKEN_TEXT_SECONDARY);
    const rawdraw_color_t accent       = rawdraw_theme_color_for(THEME_TOKEN_ACCENT);
    const rawdraw_color_t border       = rawdraw_theme_color_for(THEME_TOKEN_BORDER);
    const int             content_top  = 35;
    const int             line_spacing = 28;
    const int             left_margin  = 20;

    int y = content_top;

    /* WiFi icon area */
    rawdraw_draw_rect_border(fb, width, height, (rawdraw_rect_t){left_margin, y, 60, 60}, 2, accent);

    /* WiFi signal bars inside */
    const int bar_x     = left_margin + 10;
    const int bar_y     = y + 30;
    const int bar_w     = 8;
    const int bar_gap   = 4;
    const int heights[] = {8, 16, 24, 32};
    for (int i = 0; i < 4; i++) {
        rawdraw_draw_rect(fb, width, height, bar_x + i * (bar_w + bar_gap), bar_y - heights[i], bar_w, heights[i],
                          accent);
    }

    y += 70;

    /* Always show the browser URL. The AP/HTTP startup callback can arrive
     * while the e-paper is busy, so relying on status_message_ made the
     * address occasionally disappear and only show "启动中...". */
    const char *ip = looks_like_ipv4(r->status_message) ? r->status_message : AP_TRANSFER_DEFAULT_AP_IP;
    char        url_buf[AP_TRANSFER_MSG_LEN + 16];
    const char *url;
    if (r->url_text[0] != '\0') {
        url = r->url_text;
    } else {
        snprintf(url_buf, sizeof(url_buf), "http://%s", ip);
        url = url_buf;
    }
    ESP_LOGI(TAG, "RenderInstructions ip=%s state=%d message='%s'", ip, (int)r->state, r->status_message);

    char state_hint[AP_TRANSFER_MSG_LEN];
    if (r->hint_text[0] != '\0') {
        strncpy(state_hint, r->hint_text, sizeof(state_hint) - 1);
        state_hint[sizeof(state_hint) - 1] = '\0';
    } else if (r->status_message[0] == '\0' || looks_like_ipv4(r->status_message)) {
        strcpy(state_hint, "启动中，可先连接热点");
    } else {
        strncpy(state_hint, r->status_message, sizeof(state_hint) - 1);
        state_hint[sizeof(state_hint) - 1] = '\0';
    }

    char        ssid_line[AP_TRANSFER_TEXT_LEN + 8];
    const char *ssid = r->ssid_text[0] != '\0' ? r->ssid_text : "InkScreen-AP";
    snprintf(ssid_line, sizeof(ssid_line), "连接 %s", ssid);

    char pwd_line[AP_TRANSFER_TEXT_LEN + 8];
    if (r->password_text[0] == '\0') {
        snprintf(pwd_line, sizeof(pwd_line), "密码: 无");
    } else {
        snprintf(pwd_line, sizeof(pwd_line), "密码: %s", r->password_text);
    }

    const char *lines[] = {
        ssid_line, pwd_line, "", "浏览器访问", url, state_hint,
    };

    for (int i = 0; i < 6; i++) {
        const char *line = lines[i];
        if (line[0] != '\0') {
            /* Highlight URL */
            const bool       is_url   = (strncmp(line, "http://", 7) == 0);
            const lv_font_t *use_font = is_url ? r->title_font : r->font;
            rawdraw_draw_text(fb, width, height, left_margin + 75,
                              rawdraw_layout_ink_centered_text_top_y(use_font, line, y, 0), line, use_font,
                              is_url ? accent : text);
        }
        y += line_spacing;
    }

    /* Bottom hint */
    const int bottom_y = height - 30;
    rawdraw_draw_hline(fb, width, height, bottom_y - 10, 20, width - 20, border);
    const char *exit_hint = r->exit_hint_text[0] == '\0' ? "长按 BOOT 退出" : r->exit_hint_text;
    rawdraw_draw_text(fb, width, height, 20,
                      rawdraw_layout_ink_centered_text_top_y_in_box(r->font, exit_hint, bottom_y, 24, 0), exit_hint,
                      r->font, secondary);
}

static void ap_transfer_page_render_status(ap_transfer_page_t *r, uint8_t *fb, int width, int height)
{
    const rawdraw_color_t secondary = rawdraw_theme_color_for(THEME_TOKEN_TEXT_SECONDARY);
    const rawdraw_color_t danger    = rawdraw_theme_color_for(THEME_TOKEN_DANGER);
    const rawdraw_color_t accent    = rawdraw_theme_color_for(THEME_TOKEN_ACCENT);
    const rawdraw_color_t border    = rawdraw_theme_color_for(THEME_TOKEN_BORDER);
    const int             center_y  = height / 2;

    /* Status based on state */
    const char *status_text = "";
    const char *detail_text = r->status_message[0] == '\0' ? "" : r->status_message;

    switch (r->state) {
    case AP_TRANSFER_STATE_CLIENT_CONNECTED:
        status_text = "设备已连接";
        break;
    case AP_TRANSFER_STATE_UPLOADING:
        status_text = "上传中...";
        break;
    case AP_TRANSFER_STATE_PROCESSING:
        status_text = "处理图片...";
        break;
    case AP_TRANSFER_STATE_COMPLETE:
        status_text = "传输完成!";
        break;
    case AP_TRANSFER_STATE_ERROR:
        status_text = "传输失败";
        break;
    default:
        break;
    }

    /* Draw status */
    if (status_text[0] != '\0') {
        const int status_w = rawdraw_measure_text_width(status_text, r->title_font);
        rawdraw_draw_text(fb, width, height, (width - status_w) / 2,
                          rawdraw_layout_ink_centered_text_top_y(r->title_font, status_text, center_y - 20, 0),
                          status_text, r->title_font, r->state == AP_TRANSFER_STATE_ERROR ? danger : accent);
    }

    /* Draw detail */
    if (detail_text[0] != '\0') {
        const int detail_w = rawdraw_measure_text_width(detail_text, r->font);
        rawdraw_draw_text(fb, width, height, (width - detail_w) / 2,
                          rawdraw_layout_ink_centered_text_top_y(r->font, detail_text, center_y + 20, 0), detail_text,
                          r->font, secondary);
    }

    /* Progress bar for uploading/processing */
    if (r->state == AP_TRANSFER_STATE_UPLOADING || r->state == AP_TRANSFER_STATE_PROCESSING) {
        const int bar_w = width - 60;
        const int bar_h = 8;
        const int bar_x = 30;
        const int bar_y = center_y + 50;

        rawdraw_draw_rect_border(fb, width, height, (rawdraw_rect_t){bar_x, bar_y, bar_w, bar_h}, 1, border);
        /* Animated portion would be added later */
        rawdraw_draw_rect(fb, width, height, bar_x + 2, bar_y + 2, bar_w / 4, bar_h - 4, accent);
    }

    const char *ip = looks_like_ipv4(r->status_message) ? r->status_message : AP_TRANSFER_DEFAULT_AP_IP;
    char        url_buf[AP_TRANSFER_MSG_LEN + 16];
    const char *url;
    if (r->url_text[0] != '\0') {
        url = r->url_text;
    } else {
        snprintf(url_buf, sizeof(url_buf), "http://%s", ip);
        url = url_buf;
    }
    const int url_w = rawdraw_measure_text_width(url, r->font);
    rawdraw_draw_text(fb, width, height, (width - url_w) / 2,
                      rawdraw_layout_ink_centered_text_top_y(r->font, url, height - 62, 0), url, r->font, accent);

    /* Bottom hint */
    const int bottom_y = height - 30;
    rawdraw_draw_hline(fb, width, height, bottom_y - 10, 20, width - 20, border);
    const char *exit_hint = r->exit_hint_text[0] == '\0' ? "长按 BOOT 退出" : r->exit_hint_text;
    rawdraw_draw_text(fb, width, height, 20,
                      rawdraw_layout_ink_centered_text_top_y_in_box(r->font, exit_hint, bottom_y, 24, 0), exit_hint,
                      r->font, secondary);
}

/* ------------------------------------------------------------------ */
/* PageRenderer vtable                                                 */
/* ------------------------------------------------------------------ */

void ap_transfer_page_init(page_renderer_t *self, int width, int height)
{
    ap_transfer_page_t *r = (ap_transfer_page_t *)self;
    if (!r)
        return;
    r->base.width        = width;
    r->base.height       = height;
    r->state             = AP_TRANSFER_STATE_WAITING_CONNECTION;
    r->status_message[0] = '\0';
    r->font              = kApTransferFont;
    r->title_font        = kApTransferTitleFont;
    ap_transfer_page_use_default_instructions(self);
    r->exit_callback                = NULL;
    r->exit_callback_ctx            = NULL;
    r->base.needs_full_refresh_flag = true;
    ESP_LOGI(TAG, "ApTransferPage initialized: %dx%d", width, height);
}

void ap_transfer_page_render(page_renderer_t *self, uint8_t *fb, int width, int height)
{
    ap_transfer_page_t *r = (ap_transfer_page_t *)self;
    if (!r || !fb)
        return;

    const rawdraw_paint_style_t bg_style    = rawdraw_theme_style(THEME_TOKEN_BACKGROUND_PRIMARY);
    const rawdraw_paint_style_t title_style = rawdraw_theme_style(THEME_TOKEN_BACKGROUND_SECONDARY);
    const rawdraw_color_t       text        = rawdraw_theme_color_for(THEME_TOKEN_TEXT_PRIMARY);
    const rawdraw_color_t       border      = rawdraw_theme_color_for(THEME_TOKEN_BORDER);

    /* Clear to white */
    rawdraw_draw_styled_rect(fb, width, height, (rawdraw_rect_t){0, 0, width, height}, &bg_style);

    /* Draw outer frame (Macintosh style) */
    rawdraw_draw_round_rect_border(fb, width, height, (rawdraw_rect_t){1, 1, width - 2, height - 2},
                                   STYLE_BORDER_RADIUS_MD, 1, border);

    /* Title bar area */
    const int titlebar_h = STYLE_STATUS_BAR_HEIGHT;
    rawdraw_draw_styled_rect(fb, width, height, (rawdraw_rect_t){1, 1, width - 2, titlebar_h}, &title_style);
    rawdraw_draw_hline(fb, width, height, titlebar_h, 1, width - 2, border);

    /* Title */
    const char *title   = r->title_text[0] == '\0' ? "WiFi 传图" : r->title_text;
    const int   title_w = rawdraw_measure_text_width(title, r->title_font);
    rawdraw_draw_text(fb, width, height, (width - title_w) / 2,
                      rawdraw_layout_ink_centered_text_top_y_in_box(r->title_font, title, 1, titlebar_h, 0), title,
                      r->title_font, text);

    /* Content based on state */
    if (r->state == AP_TRANSFER_STATE_WAITING_CONNECTION) {
        ap_transfer_page_render_instructions(r, fb, width, height);
    } else {
        ap_transfer_page_render_status(r, fb, width, height);
    }

    r->base.needs_full_refresh_flag = false;
}

bool ap_transfer_page_handle_input(page_renderer_t *self, const ui_button_event_t *event)
{
    (void)self;
    if (!event)
        return false;
    ESP_LOGI(TAG, "HandleInput: type=%d", (int)event->type);

    /* AP mode is intentionally stable during slow e-paper refreshes. Only the
     * global BOOT long-press handler exits AP transfer; short clicks do nothing. */
    if (event->type == BTN_BOOT_CLICK) {
        ESP_LOGI(TAG, "BOOT click ignored in AP transfer mode");
        return false;
    }

    return false;
}

/* ------------------------------------------------------------------ */
/* Data interface                                                      */
/* ------------------------------------------------------------------ */

void ap_transfer_page_set_state(page_renderer_t *self, ap_transfer_state_t state, const char *message)
{
    ap_transfer_page_t *r = (ap_transfer_page_t *)self;
    if (!r)
        return;
    r->state = state;
    if (message) {
        strncpy(r->status_message, message, sizeof(r->status_message) - 1);
        r->status_message[sizeof(r->status_message) - 1] = '\0';
    } else {
        r->status_message[0] = '\0';
    }
    r->base.needs_full_refresh_flag = true;
    ESP_LOGI(TAG, "SetState: %d, message='%s'", (int)state, r->status_message);
}

ap_transfer_state_t ap_transfer_page_get_state(const page_renderer_t *self)
{
    const ap_transfer_page_t *r = (const ap_transfer_page_t *)self;
    return r ? r->state : AP_TRANSFER_STATE_ERROR;
}

const char *ap_transfer_page_get_status_message(const page_renderer_t *self)
{
    const ap_transfer_page_t *r = (const ap_transfer_page_t *)self;
    return r ? r->status_message : "";
}

void ap_transfer_page_use_default_instructions(page_renderer_t *self)
{
    ap_transfer_page_set_instruction_content(self, "WiFi 传图", "InkScreen-AP", "12345678", "http://192.168.4.1", "",
                                             "长按 BOOT 退出");
}

void ap_transfer_page_set_instruction_content(page_renderer_t *self, const char *title, const char *ssid,
                                              const char *password, const char *url, const char *hint,
                                              const char *exit_hint)
{
    ap_transfer_page_t *r = (ap_transfer_page_t *)self;
    if (!r)
        return;
#define COPY_FIELD(field, value)                                                                                       \
    do {                                                                                                               \
        if ((value) != NULL) {                                                                                         \
            strncpy((field), (value), sizeof((field)) - 1);                                                            \
            (field)[sizeof((field)) - 1] = '\0';                                                                       \
        } else {                                                                                                       \
            (field)[0] = '\0';                                                                                         \
        }                                                                                                              \
    } while (0)

    COPY_FIELD(r->title_text, title);
    COPY_FIELD(r->ssid_text, ssid);
    COPY_FIELD(r->password_text, password);
    COPY_FIELD(r->url_text, url);
    COPY_FIELD(r->hint_text, hint);
    COPY_FIELD(r->exit_hint_text, exit_hint);
#undef COPY_FIELD

    r->base.needs_full_refresh_flag = true;
    ESP_LOGI(TAG, "SetInstructionContent title='%s' ssid='%s' url='%s'", r->title_text, r->ssid_text, r->url_text);
}

void ap_transfer_page_set_exit_callback(page_renderer_t *self, void (*callback)(void *ctx), void *ctx)
{
    ap_transfer_page_t *r = (ap_transfer_page_t *)self;
    if (!r)
        return;
    r->exit_callback     = callback;
    r->exit_callback_ctx = ctx;
}

/* ------------------------------------------------------------------ */
/* vtable instance                                                     */
/* ------------------------------------------------------------------ */

EXT_RAM_BSS_ATTR ap_transfer_page_t s_ap_transfer_instance;

const page_renderer_ops_t ap_transfer_page_ops = {
    .init                    = ap_transfer_page_init,
    .render                  = ap_transfer_page_render,
    .handle_input            = ap_transfer_page_handle_input,
    .get_dirty_rect          = NULL,
    .needs_full_refresh      = NULL,
    .mark_full_refresh       = NULL,
    .clear_full_refresh_flag = NULL,
    .append_text             = NULL,
    .begin_stream            = NULL,
    .end_stream              = NULL,
};

PAGE_REGISTER(UI_PAGE_AP_TRANSFER, "传图模式", NULL, false, 999, &ap_transfer_page_ops, &s_ap_transfer_instance.base);
