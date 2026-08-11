/**
 * @file lifebar_renderer.c
 * @brief Life progress page renderer — C port of C++ rawdraw::LifeBarRenderer.
 *
 * Large circular gauge showing life percentage, age, days elapsed/remaining,
 * weekends remaining, and a motivational quote.
 * Default: birthdate 1990-01-01, 80-year lifespan.
 */
#include "lifebar_page.h"
#include "page_registry.h"

#include "rawdraw_ext.h"
#include "theme.h"
#include "style.h"
#include "layout.h"
#include "progress_bar.h"

#include <stdio.h>
#include <time.h>

#define LIFEBAR_BIRTH_YEAR 1997
#define LIFEBAR_BIRTH_MONTH 4
#define LIFEBAR_BIRTH_DAY 21
#define LIFEBAR_EXPECTED_LIFESPAN_YEARS 80

/* Motivational quotes (rotated by index) */
static const char *kLifebarQuotes[] = {
    "时间是最公平的，\n每人每天都只有24小时", "余生很长，何必慌张；\n余生很短，何必平凡",
    "把每一天当成\n生命中最后一天来过",       "种一棵树最好的时间\n是十年前，其次是现在",
    "人生没有白走的路，\n每一步都算数",
};
#define LIFEBAR_NUM_QUOTES ((int)(sizeof(kLifebarQuotes) / sizeof(kLifebarQuotes[0])))

static const lv_font_t *const kLifebarTitleFont = &SourceHanSansSC_Medium_slim;
static const lv_font_t *const kLifebarBodyFont  = &SourceHanSansSC_Regular_slim;
static const lv_font_t *const kLifebarSmallFont = &SourceHanSansSC_Regular_slim;

/* Round down to the nearest multiple of 8 (e-paper anti-aliasing grid). */
static int align_x8(int x)
{
    return (x + 7) & ~7;
}

/* ------------------------------------------------------------------ */
/* Data calculation                                                    */
/* ------------------------------------------------------------------ */

static bool is_leap(int y)
{
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static int days_in_month(int y, int m)
{
    static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 1 && is_leap(y))
        return 29;
    return d[m];
}

static void update_stats(lifebar_page_t *r)
{
    time_t    now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);

    const int cur_year  = tm_now.tm_year + 1900;
    const int cur_month = tm_now.tm_mon + 1; /* 1-based */
    const int cur_day   = tm_now.tm_mday;

    /* Days elapsed since birth */
    int total_days = 0;
    for (int y = LIFEBAR_BIRTH_YEAR; y < cur_year; y++) {
        total_days += is_leap(y) ? 366 : 365;
    }
    for (int m = 0; m < cur_month - 1; m++) {
        total_days += days_in_month(cur_year, m);
    }
    if (cur_year == LIFEBAR_BIRTH_YEAR) {
        total_days += cur_day - LIFEBAR_BIRTH_DAY;
    } else {
        total_days += cur_day;
    }
    if (total_days < 0)
        total_days = 0;

    /* Total lifespan in days */
    int lifespan_days = 0;
    for (int y = LIFEBAR_BIRTH_YEAR; y < LIFEBAR_BIRTH_YEAR + LIFEBAR_EXPECTED_LIFESPAN_YEARS; y++) {
        lifespan_days += is_leap(y) ? 366 : 365;
    }

    /* Age */
    r->age_years  = cur_year - LIFEBAR_BIRTH_YEAR;
    r->age_months = cur_month - LIFEBAR_BIRTH_MONTH;
    if (r->age_months < 0) {
        r->age_years--;
        r->age_months += 12;
    }
    if (cur_day < LIFEBAR_BIRTH_DAY) {
        r->age_months--;
        if (r->age_months < 0) {
            r->age_years--;
            r->age_months += 12;
        }
    }

    r->days_elapsed   = total_days;
    r->days_remaining = lifespan_days - total_days;
    if (r->days_remaining < 0)
        r->days_remaining = 0;

    /* Weekends remaining (roughly 2/7 of remaining days) */
    r->weekends_remaining = (r->days_remaining * 2) / 7;

    /* Life percentage */
    if (lifespan_days > 0) {
        r->life_pct = (total_days * 100) / lifespan_days;
    }
    if (r->life_pct > 100)
        r->life_pct = 100;
}

/* ------------------------------------------------------------------ */
/* Rendering sections                                                  */
/* ------------------------------------------------------------------ */

static void render_header(lifebar_page_t *r, uint8_t *fb, int width, int height, int y)
{
    const rawdraw_color_t text      = rawdraw_theme_color_for(THEME_TOKEN_TEXT_PRIMARY);
    const rawdraw_color_t secondary = rawdraw_theme_color_for(THEME_TOKEN_TEXT_SECONDARY);

    /* Title */
    const char *title   = "人生进度";
    const int   title_w = rawdraw_measure_text_width(title, r->title_font);
    rawdraw_draw_text(fb, width, height, align_x8((width - title_w) / 2), y, title, r->title_font, text);

    /* Subtitle */
    const char *sub   = "每一天都值得珍惜";
    const int   sub_w = rawdraw_measure_text_width(sub, r->small_font);
    const int   sub_y = y + r->title_font->line_height + STYLE_SPACING_XXS;
    rawdraw_draw_text(fb, width, height, align_x8((width - sub_w) / 2), sub_y, sub, r->small_font, secondary);
}

static void render_quote(lifebar_page_t *r, uint8_t *fb, int width, int height, int y)
{
    if (y + r->small_font->line_height > height - STYLE_SPACING_SM) {
        return; /* Not enough space */
    }

    /* Pick quote by day (rotates daily) */
    time_t    now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    const int   idx   = (tm_buf.tm_yday) % LIFEBAR_NUM_QUOTES;
    const char *quote = kLifebarQuotes[idx];

    /* Draw quote lines */
    char        line[64];
    int         line_idx  = 0;
    const int   max_lines = 2;
    const char *p         = quote;

    while (*p && line_idx < max_lines) {
        int i = 0;
        while (*p && *p != '\n' && i < 63) {
            line[i++] = *p++;
        }
        line[i] = '\0';
        if (*p == '\n')
            p++;

        const int w = rawdraw_measure_text_width(line, r->small_font);
        const int x = align_x8((width - w) / 2);
        rawdraw_draw_text(fb, width, height, x, y + line_idx * (r->small_font->line_height + STYLE_SPACING_XS), line,
                          r->small_font, rawdraw_theme_color_for(THEME_TOKEN_TEXT_SECONDARY));
        line_idx++;
    }
}

static void render_gauge(lifebar_page_t *r, uint8_t *fb, int width, int height, int y_start)
{
    const rawdraw_paint_style_t progress_style = rawdraw_theme_component(ROLE_PROGRESS);
    const rawdraw_color_t       text           = rawdraw_theme_color_for(THEME_TOKEN_TEXT_PRIMARY);
    const rawdraw_color_t       secondary      = rawdraw_theme_color_for(THEME_TOKEN_TEXT_SECONDARY);
    const rawdraw_color_t       accent         = rawdraw_theme_color_for(THEME_TOKEN_ACCENT);

    /* Gauge geometry */
    const int gauge_r         = 70;
    const int gauge_thickness = 8;
    const int cx              = width / 2;
    int       cy              = y_start + gauge_r + 5;

    /* If the gauge doesn't fit within the content area, shift it up */
    const int gauge_bottom   = cy + gauge_r;
    const int content_bottom = r->base.height - STYLE_SPACING_SM;
    if (gauge_bottom > content_bottom) {
        cy = content_bottom - gauge_r;
    }

    /* === Draw circular progress === */
    const rawdraw_point_t center = {cx, cy};
    rawdraw_draw_circular_progress(fb, width, height, center, gauge_r, gauge_thickness, r->life_pct, progress_style.bg,
                                   progress_style.fg);

    /* === Center text: percentage number only (no overlapping text) === */
    char pct_buf[16];
    snprintf(pct_buf, sizeof(pct_buf), "%d%%", r->life_pct);
    const int pct_w = rawdraw_measure_text_width(pct_buf, r->title_font);
    const int pct_x = align_x8(cx - pct_w / 2);
    const int pct_y = rawdraw_layout_ink_centered_text_top_y(r->title_font, pct_buf, cy, 0);
    rawdraw_draw_text(fb, width, height, pct_x, pct_y, pct_buf, r->title_font, accent);

    /* === Stats below gauge === */
    int       stats_y      = cy + gauge_r + gauge_thickness + STYLE_SPACING_MD;
    const int bottom_limit = r->base.height - STYLE_SPACING_SM;

    /* Quote needs at least 2 lines */
    const int quote_min_h = r->small_font->line_height * 2 + STYLE_SPACING_XS;

    char buf[64];

    /* Line 1: Age (consistent with gauge percentage) */
    if (stats_y + r->small_font->line_height <= bottom_limit) {
        snprintf(buf, sizeof(buf), "%d岁%d月  已过%d%%", r->age_years, r->age_months, r->life_pct);
        const int w = rawdraw_measure_text_width(buf, r->small_font);
        rawdraw_draw_text(fb, width, height, align_x8((width - w) / 2), stats_y, buf, r->small_font, text);
        stats_y += r->small_font->line_height + STYLE_SPACING_XS;
    }

    /* Line 2: Days remaining */
    if (stats_y + r->small_font->line_height <= bottom_limit) {
        snprintf(buf, sizeof(buf), "剩余天数 %d天", r->days_remaining);
        const int w = rawdraw_measure_text_width(buf, r->small_font);
        rawdraw_draw_text(fb, width, height, align_x8((width - w) / 2), stats_y, buf, r->small_font, secondary);
        stats_y += r->small_font->line_height + STYLE_SPACING_XS;
    }

    /* Line 3: Weekends remaining */
    if (stats_y + r->small_font->line_height <= bottom_limit) {
        snprintf(buf, sizeof(buf), "剩余周末 %d个", r->weekends_remaining);
        const int w = rawdraw_measure_text_width(buf, r->small_font);
        rawdraw_draw_text(fb, width, height, align_x8((width - w) / 2), stats_y, buf, r->small_font, secondary);
        stats_y += r->small_font->line_height + STYLE_SPACING_XS;
    }

    /* === Motivational quote at bottom — only if enough room === */
    const int quote_start_y = stats_y + STYLE_SPACING_XS;
    if (quote_start_y + quote_min_h <= bottom_limit) {
        render_quote(r, fb, width, height, quote_start_y);
    } else if (stats_y + quote_min_h <= bottom_limit) {
        /* Try squeeze: skip the gap, draw directly */
        render_quote(r, fb, width, height, stats_y);
    }
    /* else: not enough space, skip the quote entirely */
}

/* ------------------------------------------------------------------ */
/* PageRenderer vtable                                                 */
/* ------------------------------------------------------------------ */

void lifebar_page_init(page_renderer_t *self, int width, int height)
{
    lifebar_page_t *r               = (lifebar_page_t *)self;
    r->base.width                   = width;
    r->base.height                  = height;
    r->base.needs_full_refresh_flag = true;
    r->title_font                   = kLifebarTitleFont;
    r->body_font                    = kLifebarBodyFont;
    r->small_font                   = kLifebarSmallFont;
    r->age_years                    = 0;
    r->age_months                   = 0;
    r->days_elapsed                 = 0;
    r->days_remaining               = 0;
    r->weekends_remaining           = 0;
    r->life_pct                     = 0;
    r->visible                      = true;
    update_stats(r);
}

/* Page gained focus: request a redraw but keep the visible state. */
static void lifebar_page_enter(page_renderer_t *self)
{
    lifebar_page_t *r = (lifebar_page_t *)self;
    if (!r)
        return;
    r->base.needs_full_refresh_flag = true;
}

void lifebar_page_render(page_renderer_t *self, uint8_t *fb, int width, int height)
{
    lifebar_page_t *r = (lifebar_page_t *)self;
    if (!fb)
        return;
    const rawdraw_color_t text      = rawdraw_theme_color_for(THEME_TOKEN_TEXT_PRIMARY);
    const rawdraw_color_t secondary = rawdraw_theme_color_for(THEME_TOKEN_TEXT_SECONDARY);

    if (!r->visible) {
        /* Show hidden placeholder */
        const char *msg   = "人生进度页已隐藏";
        const int   msg_w = rawdraw_measure_text_width(msg, r->small_font);
        const int   msg_x = align_x8((width - msg_w) / 2);
        const int   msg_y = rawdraw_layout_ink_centered_text_top_y(r->small_font, msg, height / 2, 0);
        rawdraw_draw_text(fb, width, height, msg_x, msg_y, msg, r->small_font, text);

        const char *hint   = "在设置中重新开启";
        const int   hint_w = rawdraw_measure_text_width(hint, r->small_font);
        const int   hint_x = align_x8((width - hint_w) / 2);
        const int   hint_y = msg_y + r->small_font->line_height + STYLE_SPACING_SM;
        rawdraw_draw_text(fb, width, height, hint_x, hint_y, hint, r->small_font, secondary);
        r->base.needs_full_refresh_flag = false;
        return;
    }

    update_stats(r);

    int y = STYLE_STATUS_BAR_HEIGHT + STYLE_SPACING_MD;

    /* === Header === */
    render_header(r, fb, width, height, y);
    y += r->title_font->line_height + STYLE_SPACING_XXS;

    /* === Circular gauge (uses the remaining vertical space) === */
    render_gauge(r, fb, width, height, y);

    r->base.needs_full_refresh_flag = false;
}

bool lifebar_page_handle_input(page_renderer_t *self, const ui_button_event_t *event)
{
    /* No interactive navigation needed yet — page is informational */
    (void)self;
    (void)event;
    return false;
}

/* ------------------------------------------------------------------ */
/* Data interface                                                      */
/* ------------------------------------------------------------------ */

void lifebar_page_set_visible(page_renderer_t *self, bool visible)
{
    lifebar_page_t *r = (lifebar_page_t *)self;
    if (r->visible == visible)
        return;
    r->visible                      = visible;
    r->base.needs_full_refresh_flag = true;
}

bool lifebar_page_is_visible(const page_renderer_t *self)
{
    const lifebar_page_t *r = (const lifebar_page_t *)self;
    return r->visible;
}

/* ------------------------------------------------------------------ */
/* vtable instance                                                     */
/* ------------------------------------------------------------------ */

EXT_RAM_BSS_ATTR lifebar_page_t s_lifebar_instance;

const page_renderer_ops_t lifebar_page_ops = {
    .init                    = lifebar_page_init,
    .enter                   = lifebar_page_enter,
    .render                  = lifebar_page_render,
    .handle_input            = lifebar_page_handle_input,
    .get_dirty_rect          = NULL,
    .needs_full_refresh      = NULL,
    .mark_full_refresh       = NULL,
    .clear_full_refresh_flag = NULL,
    .append_text             = NULL,
    .begin_stream            = NULL,
    .end_stream              = NULL,
};

PAGE_REGISTER(UI_PAGE_LIFEBAR, "人生进度", NULL, true, 100, &lifebar_page_ops,
              &s_lifebar_instance.base);
