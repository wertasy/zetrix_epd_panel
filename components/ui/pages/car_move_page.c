/**
 * @file car_move_page.c
 * @brief Car move (scan-QR-to-call-owner) page renderer — local pure-C
 *        implementation (计划 Task 5.9, 本地全新).
 *
 * Renders the "微信扫码，呼叫车主" headline with the owner phone number from
 * NVS settings (key "car_phone", default 13800000000), encodes it as a
 * `tel:` QR payload, and renders a real QR code via the esp_qrcode component
 * (espressif/qrcode, declared in main/idf_component.yml). If QR generation
 * fails (e.g. out of memory), a placeholder box with instructions is drawn
 * instead. Short-pressing BOOT reloads the phone number and refreshes.
 *
 * QR rendering: esp_qrcode_generate() runs a capture callback that stores the
 * module bitmap (esp_qrcode_get_module) into a scratch buffer; after
 * generation the modules are scaled into a box on the framebuffer — black
 * modules paint RAWDRAW_COLOR_BLACK, white modules paint RAWDRAW_COLOR_WHITE
 * (a quiet zone of 4 modules is added around the symbol).
 */
#include "car_move_page.h"
#include "page_registry.h"

#include "rawdraw_ext.h"
#include "theme.h"
#include "style.h"
#include "layout.h"
#include "footer_bar.h"
#include "nvs_state.h"
#include "qrcode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CAR_MOVE_SETTINGS_KEY "car_phone"

#ifndef CONFIG_CAR_MOVE_PHONE_NUMBER
#    define CONFIG_CAR_MOVE_PHONE_NUMBER ""
#endif

#define CAR_MOVE_QR_BOX_SIZE 210 /* on-screen QR box (px), screen-centered */
#define CAR_MOVE_QR_MAX_MODULES 177 /* version-40 max; we cap version at 10  */

static const lv_font_t *const car_move_font = &SourceHanSansSC_Regular_slim;
static const lv_font_t *const car_move_title_font = &SourceHanSansSC_Medium_slim;

static void reload_phone(car_move_page_t *r)
{
    /* Priority: NVS (runtime override) → sdkconfig → compile-time default. */
    if (!nvs_state_get_string(CAR_MOVE_SETTINGS_KEY, r->phone, sizeof(r->phone))) {
        if (strlen(CONFIG_CAR_MOVE_PHONE_NUMBER) > 0) {
            strncpy(r->phone, CONFIG_CAR_MOVE_PHONE_NUMBER, sizeof(r->phone) - 1);
            r->phone[sizeof(r->phone) - 1] = '\0';
        } else {
            strncpy(r->phone, CAR_MOVE_PHONE_DEFAULT, sizeof(r->phone) - 1);
            r->phone[sizeof(r->phone) - 1] = '\0';
        }
    }
    /* QR payload: raw phone number (WeChat recognises plain phone numbers
     * and offers to dial; `tel:` URIs are blocked by WeChat scanner). */
    snprintf(r->tel_text, sizeof(r->tel_text), "%s", r->phone);
    /* Compact display form: strip a leading +86 (common CN prefix). */
    const char *src = r->phone;
    if (strncmp(src, "+86", 3) == 0) {
        src += 3;
    }
    snprintf(r->phone_display, sizeof(r->phone_display), "%s", src);
}

/* ------------------------------------------------------------------ */
/* QR rendering (esp_qrcode) + placeholder fallback                    */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t *modules; /* scratch module bitmap (1 bit per module) */
    int size; /* QR side length in modules (0 = not generated) */
} qr_capture_ctx_t;

static void qr_capture_cb(esp_qrcode_handle_t qrcode, void *user_data)
{
    qr_capture_ctx_t *ctx = (qr_capture_ctx_t *)user_data;
    const int n = esp_qrcode_get_size(qrcode);
    if (!ctx->modules || n <= 0 || n > CAR_MOVE_QR_MAX_MODULES)
        return;
    ctx->size = n;
    const int row_bytes = (n + 7) / 8;
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            if (esp_qrcode_get_module(qrcode, x, y)) {
                ctx->modules[y * row_bytes + (x >> 3)] |= (uint8_t)(0x80 >> (x & 7));
            }
        }
    }
}

/**
 * @brief Generate the tel: payload QR and draw it centered in box
 *        (box_x, box_y, box_size). Returns true on success; on failure the
 *        caller falls back to the placeholder box.
 */
static bool render_qr(page_renderer_t *self, uint8_t *fb, int width, int height, int box_x, int box_y, int box_size)
{
    car_move_page_t *r = (car_move_page_t *)self;

    uint8_t *scratch = (uint8_t *)malloc(CAR_MOVE_QR_MAX_MODULES * CAR_MOVE_QR_MAX_MODULES / 8);
    if (!scratch)
        return false;
    memset(scratch, 0, (size_t)CAR_MOVE_QR_MAX_MODULES * CAR_MOVE_QR_MAX_MODULES / 8);

    qr_capture_ctx_t ctx = {.modules = scratch, .size = 0};
    esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
    cfg.display_func_with_cb = qr_capture_cb;
    cfg.user_data = &ctx;
    cfg.max_qrcode_version = 10; /* 57x57 max — plenty for a tel: payload */

    const esp_err_t err = esp_qrcode_generate(&cfg, r->tel_text);
    if (err != ESP_OK || ctx.size <= 0) {
        free(scratch);
        return false;
    }

    const int n = ctx.size;
    const int total = n + 8; /* symbol + 4-module quiet zone */
    const int cell = box_size / total;
    if (cell < 1) {
        free(scratch);
        return false;
    }
    const int drawn = cell * total;
    const int origin_x = box_x + (box_size - drawn) / 2;
    const int origin_y = box_y + (box_size - drawn) / 2;
    const int row_bytes = (n + 7) / 8;

    for (int my = 0; my < n; ++my) {
        for (int mx = 0; mx < n; ++mx) {
            const bool black = (scratch[my * row_bytes + (mx >> 3)] & (0x80 >> (mx & 7))) != 0;
            const int px = origin_x + (mx + 4) * cell;
            const int py = origin_y + (my + 4) * cell;
            const rawdraw_color_t color = black ? RAWDRAW_COLOR_BLACK : RAWDRAW_COLOR_WHITE;
            for (int dy = 0; dy < cell; ++dy) {
                for (int dx = 0; dx < cell; ++dx) {
                    rawdraw_set_pixel(fb, width, height, px + dx, py + dy, color);
                }
            }
        }
    }

    free(scratch);
    return true;
}

/* Fallback shown only when esp_qrcode_generate() fails (e.g. OOM). */
static void render_qr_placeholder(page_renderer_t *self, uint8_t *fb, int width, int height, int box_x, int box_y,
                                  int box_size)
{
    car_move_page_t *r = (car_move_page_t *)self;

    const rawdraw_color_t secondary = rawdraw_theme_color_for(THEME_TOKEN_TEXT_SECONDARY);
    const rawdraw_color_t border = rawdraw_theme_color_for(THEME_TOKEN_BORDER);
    const rawdraw_paint_style_t card_style = rawdraw_theme_component(ROLE_CARD_DEFAULT);

    rawdraw_draw_styled_round_rect(fb, width, height, (rawdraw_rect_t){box_x, box_y, box_size, box_size},
                                   STYLE_BORDER_RADIUS_SM, &card_style);

    /* QR finder-pattern mock: three corner squares + a center dot, so the
     * placeholder still reads as a QR frame at a glance. */
    const int finder = box_size / 7;
    const int inset = box_size / 18;
    const int finder_x[3] = {box_x + inset, box_x + box_size - inset - finder, box_x + inset};
    const int finder_y[3] = {box_y + inset, box_y + inset, box_y + box_size - inset - finder};
    for (int i = 0; i < 3; ++i) {
        rawdraw_draw_rect_border(fb, width, height, (rawdraw_rect_t){finder_x[i], finder_y[i], finder, finder},
                                 STYLE_BORDER_MEDIUM, border);
        rawdraw_draw_rect_border(fb, width, height,
                                 (rawdraw_rect_t){finder_x[i] + 6, finder_y[i] + 6, finder - 12, finder - 12},
                                 STYLE_BORDER_THIN, border);
    }
    const int dot_r = box_size / 22;
    rawdraw_draw_circle_border(fb, width, height, (rawdraw_point_t){box_x + box_size / 2, box_y + box_size / 2}, dot_r,
                               STYLE_BORDER_MEDIUM, border);

    const char *cap1 = "二维码暂不可用";
    const int cap1_w = rawdraw_measure_text_width(cap1, r->font);
    const int cap1_y = rawdraw_layout_ink_centered_text_top_y(r->font, cap1, box_y + box_size + 4, 0);
    rawdraw_draw_text(fb, width, height, (width - cap1_w) / 2, cap1_y, cap1, r->font, secondary);
}

static void render_qr_area(page_renderer_t *self, uint8_t *fb, int width, int height, int qr_x, int qr_y, int qr_size)
{
    if (!render_qr(self, fb, width, height, qr_x, qr_y, qr_size)) {
        render_qr_placeholder(self, fb, width, height, qr_x, qr_y, qr_size);
    }
}

/* ------------------------------------------------------------------ */
/* PageRenderer vtable                                                 */
/* ------------------------------------------------------------------ */

void car_move_page_init(page_renderer_t *self, int width, int height)
{
    car_move_page_t *r = (car_move_page_t *)self;
    r->base.width = width;
    r->base.height = height;
    r->base.needs_full_refresh_flag = true;
    r->font = car_move_font;
    r->title_font = car_move_title_font;
    reload_phone(r);
}

void car_move_page_render(page_renderer_t *self, uint8_t *fb, int width, int height)
{
    car_move_page_t *r = (car_move_page_t *)self;
    if (!fb)
        return;

    const rawdraw_paint_style_t bg_style = rawdraw_theme_style(THEME_TOKEN_BACKGROUND_PRIMARY);
    const rawdraw_color_t text = rawdraw_theme_color_for(THEME_TOKEN_TEXT_PRIMARY);
    const rawdraw_color_t accent = rawdraw_theme_color_for(THEME_TOKEN_ACCENT);

    rawdraw_draw_styled_rect(fb, width, height,
                             (rawdraw_rect_t){0, STYLE_STATUS_BAR_HEIGHT, width, height - STYLE_STATUS_BAR_HEIGHT},
                             &bg_style);

    /* Layout (400×300 screen):
     *   Headline   y=30–52
     *   QR box    210px, y=54–264
     *   Phone     y=268–276 (single line)
     *   Footer    y=278–300 */
    const int qr_size = CAR_MOVE_QR_BOX_SIZE;
    const int qr_x = (width - qr_size) / 2;
    const int qr_y = 54;

    /* Headline. */
    const char *headline = "微信扫码，呼叫车主";
    const int hl_w = rawdraw_measure_text_width(headline, r->title_font);
    const int hl_y = rawdraw_layout_ink_centered_text_top_y(r->title_font, headline, STYLE_STATUS_BAR_HEIGHT + 4, 0);
    rawdraw_draw_text(fb, width, height, (width - hl_w) / 2, hl_y, headline, r->title_font, text);
    rawdraw_draw_hline(fb, width, height, hl_y + r->title_font->line_height + 2, (width - hl_w) / 2 - 8,
                       (width + hl_w) / 2 + 8, accent);

    /* QR code. */
    render_qr_area(self, fb, width, height, qr_x, qr_y, qr_size);

    /* Phone number display. */
    char phone_line[64];
    snprintf(phone_line, sizeof(phone_line), "车主电话: %s", r->phone_display);
    const int phone_w = rawdraw_measure_text_width(phone_line, r->font);
    const int phone_y = rawdraw_layout_ink_centered_text_top_y(r->font, phone_line, qr_y + qr_size + 4, 0);
    rawdraw_draw_text(fb, width, height, (width - phone_w) / 2, phone_y, phone_line, r->title_font, text);

    /* Footer hints. */
    widget_footer_bar_t footer;
    widget_footer_bar_init(&footer, width, height);
    widget_footer_bar_set_text(&footer, NULL, NULL, "BOOT 刷新");
    widget_footer_bar_render(&footer, fb, width, height);

    r->base.needs_full_refresh_flag = false;
}

bool car_move_page_handle_input(page_renderer_t *self, const ui_button_event_t *event)
{
    car_move_page_t *r = (car_move_page_t *)self;
    switch (event->type) {
    case BTN_BOOT_CLICK:
        /* Reload phone from NVS and force a full redraw. */
        reload_phone(r);
        r->base.needs_full_refresh_flag = true;
        return true;
    default:
        return false;
    }
}

/* ------------------------------------------------------------------ */
/* Data interface                                                      */
/* ------------------------------------------------------------------ */

void car_move_page_set_phone(page_renderer_t *self, const char *phone)
{
    car_move_page_t *r = (car_move_page_t *)self;
    if (phone && phone[0] != '\0') {
        strncpy(r->phone, phone, sizeof(r->phone) - 1);
        r->phone[sizeof(r->phone) - 1] = '\0';
    } else {
        strncpy(r->phone, CAR_MOVE_PHONE_DEFAULT, sizeof(r->phone) - 1);
        r->phone[sizeof(r->phone) - 1] = '\0';
    }
    snprintf(r->tel_text, sizeof(r->tel_text), "%s", r->phone);
    snprintf(r->phone_display, sizeof(r->phone_display), "%s", r->phone);
    r->base.needs_full_refresh_flag = true;
}

const char *car_move_page_get_phone(const page_renderer_t *self)
{
    const car_move_page_t *r = (const car_move_page_t *)self;
    return r->phone;
}

const char *car_move_page_get_tel_payload(const page_renderer_t *self)
{
    const car_move_page_t *r = (const car_move_page_t *)self;
    return r->tel_text;
}

/* ------------------------------------------------------------------ */
/* vtable instance                                                     */
/* ------------------------------------------------------------------ */

EXT_RAM_BSS_ATTR car_move_page_t s_car_move_instance;

const page_renderer_ops_t car_move_page_ops = {
    .init = car_move_page_init,
    .render = car_move_page_render,
    .handle_input = car_move_page_handle_input,
    .get_dirty_rect = NULL,
    .needs_full_refresh = NULL,
    .mark_full_refresh = NULL,
    .clear_full_refresh_flag = NULL,
    .append_text = NULL,
    .begin_stream = NULL,
    .end_stream = NULL,
};

PAGE_REGISTER(UI_PAGE_CAR_MOVE, "扫码挪车", NULL, true, 50, &car_move_page_ops, &s_car_move_instance.base);
