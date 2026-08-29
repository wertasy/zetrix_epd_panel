/**
 * @file time_window.h
 * @brief Neutral year-trust window (docs/rtc-time-validity-plan.md §3.1,
 *        relocated to data_types by docs/arch-hardening-plan.md §3.2.1).
 *
 * Pure predicate, no ESP includes, so every layer (rawdraw clock,
 * bsp_peripherals RTC, main/ui) shares ONE definition of "may this time
 * source be trusted at all" without rawdraw reaching into bsp_peripherals.
 * Formerly defined in bsp_peripherals/rtc_time_valid.h; before that,
 * scattered as bare literals in rawdraw/clock.c (< 2020) and
 * calendar_page.c (2020..2050 inline).
 *
 * Upper bound 2099 is the PCF8563 two-digit-year hardware limit. The
 * calendar navigation window (2020-2050) is a separate decision and stays
 * in bsp_peripherals/rtc_time_valid.h.
 */
#ifndef DATA_TYPES_TIME_WINDOW_H_
#define DATA_TYPES_TIME_WINDOW_H_

#include <stdbool.h>

#define TIME_PLAUSIBLE_YEAR_MIN 2020
#define TIME_PLAUSIBLE_YEAR_MAX 2099 /* PCF8563 year register ends at 99 */

/** Trust window for a time source (RTC registers, system continuation time). */
static inline bool time_year_is_plausible(int year)
{
    return year >= TIME_PLAUSIBLE_YEAR_MIN && year <= TIME_PLAUSIBLE_YEAR_MAX;
}

#endif /* DATA_TYPES_TIME_WINDOW_H_ */
