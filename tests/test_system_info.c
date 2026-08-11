/**
 * @file test_system_info.c
 * @brief Host unit tests for system_info (valid return values, no crash)
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "system_info.h"

static void test_mac_address(void)
{
    printf("Running test_mac_address...\n");
    char mac[32] = {0};
    system_info_get_mac_address(mac, sizeof(mac));
    /* Must be non-empty and NUL-terminated. */
    assert(mac[0] != '\0');
    assert(strlen(mac) < sizeof(mac));
    /* Should contain at least 5 colons for a MAC. */
    int colons = 0;
    for (int i = 0; mac[i]; i++)
        if (mac[i] == ':')
            colons++;
    assert(colons == 5);
    printf("  test_mac_address passed!\n");
}

static void test_chip_model(void)
{
    printf("Running test_chip_model...\n");
    const char *model = system_info_get_chip_model();
    assert(model != NULL);
    assert(model[0] != '\0');
    printf("  test_chip_model passed!\n");
}

static void test_sdk_version(void)
{
    printf("Running test_sdk_version...\n");
    const char *ver = system_info_get_sdk_version();
    assert(ver != NULL);
    assert(ver[0] != '\0');
    printf("  test_sdk_version passed!\n");
}

static void test_heap(void)
{
    printf("Running test_heap...\n");
    uint32_t free     = system_info_get_free_heap();
    uint32_t min_free = system_info_get_min_free_heap();
    assert(free > 0);
    /* min_free must not exceed current free. */
    assert(min_free <= free);
    printf("  test_heap passed!\n");
}

static void test_truncated_buffer(void)
{
    printf("Running test_truncated_buffer...\n");
    /* A too-small buffer must not overflow and must be NUL-terminated. */
    char tiny[4] = {0};
    system_info_get_mac_address(tiny, sizeof(tiny));
    assert(tiny[sizeof(tiny) - 1] == '\0');
    printf("  test_truncated_buffer passed!\n");
}

int main(void)
{
    printf("Starting SystemInfo tests...\n");
    test_mac_address();
    test_chip_model();
    test_sdk_version();
    test_heap();
    test_truncated_buffer();
    printf("All SystemInfo tests passed successfully!\n");
    return 0;
}
