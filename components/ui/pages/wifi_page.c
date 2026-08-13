/**
 * @file wifi_renderer.c
 * @brief WiFi status page renderer — C port of C++ rawdraw::WifiRenderer.
 *
 * CONNECTING  : blinking WiFi icon + "正在连接..." + progress bar
 * CONNECTED   : WiFi info card (icon + SSID + signal bars) + server card
 * DISCONNECTED: large cross mark + reconnect button + hints
 */
#include "wifi_page.h"
#include "page_registry.h"

#include "rawdraw_ext.h"
#include "theme.h"
#include "style.h"
#include "layout.h"
#include "ui_text_util.h"
#include "font_zectrix.h"
#include "progress_bar.h"

#include <stdio.h>
#include <string.h>

static const lv_font_t *const kWifiFont = &SourceHanSansSC_Regular_slim;
static const lv_font_t *const kWifiTitleFont = &SourceHanSansSC_Medium_slim;
static const lv_font_t *const kWifiIconFont = &font_zectrix_16_1;
static const lv_font_t *const kWifiLargeIconFont = &font_zectrix_48_1;

/* ------------------------------------------------------------------ */
/* Signal helpers                                                      */
/* ------------------------------------------------------------------ */

static int signal_to_percent(int dbm)
{
    /* Map dBm (-30 to -90) to percentage (100 to 0). */
    int pct = (dbm + 90) * 100 / 60;
    return RD_MAX(0, RD_MIN(100, pct));
}

static const char *wifi_icon_glyph(int signal_dbm)
{
    /* Map signal strength to a zectrix WiFi icon level. */
    const int pct = signal_to_percent(signal_dbm);
    if (pct >= 75)
        return FONT_ZECTRIX_WIFI_FULL; /* strong */
    if (pct >= 50)
        return FONT_ZECTRIX_WIFI_FAIR; /* medium */
    return FONT_ZECTRIX_WIFI_WEAK; /* weak */
}

/* ------------------------------------------------------------------ */
/* Drawing helpers                                                     */
/* ------------------------------------------------------------------ */

static void draw_wifi_icon(wifi_page_t *r, uint8_t *fb, int width, int height, int x, int y, int size,
                           rawdraw_color_t color)
{
    /* The size parameter picks the icon font: >= 32 px uses the 48 px hero
     * glyph, otherwise the 16 px small glyph (glyphs render at font size). */
    const lv_font_t *font = (size >= 32) ? r->large_icon_font : r->icon_font;
    const char *glyph = wifi_icon_glyph(r->status.signal_strength);
    rawdraw_draw_text(fb, width, height, x, y, glyph, font, color);
}

static void draw_signal_bars(uint8_t *fb, int width, int height, int x, int y, int bar_count, int signal_pct)
{
    const rawdraw_color_t active_color = rawdraw_theme_color_for(THEME_TOKEN_SUCCESS_LIKE);
    const rawdraw_color_t inactive_bg = rawdraw_theme_color_for(THEME_TOKEN_BACKGROUND_SECONDARY);
    const rawdraw_color_t border = rawdraw_theme_color_for(THEME_TOKEN_BORDER);
    const int bar_w = 6;
    const int bar_gap = 3;
    const int max_bar_h = 24;

    for (int i = 0; i < bar_count; ++i) {
        const int threshold = (i + 1) * 100 / bar_count;
        const bool active = signal_pct >= threshold;
        const int bar_h = max_bar_h * (i + 1) / bar_count;
        const int bar_x = x + i * (bar_w + bar_gap);
        const int bar_y = y + (max_bar_h - bar_h);
        if (active) {
            rawdraw_draw_round_rect(fb, width, height, bar_x, bar_y, bar_w, bar_h, STYLE_BORDER_RADIUS_SM, active_color,
                                    active_color, 0);
        } else {
            rawdraw_draw_round_rect(fb, width, height, bar_x, bar_y, bar_w, bar_h, STYLE_BORDER_RADIUS_SM, inactive_bg,
                                    border, STYLE_BORDER_THIN);
        }
    }
}

static void draw_server_card(wifi_page_t *r, uint8_t *fb, int width, int height, int x, int y, int w)
{
    const rawdraw_paint_style_t card_style = rawdraw_theme_component(ROLE_CARD_DEFAULT);
    const rawdraw_color_t text = rawdraw_theme_color_for(THEME_TOKEN_TEXT_PRIMARY);
    const rawdraw_color_t secondary = rawdraw_theme_color_for(THEME_TOKEN_TEXT_SECONDARY);
    const rawdraw_color_t status_color =
        rawdraw_theme_color_for(r->status.server_connected ? THEME_TOKEN_SUCCESS_LIKE : THEME_TOKEN_WARNING);
    const rawdraw_color_t border = rawdraw_theme_color_for(THEME_TOKEN_BORDER);
    const int card_h = 76;

    rawdraw_draw_styled_round_rect(fb, width, height, (rawdraw_rect_t){x, y, w, card_h}, STYLE_BORDER_RADIUS_LG,
                                   &card_style);
    rawdraw_draw_hline(fb, width, height, y + STYLE_PANEL_TITLE_HEIGHT - 1, x + STYLE_BORDER_RADIUS_LG,
                       x + w - STYLE_BORDER_RADIUS_LG, border);
    rawdraw_draw_text(fb, width, height, x + STYLE_PANEL_PADDING, y + STYLE_SPACING_XS, "服务器", r->title_font, text);

    const int icon_y = y + STYLE_PANEL_TITLE_HEIGHT + STYLE_SPACING_SM;
    if (r->status.server_connected) {
        /* Checkmark as text (icon font glyphs for it are unavailable). */
        rawdraw_draw_text(fb, width, height, x + STYLE_PANEL_PADDING, icon_y, "[v]", r->font, status_color);
        rawdraw_draw_text(fb, width, height, x + STYLE_PANEL_PADDING + STYLE_FONT_SIZE_SM + STYLE_SPACING_SM, icon_y,
                          "已连接", r->font, text);
        if (r->status.server_uri[0] != '\0') {
            const int uri_x = x + STYLE_PANEL_PADDING + STYLE_SPACING_XS;
            const int uri_max_w = RD_MAX(0, w - STYLE_PANEL_PADDING * 2 - STYLE_SPACING_XS * 2);
            char uri_buf[64];
            ui_text_fit_to_width(r->status.server_uri, r->font, uri_max_w, uri_buf, sizeof(uri_buf));
            rawdraw_draw_text(fb, width, height, uri_x, icon_y + r->font->line_height + STYLE_SPACING_XS, uri_buf,
                              r->font, secondary);
        }
    } else {
        rawdraw_draw_text(fb, width, height, x + STYLE_PANEL_PADDING, icon_y, "[X]", r->font, status_color);
        rawdraw_draw_text(fb, width, height, x + STYLE_PANEL_PADDING + STYLE_FONT_SIZE_SM + STYLE_SPACING_SM, icon_y,
                          "未连接", r->font, text);
    }
}

/* ------------------------------------------------------------------ */
/* State renderers                                                     */
/* ------------------------------------------------------------------ */

static void render_connecting(wifi_page_t *r, uint8_t *fb, int width, int height)
{
    const rawdraw_color_t text = rawdraw_theme_color_for(THEME_TOKEN_TEXT_PRIMARY);
    const rawdraw_color_t secondary = rawdraw_theme_color_for(THEME_TOKEN_TEXT_SECONDARY);
    const rawdraw_color_t accent = rawdraw_theme_color_for(THEME_TOKEN_ACCENT);

    /* Large WiFi icon (centered, blinking). */
    const int icon_size = STYLE_FONT_SIZE_XL; /* 48 px */
    r->blink_frame++;
    const bool visible = (r->blink_frame % 20) < 14; /* on for 14 frames */

    if (visible) {
        const int icon_x = (width - icon_size) / 2;
        const int icon_y = STYLE_STATUS_BAR_HEIGHT + STYLE_SPACING_XL;
        draw_wifi_icon(r, fb, width, height, icon_x, icon_y, icon_size, accent);
    }

    /* Status text. */
    const char *status_text = "正在连接...";
    int text_w = rawdraw_measure_text_width(status_text, r->font);
    int text_x = (width - text_w) / 2;
    int text_y = STYLE_STATUS_BAR_HEIGHT + STYLE_SPACING_XL + icon_size + STYLE_SPACING_LG;
    rawdraw_draw_text(fb, width, height, text_x, text_y, status_text, r->font, text);

    /* SSID text if available. */
    if (r->status.ssid[0] != '\0') {
        char ssid_buf[32];
        ui_text_fit_to_width(r->status.ssid, r->font, width - STYLE_SPACING_XL * 2, ssid_buf, sizeof(ssid_buf));
        int ssid_w = rawdraw_measure_text_width(ssid_buf, r->font);
        int ssid_x = (width - ssid_w) / 2;
        int ssid_y = text_y + r->font->line_height + STYLE_SPACING_SM;
        rawdraw_draw_text(fb, width, height, ssid_x, ssid_y, ssid_buf, r->font, secondary);
    }

    /* Progress bar. */
    if (r->status.progress > 0) {
        const int bar_y = height - STYLE_SPACING_XXL - STYLE_PROGRESS_HEIGHT;
        const int bar_w = width - STYLE_SPACING_XL * 2;
        const int bar_x = STYLE_SPACING_XL;

        widget_progress_bar_t bar;
        widget_progress_bar_init(&bar, bar_x, bar_y, bar_w, STYLE_PROGRESS_HEIGHT);
        widget_progress_bar_set_value(&bar, r->status.progress);
        widget_progress_bar_render(&bar, fb, width, height);

        char pct_buf[8];
        snprintf(pct_buf, sizeof(pct_buf), "%d%%", r->status.progress);
        int pct_w = rawdraw_measure_text_width(pct_buf, r->font);
        rawdraw_draw_text(fb, width, height, (width - pct_w) / 2, bar_y - r->font->line_height - STYLE_SPACING_XS,
                          pct_buf, r->font, text);
    }

    /* Hint text. */
    const char *hint = "请稍候...";
    int hint_w = rawdraw_measure_text_width(hint, r->font);
    rawdraw_draw_text(fb, width, height, (width - hint_w) / 2, height - r->font->line_height - STYLE_SPACING_SM, hint,
                      r->font, secondary);
}

static void render_connected(wifi_page_t *r, uint8_t *fb, int width, int height)
{
    const rawdraw_paint_style_t card_style = rawdraw_theme_component(ROLE_CARD_DEFAULT);
    const rawdraw_color_t text = rawdraw_theme_color_for(THEME_TOKEN_TEXT_PRIMARY);
    const rawdraw_color_t secondary = rawdraw_theme_color_for(THEME_TOKEN_TEXT_SECONDARY);
    const rawdraw_color_t border = rawdraw_theme_color_for(THEME_TOKEN_BORDER);
    const rawdraw_color_t accent = rawdraw_theme_color_for(THEME_TOKEN_ACCENT);
    const int content_top = STYLE_STATUS_BAR_HEIGHT + STYLE_SPACING_SM;
    const int card_w = width - STYLE_SPACING_XL;
    const int card_x = STYLE_SPACING_XL / 2;

    /* === WiFi Info Card === */
    int card_y = content_top;
    int card_h = 80;

    rawdraw_draw_styled_round_rect(fb, width, height, (rawdraw_rect_t){card_x, card_y, card_w, card_h},
                                   STYLE_BORDER_RADIUS_LG, &card_style);
    rawdraw_draw_hline(fb, width, height, card_y + STYLE_PANEL_TITLE_HEIGHT - 1, card_x + STYLE_BORDER_RADIUS_LG,
                       card_x + card_w - STYLE_BORDER_RADIUS_LG, border);
    rawdraw_draw_text(fb, width, height, card_x + STYLE_PANEL_PADDING, card_y + STYLE_SPACING_XS, "WiFi", r->title_font,
                      text);

    const int wifi_icon_size = 16; /* small glyph inside the card */
    const int wifi_icon_x = card_x + STYLE_PANEL_PADDING;
    const int wifi_icon_y = card_y + STYLE_PANEL_TITLE_HEIGHT + STYLE_SPACING_SM;
    const int signal_pct = signal_to_percent(r->status.signal_strength);
    const int bars_x = card_x + card_w - STYLE_PANEL_PADDING - 45;
    const int bars_y = wifi_icon_y + 4;
    draw_wifi_icon(r, fb, width, height, wifi_icon_x, wifi_icon_y, wifi_icon_size, accent);

    /* SSID text (center-right). */
    if (r->status.ssid[0] != '\0') {
        const int ssid_x = wifi_icon_x + wifi_icon_size + STYLE_SPACING_SM;
        const int ssid_max_w = RD_MAX(0, bars_x - STYLE_SPACING_SM - ssid_x);
        char ssid_buf[32];
        ui_text_fit_to_width(r->status.ssid, r->title_font, ssid_max_w, ssid_buf, sizeof(ssid_buf));
        rawdraw_draw_text(fb, width, height, ssid_x, wifi_icon_y, ssid_buf, r->title_font, text);
    }

    /* Signal bars (right side) + percentage text below. */
    draw_signal_bars(fb, width, height, bars_x, bars_y, 5, signal_pct);
    char signal_buf[16];
    snprintf(signal_buf, sizeof(signal_buf), "%d%%", signal_pct);
    int sig_w = rawdraw_measure_text_width(signal_buf, r->font);
    rawdraw_draw_text(fb, width, height, bars_x + (45 - sig_w) / 2, bars_y + 28, signal_buf, r->font, secondary);

    /* === Server Status Card === */
    int server_y = card_y + card_h + STYLE_SPACING_SM;
    draw_server_card(r, fb, width, height, card_x, server_y, card_w);

    /* === Bottom hint === */
    const char *hint = "按 BOOT 返回";
    int hint_w = rawdraw_measure_text_width(hint, r->font);
    rawdraw_draw_text(fb, width, height, (width - hint_w) / 2, height - r->font->line_height - STYLE_SPACING_SM, hint,
                      r->font, secondary);
}

static void render_disconnected(wifi_page_t *r, uint8_t *fb, int width, int height)
{
    const rawdraw_paint_style_t button_style = rawdraw_theme_component(ROLE_BUTTON_SELECTED);
    const rawdraw_color_t text = rawdraw_theme_color_for(THEME_TOKEN_TEXT_PRIMARY);
    const rawdraw_color_t secondary = rawdraw_theme_color_for(THEME_TOKEN_TEXT_SECONDARY);
    const rawdraw_color_t danger = rawdraw_theme_color_for(THEME_TOKEN_DANGER);
    const rawdraw_color_t border = rawdraw_theme_color_for(THEME_TOKEN_BORDER);
    const int center_y = height / 2 - STYLE_SPACING_XXL;

    /* Large cross mark drawn with rawdraw primitives (no 48 px "X" glyph
     * exists in the icon fonts). */
    const int cross_icon_size = STYLE_FONT_SIZE_XL; /* 48 px */
    const int cross_cx = width / 2;
    const int cross_cy = center_y + cross_icon_size / 2;
    const int arm = cross_icon_size / 3;
    rawdraw_draw_line(fb, width, height, (rawdraw_point_t){cross_cx - arm, cross_cy - arm},
                      (rawdraw_point_t){cross_cx + arm, cross_cy + arm}, danger);
    rawdraw_draw_line(fb, width, height, (rawdraw_point_t){cross_cx - arm, cross_cy + arm},
                      (rawdraw_point_t){cross_cx + arm, cross_cy - arm}, danger);

    /* Status text. */
    const char *status_text = "网络已断开";
    int text_w = rawdraw_measure_text_width(status_text, r->title_font);
    int text_x = (width - text_w) / 2;
    int text_y = center_y + cross_icon_size + STYLE_SPACING_LG;
    rawdraw_draw_text(fb, width, height, text_x, text_y, status_text, r->title_font, text);

    /* Divider. */
    const int divider_y = text_y + r->title_font->line_height + STYLE_SPACING_SM;
    const int divider_w = 120;
    rawdraw_draw_hline(fb, width, height, divider_y, (width - divider_w) / 2, (width + divider_w) / 2, border);

    /* Primary action as a button-like element. */
    const char *primary = "按 BOOT 重新连接";
    int primary_w = rawdraw_measure_text_width(primary, r->font);
    const int btn_h = r->font->line_height + STYLE_SPACING_SM * 2;
    const int btn_w = primary_w + STYLE_SPACING_XL;
    const int btn_x = (width - btn_w) / 2;
    const int btn_y = divider_y + STYLE_SPACING_SM;
    rawdraw_draw_styled_round_rect(fb, width, height, (rawdraw_rect_t){btn_x, btn_y, btn_w, btn_h},
                                   STYLE_BORDER_RADIUS_PILL, &button_style);
    rawdraw_draw_text(fb, width, height, btn_x + STYLE_SPACING_MD,
                      rawdraw_layout_ink_centered_text_top_y_in_box(r->font, primary, btn_y, btn_h, 0), primary,
                      r->font, button_style.fg);

    /* Secondary hint. */
    const char *secondary_hint = "长按 BOOT 进入图传模式";
    int sec_w = rawdraw_measure_text_width(secondary_hint, r->font);
    int sec_x = (width - sec_w) / 2;
    int sec_y = btn_y + btn_h + STYLE_SPACING_SM;
    if (sec_y + r->font->line_height <= height - STYLE_SPACING_SM) {
        rawdraw_draw_text(fb, width, height, sec_x, sec_y, secondary_hint, r->font, secondary);
    }
}

/* ------------------------------------------------------------------ */
/* PageRenderer vtable                                                 */
/* ------------------------------------------------------------------ */

void wifi_page_init(page_renderer_t *self, int width, int height)
{
    wifi_page_t *r = (wifi_page_t *)self;
    r->base.width = width;
    r->base.height = height;
    r->base.needs_full_refresh_flag = true;
    r->status.state = WIFI_STATE_DISCONNECTED;
    r->status.ssid[0] = '\0';
    r->status.signal_strength = 0;
    r->status.progress = 0;
    r->status.server_connected = false;
    r->status.server_uri[0] = '\0';
    r->is_blinking = false;
    r->blink_frame = 0;
    r->font = kWifiFont;
    r->title_font = kWifiTitleFont;
    r->icon_font = kWifiIconFont;
    r->large_icon_font = kWifiLargeIconFont;
}

/* Page gained focus: request a redraw but keep the live WiFi status. */
static void wifi_page_enter(page_renderer_t *self)
{
    wifi_page_t *r = (wifi_page_t *)self;
    if (!r)
        return;
    r->base.needs_full_refresh_flag = true;
}

void wifi_page_render(page_renderer_t *self, uint8_t *fb, int width, int height)
{
    wifi_page_t *r = (wifi_page_t *)self;
    if (!fb)
        return;

    /* Note: the framebuffer is NOT cleared here — it is managed by the UI
     * manager which draws the status bar first, then the page render. */

    switch (r->status.state) {
    case WIFI_STATE_CONNECTING:
        render_connecting(r, fb, width, height);
        break;
    case WIFI_STATE_CONNECTED:
        render_connected(r, fb, width, height);
        break;
    case WIFI_STATE_DISCONNECTED:
        render_disconnected(r, fb, width, height);
        break;
    default:
        break;
    }

    r->base.needs_full_refresh_flag = false;
}

bool wifi_page_handle_input(page_renderer_t *self, const ui_button_event_t *event)
{
    wifi_page_t *r = (wifi_page_t *)self;
    switch (event->type) {
    case BTN_BOOT_CLICK:
        /* Trigger reconnection if disconnected — let the app handle it. */
        if (r->status.state == WIFI_STATE_DISCONNECTED) {
            return false;
        }
        break;
    case BTN_DOWN_LONG_PRESS:
        /* Enter WiFi config mode — let the app handle it. */
        return false;
    default:
        break;
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* Data interface                                                      */
/* ------------------------------------------------------------------ */

void wifi_page_update(page_renderer_t *self, const wifi_status_t *status)
{
    wifi_page_t *r = (wifi_page_t *)self;
    if (!status)
        return;
    r->status = *status;
    r->status.ssid[sizeof(r->status.ssid) - 1] = '\0';
    r->status.server_uri[sizeof(r->status.server_uri) - 1] = '\0';
    r->is_blinking = (status->state == WIFI_STATE_CONNECTING);
    r->base.needs_full_refresh_flag = true;
}

const wifi_status_t *wifi_page_get_status(const page_renderer_t *self)
{
    const wifi_page_t *r = (const wifi_page_t *)self;
    return &r->status;
}

void wifi_page_set_blinking(page_renderer_t *self, bool blinking)
{
    wifi_page_t *r = (wifi_page_t *)self;
    r->is_blinking = blinking;
    r->blink_frame++;
    r->base.needs_full_refresh_flag = true;
}

bool wifi_page_is_blinking(const page_renderer_t *self)
{
    const wifi_page_t *r = (const wifi_page_t *)self;
    return r->is_blinking;
}

/* ------------------------------------------------------------------ */
/* vtable instance                                                     */
/* ------------------------------------------------------------------ */

EXT_RAM_BSS_ATTR wifi_page_t s_wifi_instance;

const page_renderer_ops_t wifi_page_ops = {
    .init = wifi_page_init,
    .enter = wifi_page_enter,
    .render = wifi_page_render,
    .handle_input = wifi_page_handle_input,
    .get_dirty_rect = NULL,
    .needs_full_refresh = NULL,
    .mark_full_refresh = NULL,
    .clear_full_refresh_flag = NULL,
    .append_text = NULL,
    .begin_stream = NULL,
    .end_stream = NULL,
};

PAGE_REGISTER(UI_PAGE_WIFI, "WiFi状态", NULL, true, 60, &wifi_page_ops, &s_wifi_instance.base);
