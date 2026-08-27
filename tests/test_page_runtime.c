/**
 * @file test_page_runtime.c
 * @brief Host tests for the page_runtime policy core.
 *
 * Uses the real page registry (host-mode constructors run normally) plus
 * pages that register runtime policies (calendar / weather / coding_plan).
 * Verifies:
 *   - default fallback for pages without a policy
 *   - policy override for pages with one
 *   - effective interests / wake interval / network / periodic refresh
 *   - pending bitmap set/clear + freeze-on-exit semantics
 *   - active-page guard
 */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#include "page_registry.h"
#include "page_runtime.h"

/* Dummy font symbols referenced by page renderers (same trick as
 * test_ui_pages_smoke.c — the real fonts are link-time constants in
 * zetrix_fonts; here any object satisfies the reference). */
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
            s_fail++;                                                                                                  \
        }                                                                                                              \
    } while (0)

int main(void)
{
    page_registry_init();
    page_runtime_init();

    /* --- registry sanity ------------------------------------------------ */
    CHECK(page_registry_count() > 0);

    /* --- default fallback: pages without a policy ------------------------ */
    /* CHAT has no PAGE_REGISTER_WITH_RUNTIME -> default policy applies. */
    const page_runtime_policy_t *chat_pol = page_runtime_policy(UI_PAGE_CHAT);
    CHECK(chat_pol != NULL);
    CHECK(chat_pol->wake_align == PAGE_WAKE_ALIGN_NONE);
    CHECK(chat_pol->data_interests ==
          (PAGE_DATA_WEATHER | PAGE_DATA_CODING_PLAN | PAGE_DATA_HOLIDAY | PAGE_DATA_SNTP));
    CHECK(chat_pol->needs_network_on_wake == true);
    CHECK(page_runtime_effective_interests(UI_PAGE_CHAT) == chat_pol->data_interests);
    CHECK(page_runtime_effective_wake_interval_min(UI_PAGE_CHAT) == 0);
    CHECK(page_runtime_effective_network_on_wake(UI_PAGE_CHAT) == true);
    CHECK(page_runtime_effective_periodic_refresh_s(UI_PAGE_CHAT) == 1800);

    /* Out-of-range page ids also fall back to the default policy. */
    const page_runtime_policy_t *oob = page_runtime_policy((ui_page_id_t)-1);
    CHECK(oob == chat_pol);
    CHECK(page_runtime_policy(UI_PAGE_COUNT) == chat_pol);

    /* --- migrated pages: policy overrides -------------------------------- */
    const page_runtime_policy_t *cal = page_runtime_policy(UI_PAGE_CALENDAR);
    CHECK(cal != chat_pol);
    CHECK(cal->wake_align == PAGE_WAKE_ALIGN_MIDNIGHT);
    CHECK(cal->data_interests == (PAGE_DATA_HOLIDAY | PAGE_DATA_SNTP));
    CHECK(page_runtime_effective_interests(UI_PAGE_CALENDAR) == (PAGE_DATA_HOLIDAY | PAGE_DATA_SNTP));

    const page_runtime_policy_t *weather = page_runtime_policy(UI_PAGE_WEATHER);
    CHECK(weather != chat_pol);
    CHECK(weather->data_interests == (PAGE_DATA_WEATHER | PAGE_DATA_SNTP));
    CHECK(weather->wake_interval_min == 30);
    CHECK(page_runtime_effective_wake_interval_min(UI_PAGE_WEATHER) == 30);

    const page_runtime_policy_t *cp = page_runtime_policy(UI_PAGE_CODING_PLAN);
    CHECK(cp != chat_pol);
    CHECK(cp->data_interests == (PAGE_DATA_CODING_PLAN | PAGE_DATA_SNTP));
    CHECK(cp->periodic_refresh_s == 1800);

    /* --- pending bitmap state machine ------------------------------------ */
    /* init leaves pending empty */
    CHECK(page_runtime_pending_interests() == 0);

    /* entering CHAT sets the full default interest set as pending */
    page_runtime_on_page_entered(UI_PAGE_CHAT);
    CHECK(page_runtime_pending_interests() ==
          (PAGE_DATA_WEATHER | PAGE_DATA_CODING_PLAN | PAGE_DATA_HOLIDAY | PAGE_DATA_SNTP));

    /* entering CALENDAR freezes CHAT's pending bits and sets calendar's */
    page_runtime_on_page_entered(UI_PAGE_CALENDAR);
    CHECK(page_runtime_pending_interests() == (PAGE_DATA_HOLIDAY | PAGE_DATA_SNTP));

    /* clearing a pending bit removes it from the effective view */
    page_runtime_clear_pending(PAGE_DATA_HOLIDAY);
    CHECK(page_runtime_pending_interests() == PAGE_DATA_SNTP);

    /* raw pending survives outside the declared mask until masked out */
    page_runtime_set_pending(PAGE_DATA_WEATHER); /* not declared by calendar */
    CHECK(page_runtime_pending_interests() == PAGE_DATA_SNTP); /* masked out */

    /* re-entering the page re-arms all declared interests */
    page_runtime_on_page_entered(UI_PAGE_CALENDAR);
    CHECK(page_runtime_pending_interests() == (PAGE_DATA_HOLIDAY | PAGE_DATA_SNTP));

    /* --- active page guard ----------------------------------------------- */
    CHECK(page_runtime_is_page_active(UI_PAGE_CALENDAR) == true);
    CHECK(page_runtime_is_page_active(UI_PAGE_CHAT) == false);

    page_runtime_on_page_entered(UI_PAGE_GALLERY);
    CHECK(page_runtime_is_page_active(UI_PAGE_GALLERY) == true);
    CHECK(page_runtime_is_page_active(UI_PAGE_CALENDAR) == false);

    /* out-of-range enter is a no-op (gallery stays active) */
    page_runtime_on_page_entered((ui_page_id_t)-1);
    page_runtime_on_page_entered(UI_PAGE_COUNT);
    CHECK(page_runtime_is_page_active(UI_PAGE_GALLERY) == true);

    /* --- wake interval runtime override ----------------------------------- */
    CHECK(page_runtime_effective_wake_interval_override_min(UI_PAGE_GALLERY) == 0);
    page_runtime_set_wake_interval_override(UI_PAGE_GALLERY, 7);
    CHECK(page_runtime_effective_wake_interval_override_min(UI_PAGE_GALLERY) == 7);
    page_runtime_set_wake_interval_override(UI_PAGE_GALLERY, 0);
    CHECK(page_runtime_effective_wake_interval_override_min(UI_PAGE_GALLERY) == 0);
    /* out-of-range page or minutes is a no-op */
    page_runtime_set_wake_interval_override((ui_page_id_t)-1, 5);
    page_runtime_set_wake_interval_override(UI_PAGE_COUNT, 5);
    page_runtime_set_wake_interval_override(UI_PAGE_CHAT, -1);
    CHECK(page_runtime_effective_wake_interval_override_min(UI_PAGE_CHAT) == 0);

    /* --- MIDNIGHT alignment helper ---------------------------------------- */
    struct tm now, out;
    bool catch_up = false;

    /* Case 1: 10:00 with today unserved -> catch-up. */
    memset(&now, 0, sizeof(now));
    now.tm_year = 126; /* 2026 */
    now.tm_mon = 7;    /* August */
    now.tm_mday = 25;
    now.tm_hour = 10;
    now.tm_min = 0;
    mktime(&now);
    CHECK(page_runtime_midnight_alarm_target(&now, &out, &catch_up) == 0);
    CHECK(catch_up == true);

    /* Case 2: same time with today served -> tomorrow 00:01. */
    page_runtime_mark_day_served(20260825u);
    CHECK(page_runtime_last_served_day() == 20260825u);
    CHECK(page_runtime_midnight_alarm_target(&now, &out, &catch_up) == 0);
    CHECK(catch_up == false);
    CHECK(out.tm_hour == 0 && out.tm_min == 1);
    CHECK(out.tm_mday == 26);
    CHECK(out.tm_mon == 7 && (out.tm_year + 1900) == 2026);

    /* Case 3: 23:59 served -> tomorrow 00:01 (month rollover: Dec 31). */
    memset(&now, 0, sizeof(now));
    now.tm_year = 126;
    now.tm_mon = 11; /* December */
    now.tm_mday = 31;
    now.tm_hour = 23;
    now.tm_min = 59;
    mktime(&now);
    page_runtime_mark_day_served(20261231u);
    CHECK(page_runtime_midnight_alarm_target(&now, &out, &catch_up) == 0);
    CHECK(catch_up == false);
    CHECK(out.tm_hour == 0 && out.tm_min == 1);
    CHECK(out.tm_mday == 1 && out.tm_mon == 0 && (out.tm_year + 1900) == 2027);

    /* Case 4: 00:00:30 (boundary not yet passed) -> today 00:01. */
    memset(&now, 0, sizeof(now));
    now.tm_year = 126;
    now.tm_mon = 7;
    now.tm_mday = 25;
    now.tm_hour = 0;
    now.tm_min = 0;
    now.tm_sec = 30;
    mktime(&now);
    page_runtime_mark_day_served(20260824u); /* yesterday served */
    CHECK(page_runtime_midnight_alarm_target(&now, &out, &catch_up) == 0);
    CHECK(catch_up == false);
    CHECK(out.tm_hour == 0 && out.tm_min == 1);
    CHECK(out.tm_mday == 25); /* today, not tomorrow */

    /* NULL args -> -1. */
    CHECK(page_runtime_midnight_alarm_target(NULL, &out, &catch_up) == -1);
    CHECK(page_runtime_midnight_alarm_target(&now, NULL, &catch_up) == -1);
    CHECK(page_runtime_midnight_alarm_target(&now, &out, NULL) == -1);

    /* --- summary ---------------------------------------------------------- */
    if (s_fail == 0) {
        printf("All page_runtime tests passed.\n");
        return 0;
    }
    printf("%d page_runtime test(s) FAILED.\n", s_fail);
    return 1;
}
