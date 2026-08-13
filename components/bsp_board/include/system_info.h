/**
 * @file system_info.h
 * @brief Hardware/system introspection helpers (C port of SystemInfo)
 *
 * Returns MAC address, chip model, SDK version and heap statistics. On a host
 * build the functions return deterministic mock values so callers can be unit
 * tested without target hardware.
 */

#ifndef SYSTEM_INFO_H
#define SYSTEM_INFO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Format the station MAC address as "aa:bb:cc:dd:ee:ff".
 * @param buf      destination buffer (>= 18 bytes recommended)
 * @param buf_size size of @p buf
 */
void system_info_get_mac_address(char *buf, int buf_size);

/**
 * @brief Chip model / target name (e.g. "esp32s3"). Stable pointer.
 */
const char *system_info_get_chip_model(void);

/**
 * @brief ESP-IDF version string. Stable pointer.
 */
const char *system_info_get_sdk_version(void);

/**
 * @brief Current free heap size in bytes.
 */
uint32_t system_info_get_free_heap(void);

/**
 * @brief Minimum-ever free heap size since boot in bytes.
 */
uint32_t system_info_get_min_free_heap(void);
void     system_info_get_device_id(char *out, size_t len);
void     system_info_get_user_agent(char *out, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* SYSTEM_INFO_H */
