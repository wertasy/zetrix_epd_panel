/* components/ui/pages/fridge_memo_page.c */
/**
 * @file fridge_memo_page.c
 * @brief Fridge memo page renderer (design doc v1.2 §5).
 *
 * Layout (400x300): status bar (shell) / summary strip 22px / 4 rows x 52px
 * / footer 26px. Navigation is full-page flip (EPD: every visible change is
 * a full refresh; row-cursor would cost 15-20s per step).
 */
#include "fridge_memo_page.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "data_refresh.h"
#include "font_zectrix.h"
#include "fridge_memo_api.h"
#include "page_registry.h"
#include "rawdraw_ext.h"
#include "style.h"
#include "theme.h"
#include "ui_text_util.h"

/* ------------------------------------------------------------------ */
/* Layout                                                              */
/* ------------------------------------------------------------------ */

#define FM_SUMMARY_H 22
#define FM_ROW_H 52
#define FM_FOOTER_Y 264
#define FM_FOOTER_H 26
#define FM_COLOR_BAR_W 4
#define FM_PANEL_PAD 8

static const lv_font_t *const fm_font = &SourceHanSansSC_Regular_slim;
static const lv_font_t *const fm_title_font = &SourceHanSansSC_Medium_slim;

EXT_RAM_BSS_ATTR fridge_memo_page_t s_fridge_memo_instance;

/* ------------------------------------------------------------------ */
/* Pure helpers                                                        */
/* ------------------------------------------------------------------ */

int fridge_memo_page_count(const fridge_memo_page_t *r)
{
    return r ? r->data.count : 0;
}

int fridge_memo_page_pages(const fridge_memo_page_t *r)
{
    if (!r || r->data.count <= 0)
        return 1; /* empty state still shows page 1/N=1/1 */
    return (r->data.count + FRIDGE_MEMO_ROWS_PER_SCREEN - 1) / FRIDGE_MEMO_ROWS_PER_SCREEN;
}

int fridge_memo_page_rows_on_page(const fridge_memo_page_t *r, int page_index)
{
    if (!r || page_index < 0 || page_index >= fridge_memo_page_pages(r))
        return 0;
    int first = page_index * FRIDGE_MEMO_ROWS_PER_SCREEN;
    int remain = r->data.count - first;
    if (remain <= 0)
        return 0;
    return remain < FRIDGE_MEMO_ROWS_PER_SCREEN ? remain : FRIDGE_MEMO_ROWS_PER_SCREEN;
}

static bool clock_valid(struct tm *out)
{
    time_t t = time(NULL);
    struct tm tmv;
    if (!localtime_r(&t, &tmv) || tmv.tm_year + 1900 < 2024)
        return false;
    if (out)
        *out = tmv;
    return true;
}

/* ------------------------------------------------------------------ */
/* Data interface                                                      */
/* ------------------------------------------------------------------ */

void fridge_memo_page_update(page_renderer_t *self, const fridge_memo_snapshot_t *data)
{
    fridge_memo_page_t *r = (fridge_memo_page_t *)self;
    if (!r || !data)
        return;
    r->data = *data; /* caller (app_sync) passes an already-sorted snapshot */
    r->page_index = 0; /* voice/manual ops reset to page 1 (design §4.3 r8) */
    r->showing_delete = false;
    r->base.needs_full_refresh_flag = true;
}

void fridge_memo_page_set_footer_message(page_renderer_t *self, const char *msg)
{
    fridge_memo_page_t *r = (fridge_memo_page_t *)self;
    if (!r)
        return;
    snprintf(r->footer_message, sizeof(r->footer_message), "%s", msg ? msg : "");
    r->base.needs_full_refresh_flag = true;
}

void fridge_memo_page_set_offline(page_renderer_t *self, bool offline)
{
    fridge_memo_page_t *r = (fridge_memo_page_t *)self;
    if (!r)
        return;
    r->offline = offline;
    r->base.needs_full_refresh_flag = true;
}

void fridge_memo_page_set_delete_request_handler(page_renderer_t *self, void (*cb)(const char *item_id, void *ctx),
                                                 void *ctx)
{
    fridge_memo_page_t *r = (fridge_memo_page_t *)self;
    if (!r)
        return;
    r->delete_request_cb = cb;
    r->delete_request_ctx = ctx;
}

/* ------------------------------------------------------------------ */
/* Rendering                                                           */
/* ------------------------------------------------------------------ */

static void render_summary(uint8_t *fb, int width, int height, const fridge_memo_page_t *r, const struct tm *today)
{
    const int y = STYLE_STATUS_BAR_HEIGHT;
    const rawdraw_paint_style_t text = rawdraw_theme_style(THEME_TOKEN_TEXT_SECONDARY);
    const rawdraw_paint_style_t danger = rawdraw_theme_style(THEME_TOKEN_DANGER);
    const rawdraw_paint_style_t warning = rawdraw_theme_style(THEME_TOKEN_WARNING);

    int expired = fridge_memo_count_by_status(&r->data, FRIDGE_MEMO_STATUS_EXPIRED, today);
    int near = fridge_memo_count_by_status(&r->data, FRIDGE_MEMO_STATUS_NEAR, today);

    char buf[64];
    int x = FM_PANEL_PAD + 4;
    if (expired > 0) {
        rawdraw_fill_rect(fb, width, height, (rawdraw_rect_t){x, y + 6, 8, 10}, danger.fg);
        x += 12;
        snprintf(buf, sizeof(buf), "%d 过期", expired);
        rawdraw_draw_text(fb, width, height, x, y + 3, buf, fm_font, danger.fg);
        x += rawdraw_measure_text_width(buf, fm_font) + 10;
    }
    if (near > 0) {
        rawdraw_fill_rect(fb, width, height, (rawdraw_rect_t){x, y + 6, 8, 10}, warning.fg);
        x += 12;
        snprintf(buf, sizeof(buf), "%d 临期", near);
        rawdraw_draw_text(fb, width, height, x, y + 3, buf, fm_font, text.fg);
        x += rawdraw_measure_text_width(buf, fm_font) + 10;
    }
    snprintf(buf, sizeof(buf), "共 %d 项", r->data.count);
    rawdraw_draw_text(fb, width, height, x, y + 3, buf, fm_font, text.fg);

    rawdraw_draw_hline(fb, width, height, y + FM_SUMMARY_H - 1, FM_PANEL_PAD, width - FM_PANEL_PAD,
                       rawdraw_theme_style(THEME_TOKEN_BORDER).fg);
}

static void render_status_text(char *out, size_t len, const fridge_memo_item_t *it, const struct tm *today)
{
    if (!today) {
        snprintf(out, len, "%s", it->expires_at[0] ? "—" : "");
        return;
    }
    if (it->expires_at[0] == '\0') {
        out[0] = '\0'; /* no expiry -> no status anchor */
        return;
    }
    int days = fridge_memo_days_until(it->expires_at, today);
    if (days == -1000) {
        out[0] = '\0'; /* invalid date -> no status anchor */
        return;
    }
    if (days < 0)
        snprintf(out, len, "已过期 %d 天", -days);
    else
        snprintf(out, len, "剩 %d 天", days);
}

static rawdraw_color_t status_color(fridge_memo_status_t st)
{
    switch (st) {
    case FRIDGE_MEMO_STATUS_EXPIRED:
        return rawdraw_theme_style(THEME_TOKEN_DANGER).fg;
    case FRIDGE_MEMO_STATUS_NEAR:
        return rawdraw_theme_style(THEME_TOKEN_WARNING).fg;
    default:
        return rawdraw_theme_style(THEME_TOKEN_TEXT_PRIMARY).fg;
    }
}

static void render_row(uint8_t *fb, int width, int height, int y, const fridge_memo_item_t *it, const struct tm *today)
{
    const int list_x = FM_PANEL_PAD;
    const rawdraw_color_t secondary = rawdraw_theme_style(THEME_TOKEN_TEXT_SECONDARY).fg;

    /* left color bar (status color; the page itself is the selection) */
    rawdraw_color_t bar = status_color(fridge_memo_derive_status(it, today));
    rawdraw_fill_rect(fb, width, height, (rawdraw_rect_t){list_x, y + 4, FM_COLOR_BAR_W, FM_ROW_H - 8}, bar);

    /* line 1: name (Medium 24) + quantity (16) left, status right-aligned.
     * Budget the whole left side against the status anchor so the two can
     * never collide: the quantity drops first on overflow, then the name
     * ellipsizes into whatever room is left. */
    char status[32];
    render_status_text(status, sizeof(status), it, today);
    int sw = status[0] ? rawdraw_measure_text_width(status, fm_font) : 0;
    int budget = width - FM_PANEL_PAD - sw - 24;
    if (budget < 60)
        budget = 60; /* pathological status width: keep a readable floor */

    char name_fit[FRIDGE_MEMO_NAME_LEN + 4];
    int name_max = budget < 170 ? budget : 170;
    ui_text_fit_to_width(it->name, fm_title_font, name_max, name_fit, sizeof(name_fit));
    rawdraw_draw_text(fb, width, height, list_x + 10, y + 6, name_fit, fm_title_font, bar);
    int name_w = rawdraw_measure_text_width(name_fit, fm_title_font);

    if (it->quantity[0]) {
        char qty[FRIDGE_MEMO_QTY_LEN + 8];
        snprintf(qty, sizeof(qty), "（%s）", it->quantity);
        char qty_fit[FRIDGE_MEMO_QTY_LEN + 8];
        ui_text_fit_to_width(qty, fm_font, 96, qty_fit, sizeof(qty_fit));
        if (name_w + 4 + rawdraw_measure_text_width(qty_fit, fm_font) <= budget)
            rawdraw_draw_text(fb, width, height, list_x + 10 + name_w + 4, y + 14, qty_fit, fm_font, secondary);
    }

    if (status[0])
        rawdraw_draw_text(fb, width, height, width - FM_PANEL_PAD - 6 - sw, y + 12, status, fm_font, bar);

    /* line 2: "7/28 放入 · 已存 16 天" (secondary) */
    char meta[64];
    int stored = fridge_memo_days_since(it->added_at, today);
    char date_label[16];
    /* added_at "2026-07-28" -> "7/28" */
    if (strlen(it->added_at) >= 10)
        snprintf(date_label, sizeof(date_label), "%d/%d", atoi(it->added_at + 5), atoi(it->added_at + 8));
    else
        date_label[0] = '\0';
    if (!today)
        snprintf(meta, sizeof(meta), "%s 放入", date_label);
    else if (stored > 0)
        snprintf(meta, sizeof(meta), "%s 放入 · 已存 %d 天", date_label, stored);
    else
        snprintf(meta, sizeof(meta), "%s 放入", date_label);
    rawdraw_draw_text(fb, width, height, list_x + 10, y + 34, meta, fm_font, secondary);
}

static void render_footer(uint8_t *fb, int width, int height, const fridge_memo_page_t *r)
{
    const int y = FM_FOOTER_Y;
    const rawdraw_paint_style_t text = rawdraw_theme_style(THEME_TOKEN_TEXT_PRIMARY);
    const rawdraw_paint_style_t secondary = rawdraw_theme_style(THEME_TOKEN_TEXT_SECONDARY);
    const rawdraw_paint_style_t warning = rawdraw_theme_style(THEME_TOKEN_WARNING);
    const rawdraw_paint_style_t danger = rawdraw_theme_style(THEME_TOKEN_DANGER);

    rawdraw_draw_hline(fb, width, height, y, FM_PANEL_PAD, width - FM_PANEL_PAD,
                       rawdraw_theme_style(THEME_TOKEN_BORDER).fg);

    char page_label[32];
    snprintf(page_label, sizeof(page_label), "第 %d/%d 页", r->page_index + 1, fridge_memo_page_pages(r));
    int pw = rawdraw_measure_text_width(page_label, fm_font);
    rawdraw_draw_text(fb, width, height, width / 2 - pw / 2, y + 5, page_label, fm_font, secondary.fg);
    rawdraw_draw_text(fb, width, height, width - FM_PANEL_PAD - 6 - rawdraw_measure_text_width("UP/DN 翻页", fm_font),
                      y + 5, "UP/DN 翻页", fm_font, text.fg);

    /* left slot priority: result > offline banner > default hint (design §5.4).
     * Budget clear of the centered page label (its slot starts ~width/2 - 90). */
    char left[FRIDGE_MEMO_FOOTER_TEXT_LEN];
    rawdraw_color_t left_color = text.fg;
    if (r->footer_message[0]) {
        snprintf(left, sizeof(left), "%s", r->footer_message);
        left_color = danger.fg;
    } else if (r->offline) {
        snprintf(left, sizeof(left), "离线 · 缓存 %s", r->data.updated_at);
        left_color = warning.fg;
    } else {
        snprintf(left, sizeof(left), "BOOT 双击删除");
    }
    int left_max = width / 2 - 90;
    if (left_max < 24)
        left_max = 24;
    char left_fit[FRIDGE_MEMO_FOOTER_TEXT_LEN];
    ui_text_fit_to_width(left, fm_font, left_max, left_fit, sizeof(left_fit));
    rawdraw_draw_text(fb, width, height, FM_PANEL_PAD + 6, y + 5, left_fit, fm_font, left_color);
}

static void render_empty(uint8_t *fb, int width, int height)
{
    const rawdraw_paint_style_t secondary = rawdraw_theme_style(THEME_TOKEN_TEXT_SECONDARY);
    const rawdraw_paint_style_t text = rawdraw_theme_style(THEME_TOKEN_TEXT_PRIMARY);
    int cx = width / 2;
    int icon_w = rawdraw_measure_text_width(FONT_ZECTRIX_ICON_MIC, &font_zectrix_16_1);
    rawdraw_draw_text(fb, width, height, cx - icon_w / 2, 120, FONT_ZECTRIX_ICON_MIC, &font_zectrix_16_1,
                      rawdraw_theme_style(THEME_TOKEN_ACCENT).fg);
    const char *l1 = "冰箱备忘还是空的";
    int w1 = rawdraw_measure_text_width(l1, fm_title_font);
    rawdraw_draw_text(fb, width, height, cx - w1 / 2, 160, l1, fm_title_font, text.fg);
    const char *l2 = "在设置中配置冰箱后端后，从后端同步条目";
    int w2 = rawdraw_measure_text_width(l2, fm_font);
    rawdraw_draw_text(fb, width, height, cx - w2 / 2, 200, l2, fm_font, secondary.fg);
}

static void render_delete_overlay(uint8_t *fb, int width, int height, const fridge_memo_page_t *r,
                                  const struct tm *today)
{
    /* modal frame (settings_page_clear_dialog_region pattern) */
    const rawdraw_rect_t box = {40, 70, width - 80, 170};
    const rawdraw_paint_style_t border = rawdraw_theme_style(THEME_TOKEN_BORDER);
    rawdraw_fill_rect(fb, width, height, box, rawdraw_theme_style(THEME_TOKEN_BACKGROUND_PRIMARY).bg);
    rawdraw_draw_rect_border(fb, width, height, box, 2, border.fg);

    const char *title = "UP/DN 选 · BOOT 删 · 双击取消";
    int tw = rawdraw_measure_text_width(title, fm_font);
    rawdraw_draw_text(fb, width, height, width / 2 - tw / 2, box.y + 10, title, fm_font,
                      rawdraw_theme_style(THEME_TOKEN_TEXT_SECONDARY).fg);

    int rows = fridge_memo_page_rows_on_page(r, r->page_index);
    int first = r->page_index * FRIDGE_MEMO_ROWS_PER_SCREEN;
    for (int i = 0; i < rows; ++i) {
        int ry = box.y + 36 + i * 32;
        const rawdraw_paint_style_t selected = rawdraw_theme_style(THEME_TOKEN_SELECTED);
        if (i == r->delete_focus)
            rawdraw_fill_rect(fb, width, height, (rawdraw_rect_t){box.x + 6, ry, box.w - 12, 30}, selected.bg);
        const fridge_memo_item_t *it = &r->data.items[first + i];
        char label[FRIDGE_MEMO_NAME_LEN + FRIDGE_MEMO_QTY_LEN + 8];
        snprintf(label, sizeof(label), "%s%s%s", it->name, it->quantity[0] ? "（" : "",
                 it->quantity[0] ? it->quantity : "");
        if (it->quantity[0])
            strncat(label, "）", sizeof(label) - strlen(label) - 1);
        char status[32];
        render_status_text(status, sizeof(status), it, today);
        char line[128];
        snprintf(line, sizeof(line), "%s  %s", label, status);
        /* contract-max lines must stay inside the modal frame */
        char line_fit[128];
        ui_text_fit_to_width(line, fm_font, box.w - 28, line_fit, sizeof(line_fit));
        rawdraw_color_t ink = status_color(fridge_memo_derive_status(it, today));
        if (i == r->delete_focus)
            ink = selected.fg; /* inverted text on the selected fill */
        rawdraw_draw_text(fb, width, height, box.x + 14, ry + 7, line_fit, fm_font, ink);
    }
}

void fridge_memo_page_render(page_renderer_t *self, uint8_t *fb, int width, int height)
{
    fridge_memo_page_t *r = (fridge_memo_page_t *)self;
    if (!r || !fb)
        return;

    rawdraw_paint_style_t bg = rawdraw_theme_style(THEME_TOKEN_BACKGROUND_PRIMARY);
    rawdraw_draw_styled_rect(
        fb, width, height, (rawdraw_rect_t){0, STYLE_STATUS_BAR_HEIGHT, width, height - STYLE_STATUS_BAR_HEIGHT}, &bg);

    struct tm today;
    bool have_time = clock_valid(&today);
    const struct tm *tm_ptr = have_time ? &today : NULL;

    if (r->data.count == 0) {
        render_empty(fb, width, height);
        render_footer(fb, width, height, r);
    } else {
        render_summary(fb, width, height, r, tm_ptr);
        int first = r->page_index * FRIDGE_MEMO_ROWS_PER_SCREEN;
        int y = STYLE_STATUS_BAR_HEIGHT + FM_SUMMARY_H;
        for (int i = 0; i < FRIDGE_MEMO_ROWS_PER_SCREEN && first + i < r->data.count; ++i) {
            render_row(fb, width, height, y, &r->data.items[first + i], tm_ptr);
            y += FM_ROW_H;
        }
        render_footer(fb, width, height, r);
    }

    if (r->showing_delete)
        render_delete_overlay(fb, width, height, r, tm_ptr);

    r->base.needs_full_refresh_flag = false;
}

/* ------------------------------------------------------------------ */
/* Input                                                               */
/* ------------------------------------------------------------------ */

bool fridge_memo_page_handle_input(page_renderer_t *self, const ui_button_event_t *event)
{
    fridge_memo_page_t *r = (fridge_memo_page_t *)self;
    if (!r || !event)
        return false;

    if (r->showing_delete) {
        int rows = fridge_memo_page_rows_on_page(r, r->page_index);
        switch (event->type) {
        case BTN_UP_CLICK:
            r->delete_focus = (r->delete_focus + rows - 1) % rows; /* wrap */
            r->base.needs_full_refresh_flag = true;
            return true;
        case BTN_DOWN_CLICK:
            r->delete_focus = (r->delete_focus + 1) % rows;
            r->base.needs_full_refresh_flag = true;
            return true;
        case BTN_BOOT_CLICK: {
            int first = r->page_index * FRIDGE_MEMO_ROWS_PER_SCREEN;
            const fridge_memo_item_t *it = &r->data.items[first + r->delete_focus];
            if (r->delete_request_cb)
                r->delete_request_cb(it->id, r->delete_request_ctx);
            r->showing_delete = false; /* close on request; result arrives via callback */
            r->base.needs_full_refresh_flag = true;
            return true;
        }
        case BTN_BOOT_DOUBLE_CLICK:
            r->showing_delete = false;
            r->base.needs_full_refresh_flag = true;
            return true;
        default:
            return false; /* BOOT long press and everything else ignored (design §5.4) */
        }
    }

    switch (event->type) {
    case BTN_UP_CLICK:
        if (r->page_index <= 0)
            return false; /* no empty refresh at boundary */
        --r->page_index;
        r->base.needs_full_refresh_flag = true;
        return true;
    case BTN_DOWN_CLICK:
        if (r->page_index >= fridge_memo_page_pages(r) - 1)
            return false;
        ++r->page_index;
        r->base.needs_full_refresh_flag = true;
        return true;
    case BTN_BOOT_CLICK:
        data_refresh_request(UI_PAGE_FRIDGE_MEMO);
        return false; /* no immediate visual change; refresh comes with data */
    case BTN_BOOT_DOUBLE_CLICK:
        if (r->data.count == 0)
            return false;
        r->showing_delete = true;
        r->delete_focus = 0;
        r->base.needs_full_refresh_flag = true;
        return true;
    default:
        return false;
    }
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

void fridge_memo_page_init(page_renderer_t *self, int width, int height)
{
    fridge_memo_page_t *r = (fridge_memo_page_t *)self;
    if (!r)
        return;
    memset(&r->data, 0, sizeof(r->data));
    r->page_index = 0;
    r->footer_message[0] = '\0';
    r->offline = false;
    r->showing_delete = false;
    r->delete_focus = 0;
    r->base.width = width;
    r->base.height = height;
    r->base.needs_full_refresh_flag = true;
}

static void fridge_memo_page_enter(page_renderer_t *self)
{
    (void)self;
    data_refresh_request(UI_PAGE_FRIDGE_MEMO); /* cache-first, then async GET (design §6.3) */
}

const page_renderer_ops_t fridge_memo_page_ops = {
    .init = fridge_memo_page_init,
    .enter = fridge_memo_page_enter,
    .exit = NULL,
    .render = fridge_memo_page_render,
    .handle_input = fridge_memo_page_handle_input,
    .get_dirty_rect = NULL,
    .needs_full_refresh = NULL,
    .mark_full_refresh = NULL,
    .clear_full_refresh_flag = NULL,
    .append_text = NULL,
    .begin_stream = NULL,
    .end_stream = NULL,
};

PAGE_REGISTER(UI_PAGE_FRIDGE_MEMO, "冰箱备忘", NULL, true, 25, &fridge_memo_page_ops, &s_fridge_memo_instance.base);
