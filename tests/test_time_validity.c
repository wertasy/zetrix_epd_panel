/**
 * @file test_time_validity.c
 * @brief Host tests for the RTC time-validity guards
 *        (docs/rtc-time-validity-plan.md, success criteria 1/2/4).
 *
 * Covers:
 *   - pcf8563_regs_time_plausible boundaries (VL, year window)
 *   - time / calendar-nav window helpers
 *   - page_runtime time-retry counter (0 -> max -> give up -> clear)
 *   - calendar_page guards with an injected clock:
 *       epoch clock            -> today=0, NVS fallback rejected, year=0
 *       polluted NVS (2000/1970/2019/2051) -> rejected (placeholder path)
 *       valid NVS + epoch clock -> keeps last valid month (old image)
 *       valid clock             -> today captured, month = today
 *       UP/DOWN clamped at the nav window edges
 *       BOOT click with no valid clock -> ignored (no almanac)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#include "page_registry.h"
#include "page_runtime.h"
#include "calendar_page.h"
#include "nvs_state.h"
#include "rtc_time_valid.h"

/* Dummy font symbols referenced by page renderers (same trick as
 * test_page_runtime.c). */
const lv_font_t SourceHanSansSC_Regular_slim;
const lv_font_t SourceHanSansSC_Medium_slim;
const lv_font_t font_zectrix_16_1;
const lv_font_t font_zectrix_48_1;
const lv_font_t weather_icons_16;
const lv_font_t weather_icons_48;
const lv_font_t fa_settings_16;

static int s_fail = 0;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
            s_fail++;                                                                                                 \
        }                                                                                                              \
    } while (0)

/* ---- injected clock --------------------------------------------------- */
static time_t s_fake_now = 0;
static time_t fake_clock(void)
{
    return s_fake_now;
}

static void set_fake(const char *iso)
{
    struct tm t = {0};
    sscanf(iso, "%d-%d-%d %d:%d:%d", &t.tm_year, &t.tm_mon, &t.tm_mday, &t.tm_hour, &t.tm_min, &t.tm_sec);
    t.tm_year -= 1900;
    t.tm_mon -= 1;
    s_fake_now = mktime(&t);
}

/* Build a raw PCF8563 register image (BCD) from a broken-down date. */
static void regs_of(const char *ymd_hms, uint8_t regs[7])
{
    int y, mo, d, h, mi, s;
    sscanf(ymd_hms, "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &s);
    regs[0] = (uint8_t)(((s / 10) << 4) | (s % 10));
    regs[1] = (uint8_t)(((mi / 10) << 4) | (mi % 10));
    regs[2] = (uint8_t)(((h / 10) << 4) | (h % 10));
    regs[3] = (uint8_t)(((d / 10) << 4) | (d % 10));
    regs[4] = 0;
    regs[5] = (uint8_t)(((mo / 10) << 4) | (mo % 10));
    regs[6] = (uint8_t)(((y % 100 / 10) << 4) | (y % 10));
}

static calendar_page_t *cal_page(void)
{
    return (calendar_page_t *)page_registry_get_instance(UI_PAGE_CALENDAR);
}

int main(void)
{
    /* Fresh key-value backing file so NVS state is deterministic. */
    remove("nvs_sim.txt");
    assert(nvs_state_init());
    page_registry_init();
    page_runtime_init();
    calendar_page_time_source = fake_clock;

    /* ---- 1. register plausibility boundaries (criterion 1) ------------ */
    {
        uint8_t regs[7];
        regs_of("2026-08-26 01:30:00", regs);
        CHECK(pcf8563_regs_time_plausible(regs) == true);

        regs_of("2020-01-01 00:00:00", regs);
        CHECK(pcf8563_regs_time_plausible(regs) == true);

        regs_of("2099-12-31 23:59:59", regs);
        CHECK(pcf8563_regs_time_plausible(regs) == true);

        /* PCF8563 power-on reset value: 2000-01-01 00:00 with VL set. */
        regs_of("2000-01-01 00:00:00", regs);
        regs[0] |= 0x80; /* VL */
        CHECK(pcf8563_regs_time_plausible(regs) == false);

        /* Same date, VL cleared: still outside the trust window. */
        regs[0] &= 0x7F;
        CHECK(pcf8563_regs_time_plausible(regs) == false);

        /* I2C garbage year 0x9A (invalid BCD but decodes to 110). */
        regs[6] = 0x9A;
        CHECK(pcf8563_regs_time_plausible(regs) == false);

        regs_of("2019-12-31 23:59:59", regs);
        CHECK(pcf8563_regs_time_plausible(regs) == false);
    }

    /* ---- 2. window helpers -------------------------------------------- */
    CHECK(time_year_is_plausible(2019) == false);
    CHECK(time_year_is_plausible(2020) == true);
    CHECK(time_year_is_plausible(2099) == true);
    CHECK(time_year_is_plausible(2100) == false);
    CHECK(calendar_nav_year_is_valid(2020) == true);
    CHECK(calendar_nav_year_is_valid(2050) == true);
    CHECK(calendar_nav_year_is_valid(2051) == false);
    CHECK(calendar_nav_year_is_valid(2019) == false);

    /* ---- 3. retry counter lifecycle (criterion 2 bound) ---------------- */
    page_runtime_time_retry_clear();
    CHECK(page_runtime_time_retry_count() == 0);
    for (int i = 1; i <= PAGE_RUNTIME_TIME_RETRY_MAX; i++)
        page_runtime_time_retry_increment();
    CHECK(page_runtime_time_retry_count() == PAGE_RUNTIME_TIME_RETRY_MAX);
    page_runtime_time_retry_clear();
    CHECK(page_runtime_time_retry_count() == 0);

    /* ---- 4. calendar init: epoch clock, clean NVS ---------------------- */
    set_fake("1970-01-01 00:00:00");
    calendar_page_init((page_renderer_t *)cal_page(), 300, 300);
    {
        calendar_page_t *r = cal_page();
        CHECK(r->today_year == 0 && r->today_month == 0 && r->today_day == 0);
        /* "cal_year" is a known nvs_state key: a clean install serves the
         * built-in default 2026/01 (a real, valid navigation month — not a
         * faked date). Today stays unknown; no fake date anywhere. */
        CHECK(r->year == 2026 && r->month == 1);
    }

    /* ---- 5. calendar init: polluted NVS entries (criterion 4) ---------- */
    const char *polluted[] = {"2000", "1970", "2019", "2051"};
    for (size_t i = 0; i < sizeof(polluted) / sizeof(polluted[0]); i++) {
        nvs_state_set_i32("cal_year", atoi(polluted[i]));
        nvs_state_set_i32("cal_month", 1);
        set_fake("1970-01-01 00:00:00");
        calendar_page_init((page_renderer_t *)cal_page(), 300, 300);
        calendar_page_t *r = cal_page();
        CHECK(r->year == 0); /* rejected -> placeholder, never the bad year */
        char msg[64];
        snprintf(msg, sizeof(msg), "polluted cal_year=%s rejected", polluted[i]);
        (void)msg;
    }
    /* Valid NVS entry survives an epoch clock (keep last valid month). */
    nvs_state_set_i32("cal_year", 2026);
    nvs_state_set_i32("cal_month", 8);
    set_fake("1970-01-01 00:00:00");
    calendar_page_init((page_renderer_t *)cal_page(), 300, 300);
    CHECK(cal_page()->year == 2026 && cal_page()->month == 8);
    CHECK(cal_page()->today_year == 0); /* but today stays unknown */

    /* ---- 6. valid clock ------------------------------------------------ */
    set_fake("2026-08-26 23:59:00");
    calendar_page_init((page_renderer_t *)cal_page(), 300, 300);
    {
        calendar_page_t *r = cal_page();
        CHECK(r->today_year == 2026 && r->today_month == 8 && r->today_day == 26);
        CHECK(r->year == 2026 && r->month == 8);
        CHECK(r->cal.today_year == 2026);
    }

    /* ---- 7. navigation clamped at the window edges (criterion 4) ------ */
    nvs_state_set_i32("cal_year", 2020);
    nvs_state_set_i32("cal_month", 1);
    set_fake("2026-08-26 12:00:00");
    calendar_page_init((page_renderer_t *)cal_page(), 300, 300);
    {
        ui_button_event_t ev = {.type = BTN_UP_CLICK};
        CHECK(calendar_page_handle_input((page_renderer_t *)cal_page(), &ev) == false);
        int32_t y = 0, m = 0;
        nvs_state_get_i32("cal_year", &y);
        nvs_state_get_i32("cal_month", &m);
        CHECK(y == 2020 && m == 1); /* NVS untouched at the edge */
    }
    nvs_state_set_i32("cal_year", 2050);
    nvs_state_set_i32("cal_month", 12);
    calendar_page_init((page_renderer_t *)cal_page(), 300, 300);
    {
        ui_button_event_t ev = {.type = BTN_DOWN_CLICK};
        CHECK(calendar_page_handle_input((page_renderer_t *)cal_page(), &ev) == false);
    }

    /* ---- 8. BOOT click with no valid clock: no almanac, no refresh ----- */
    set_fake("1970-01-01 00:00:00");
    calendar_page_init((page_renderer_t *)cal_page(), 300, 300);
    {
        ui_button_event_t ev = {.type = BTN_BOOT_CLICK};
        CHECK(calendar_page_handle_input((page_renderer_t *)cal_page(), &ev) == false);
        CHECK(cal_page()->show_almanac == false);
    }

    /* ---- 9. C-1 latch: note -> footer flag -> cleared after render ----- */
    calendar_page_note_time_invalid();
    CHECK(cal_page()->time_invalid_latched == true);
    uint8_t fb[300 * 300];
    set_fake("2026-08-26 12:00:00");
    calendar_page_init((page_renderer_t *)cal_page(), 300, 300);
    /* init re-reads the latched NVS flag */
    CHECK(cal_page()->time_invalid_latched == true);
    memset(fb, 0xFF, sizeof(fb));
    calendar_page_render((page_renderer_t *)cal_page(), fb, 300, 300);
    CHECK(cal_page()->time_invalid_latched == false); /* shown once, cleared */
    int32_t inv = 1;
    CHECK(nvs_state_get_i32("cal_time_invalid", &inv) && inv == 0);

    /* ---- 10. placeholder path does not fake a date --------------------- */
    set_fake("1970-01-01 00:00:00");
    nvs_state_set_i32("cal_year", 2000); /* re-pollute: must be rejected */
    nvs_state_set_i32("cal_month", 1);
    calendar_page_init((page_renderer_t *)cal_page(), 300, 300);
    memset(fb, 0xFF, sizeof(fb));
    calendar_page_render((page_renderer_t *)cal_page(), fb, 300, 300);
    CHECK(cal_page()->year == 0); /* render took the placeholder branch */

    calendar_page_time_source = NULL;
    nvs_state_deinit();
    remove("nvs_sim.txt");

    if (s_fail == 0)
        printf("All time-validity tests passed.\n");
    return s_fail ? 1 : 0;
}
