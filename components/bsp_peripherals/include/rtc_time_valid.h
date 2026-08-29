/**
 * @file rtc_time_valid.h
 * @brief Shared time-plausibility windows (docs/rtc-time-validity-plan.md §3.1).
 *
 * Standalone header (no ESP includes) so host tests and every component
 * (bsp_peripherals / main / ui) share ONE definition of "is this time
 * trustworthy". Formerly scattered as bare literals in rawdraw/clock.c
 * (< 2020) and calendar_page.c (2020..2050 inline).
 *
 * The generic trust predicate (time_year_is_plausible, TIME_PLAUSIBLE_*)
 * moved to the neutral leaf data_types/include/time_window.h
 * (docs/arch-hardening-plan.md §3.2.1) and is included here so existing
 * consumers keep working. What stays in this header is bsp/RTC-specific
 * by semantics: the PCF8563 register check and the calendar navigation
 * window. Two windows, on purpose (plan v2, B1-3):
 *   - TIME_PLAUSIBLE_*  — when a time SOURCE may be trusted at all
 *     (window values and rationale live in time_window.h).
 *   - CALENDAR_NAV_*    — which years the calendar page may navigate to
 *     and persist to NVS (pre-existing 2020-2050 window). The upper
 *     bounds are deliberately NOT merged: trusting a clock source and
 *     letting the user page to a year are different decisions.
 */
#ifndef BSP_PERIPHERALS_RTC_TIME_VALID_H_
#define BSP_PERIPHERALS_RTC_TIME_VALID_H_

#include <stdbool.h>
#include <stdint.h>

#include "time_window.h"

#define CALENDAR_NAV_YEAR_MIN 2020
#define CALENDAR_NAV_YEAR_MAX 2050

/** Navigation/persistence window for the calendar page (NVS cal_year etc.). */
static inline bool calendar_nav_year_is_valid(int year)
{
    return year >= CALENDAR_NAV_YEAR_MIN && year <= CALENDAR_NAV_YEAR_MAX;
}

/**
 * Validate raw PCF8563 time registers (REG_SECONDS..REG_YEARS, BCD, exactly
 * as read from 0x02). False means the clock's integrity is compromised:
 *   - VL flag set (bit 7 of seconds): the RTC lost power and its registers
 *     hold the power-on reset value 2000-01-01 00:00.
 *   - Year outside the plausible window (e.g. I2C garbage such as 0x9A).
 */
static inline bool pcf8563_regs_time_plausible(const uint8_t regs[7])
{
    if (regs[0] & 0x80) /* VL */
        return false;
    const int year = ((regs[6] >> 4) * 10 + (regs[6] & 0x0F)) + 2000;
    return time_year_is_plausible(year);
}

#endif /* BSP_PERIPHERALS_RTC_TIME_VALID_H_ */
