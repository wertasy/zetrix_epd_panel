/**
 * @file system_info.c
 * @brief Hardware/system introspection implementation
 *
 * Target build uses ESP-IDF APIs (esp_mac, esp_chip_info, esp_idf_version,
 * esp_heap_caps). Host build returns deterministic mock values so the module
 * links and unit tests under plain gcc/clang.
 */

#include "system_info.h"

#include <string.h>
#include <stdio.h>

#ifdef ESP_PLATFORM
#    include "esp_mac.h"
#    include "esp_chip_info.h"
#    include "esp_idf_version.h"
#    include "esp_system.h" /* esp_get_free_heap_size / esp_get_minimum_free_heap_size (IDF v6) */
#    include "esp_heap_caps.h"
#    include "esp_log.h"
#    include "esp_app_desc.h"

#endif

void system_info_get_mac_address(char *buf, int buf_size)
{
    if (!buf || buf_size <= 0)
        return;

#ifdef ESP_PLATFORM
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(buf, (size_t)buf_size, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
#else
    /* Deterministic mock MAC for host tests. */
    static const char mock_mac[] = "00:11:22:33:44:55";
    strncpy(buf, mock_mac, (size_t)buf_size - 1);
    buf[buf_size - 1] = '\0';
#endif
}

const char *system_info_get_chip_model(void)
{
#ifdef ESP_PLATFORM
    /* CONFIG_IDF_TARGET is the canonical target name (e.g. "esp32s3"). */
    return CONFIG_IDF_TARGET;
#else
    return "host";
#endif
}

const char *system_info_get_sdk_version(void)
{
#ifdef ESP_PLATFORM
    return esp_get_idf_version();
#else
    return "host-sim";
#endif
}

uint32_t system_info_get_free_heap(void)
{
#ifdef ESP_PLATFORM
    return (uint32_t)esp_get_free_heap_size();
#else
    /* Stable mock value for host tests. */
    return 200000u;
#endif
}

uint32_t system_info_get_min_free_heap(void)
{
#ifdef ESP_PLATFORM
    return (uint32_t)esp_get_minimum_free_heap_size();
#else
    /* Stable mock value for host tests (<= free heap). */
    return 100000u;
#endif
}

void system_info_get_device_id(char *out, size_t len)
{
    if (!out || len == 0)
        return;
#ifdef ESP_PLATFORM
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out, len, "inkscreen_%02x%02x", mac[4], mac[5]);
#else
    snprintf(out, len, "inkscreen_mock");
#endif
}

void system_info_get_user_agent(char *out, size_t len)
{
    if (!out || len == 0)
        return;
#ifdef ESP_PLATFORM
    const esp_app_desc_t *app_desc = esp_app_get_description();
    snprintf(out, len, "zectrix-s3-epaper-4.2/%s", app_desc->version);
#else
    snprintf(out, len, "zectrix-s3-epaper-4.2/host");
#endif
}
