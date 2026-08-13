/**
 * @file holiday_query.h
 * @brief Pure-data types and query helpers for the holiday schedule.
 *
 * Network/fetch lifecycle lives in holiday_fetcher.h; this header is the
 * dependency-free query surface so widgets can consume holiday data without
 * pulling in any network component.
 */
#ifndef HOLIDAY_QUERY_H_
#define HOLIDAY_QUERY_H_
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

#define HOLIDAY_MAX_ENTRIES 50
#define HOLIDAY_NAME_LEN 16

typedef struct {
    int16_t year;
    int8_t  month;
    int8_t  day;
    char    name[HOLIDAY_NAME_LEN];
    bool    is_rest;
} holiday_entry_t;

typedef struct {
    int             year;
    int             entry_count;
    holiday_entry_t entries[HOLIDAY_MAX_ENTRIES];
} holiday_cache_t;

bool        holiday_fetcher_is_holiday(int year, int month, int day);
bool        holiday_fetcher_is_makeup_workday(int year, int month, int day);
const char *holiday_fetcher_get_holiday_name(int year, int month, int day);
const char *holiday_fetcher_get_makeup_label(int year, int month, int day);
const holiday_cache_t *holiday_fetcher_get_cache(void);

#ifdef __cplusplus
}
#endif
#endif /* HOLIDAY_QUERY_H_ */
