/**
 * @file test_nvs_state.c
 * @brief Host unit tests for the NVS write-through cache
 *
 * Verifies: load/save cycle, string & i32 get/set, cache consistency,
 * persistence across deinit/reinit, and passthrough of ad-hoc keys.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "nvs_state.h"

static void clean_sim(void)
{
    nvs_state_deinit();
    remove("./nvs_sim.txt");
}

static void test_load_defaults(void)
{
    printf("Running test_load_defaults...\n");
    clean_sim();
    assert(nvs_state_init());

    system_settings_t s;
    assert(nvs_state_load(&s));
    /* Defaults must match the documented behavior. */
    assert(s.weather.auto_update == true);
    assert(s.weather.update_interval == 1);
    assert(s.weather.city[0] == '\0');
    assert(s.calendar.show_lunar == true);
    assert(s.calendar.selected_year == 2026);
    assert(s.calendar.selected_month == 1);
    assert(s.calendar.selected_day == 0);
    assert(s.ui.lifebar_visible == 1);
    assert(s.ble.enabled == false);
    assert(s.ap_transfer_boot == false);
    printf("  test_load_defaults passed!\n");
}

static void test_save_load_roundtrip(void)
{
    printf("Running test_save_load_roundtrip...\n");
    clean_sim();
    assert(nvs_state_init());

    system_settings_t s;
    nvs_state_load(&s);
    strncpy(s.weather.city, "西安", sizeof(s.weather.city) - 1);
    s.weather.auto_update        = false;
    s.weather.update_interval    = 6;
    s.calendar.show_lunar        = false;
    s.calendar.selected_year     = 2030;
    s.calendar.selected_month    = 12;
    s.calendar.selected_day      = 25;
    s.epd.partial_count          = 42;
    s.epd.lifetime_refreshes     = 9001;
    s.epd.last_refresh_timestamp = 1700000000;
    s.ui.last_page               = 3;
    s.ui.summary_scroll          = 480;
    s.ui.lifebar_visible         = 0;
    s.ble.enabled                = true;
    s.ap_transfer_boot           = true;
    assert(nvs_state_save(&s));

    /* Reload into a fresh cache — proves persistence beyond the live cache. */
    nvs_state_deinit();
    assert(nvs_state_init());
    system_settings_t r;
    assert(nvs_state_load(&r));

    assert(strcmp(r.weather.city, "西安") == 0);
    assert(r.weather.auto_update == false);
    assert(r.weather.update_interval == 6);
    assert(r.calendar.show_lunar == false);
    assert(r.calendar.selected_year == 2030);
    assert(r.calendar.selected_month == 12);
    assert(r.calendar.selected_day == 25);
    assert(r.epd.partial_count == 42);
    assert(r.epd.lifetime_refreshes == 9001);
    assert(r.epd.last_refresh_timestamp == 1700000000);
    assert(r.ui.last_page == 3);
    assert(r.ui.summary_scroll == 480);
    assert(r.ui.lifebar_visible == 0);
    assert(r.ble.enabled == true);
    assert(r.ap_transfer_boot == true);
    printf("  test_save_load_roundtrip passed!\n");
}
static void test_string_get_set_known(void)
{
    printf("Running test_string_get_set_known...\n");
    clean_sim();
    assert(nvs_state_init());

    char out[NVS_CITY_MAX_LEN];
    assert(nvs_state_get_string("weather_city", out, sizeof(out)) == false);

    /* Set a known string key via the typed accessor; cache + backing update. */
    assert(nvs_state_set_string("weather_city", "Shanghai"));
    assert(nvs_state_get_string("weather_city", out, sizeof(out)));
    assert(strcmp(out, "Shanghai") == 0);

    /* Cache consistency: struct field reflects the write. */
    system_settings_t s;
    nvs_state_load(&s);
    assert(strcmp(s.weather.city, "Shanghai") == 0);
    printf("  test_string_get_set_known passed!\n");
}

static void test_string_get_set_passthrough(void)
{
    printf("Running test_string_get_set_passthrough...\n");
    clean_sim();
    assert(nvs_state_init());

    char out[NVS_STR_VALUE_MAX_LEN];
    /* Unknown key goes straight to the backing store. */
    assert(nvs_state_set_string("wifi_ssid", "MyHomeWiFi"));
    assert(nvs_state_get_string("wifi_ssid", out, sizeof(out)));
    assert(strcmp(out, "MyHomeWiFi") == 0);

    /* Overwrite. */
    assert(nvs_state_set_string("wifi_ssid", "Office_5G"));
    assert(nvs_state_get_string("wifi_ssid", out, sizeof(out)));
    assert(strcmp(out, "Office_5G") == 0);
    printf("  test_string_get_set_passthrough passed!\n");
}

static void test_i32_get_set(void)
{
    printf("Running test_i32_get_set...\n");
    clean_sim();
    assert(nvs_state_init());

    /* Known i32 key (bool stored as 0/1). */
    int32_t v = 0;
    assert(nvs_state_set_i32("weather_auto", 0));
    assert(nvs_state_get_i32("weather_auto", &v));
    assert(v == 0);
    assert(nvs_state_set_i32("weather_auto", 1));
    assert(nvs_state_get_i32("weather_auto", &v));
    assert(v == 1);

    /* Known i32 key (regular int). */
    assert(nvs_state_set_i32("epd_partial", 99));
    assert(nvs_state_get_i32("epd_partial", &v));
    assert(v == 99);

    /* Negative value. */
    assert(nvs_state_set_i32("ui_scroll", -1234));
    assert(nvs_state_get_i32("ui_scroll", &v));
    assert(v == -1234);

    /* Unknown i32 key passthrough. */
    assert(nvs_state_set_i32("custom_count", 777));
    assert(nvs_state_get_i32("custom_count", &v));
    assert(v == 777);
    printf("  test_i32_get_set passed!\n");
}

static void test_cache_consistency_after_set(void)
{
    printf("Running test_cache_consistency_after_set...\n");
    clean_sim();
    assert(nvs_state_init());

    /* A set must update the live cache immediately (no reload needed). */
    assert(nvs_state_set_i32("cal_year", 1999));
    int32_t v = 0;
    assert(nvs_state_get_i32("cal_year", &v));
    assert(v == 1999);

    /* And it must survive a deinit/reinit (write-through persisted it). */
    nvs_state_deinit();
    assert(nvs_state_init());
    assert(nvs_state_get_i32("cal_year", &v));
    assert(v == 1999);
    printf("  test_cache_consistency_after_set passed!\n");
}

static void test_null_safety(void)
{
    printf("Running test_null_safety...\n");
    clean_sim();
    assert(nvs_state_init());
    char buf[8];
    assert(nvs_state_get_string(NULL, buf, sizeof(buf)) == false);
    assert(nvs_state_get_string("weather_city", NULL, sizeof(buf)) == false);
    assert(nvs_state_set_string(NULL, "x") == false);
    assert(nvs_state_set_string("weather_city", NULL) == false);
    assert(nvs_state_get_i32("weather_auto", NULL) == false);
    assert(nvs_state_set_i32(NULL, 1) == false);
    assert(nvs_state_load(NULL) == false);
    assert(nvs_state_save(NULL) == false);
    printf("  test_null_safety passed!\n");
}


static void test_string_truncation_consistency(void)
{
    printf("Running test_string_truncation_consistency...\n");
    clean_sim();
    assert(nvs_state_init());

    // Generate a string longer than NVS_CITY_MAX_LEN (64)
    char long_city[100];
    memset(long_city, 'A', sizeof(long_city) - 1);
    long_city[sizeof(long_city) - 1] = '\0';

    assert(nvs_state_set_string("weather_city", long_city));

    char out[NVS_CITY_MAX_LEN];
    assert(nvs_state_get_string("weather_city", out, sizeof(out)));
    assert(strlen(out) == NVS_CITY_MAX_LEN - 1);

    // Deinit and reinit to reload from backing to verify it matches
    nvs_state_deinit();
    assert(nvs_state_init());

    char out2[NVS_CITY_MAX_LEN];
    assert(nvs_state_get_string("weather_city", out2, sizeof(out2)));
    assert(strcmp(out, out2) == 0);
    printf("  test_string_truncation_consistency passed!\n");
}
int main(void)
{
    printf("Starting NVS State tests...\n");
    test_load_defaults();
    test_save_load_roundtrip();
    test_string_get_set_known();
    test_string_get_set_passthrough();
    test_i32_get_set();
    test_cache_consistency_after_set();
    test_null_safety();
    clean_sim();
    test_string_truncation_consistency();
    printf("All NVS State tests passed successfully!\n");
    return 0;
}
