/**
 * @file ble_gatt_service.h
 * @brief BLE GATT service for image push to the ePaper display — C port.
 *
 * Custom service UUID 0xF000 with three characteristics:
 *   - 0xF001 Image Data   (Write) — receive raw 1bpp image chunks.
 *   - 0xF002 Image Control(Read/Write NoResp) — start/cancel/complete commands
 *                 and transfer-status polling. Accepts binary control bytes or
 *                 a JSON command object ({"cmd":"begin","size":N}).
 *   - 0xF003 Device Info  (Read)  — packed battery/storage/firmware/display.
 *
 * Image chunks are handed to ble_image_receiver for reassembly; when a
 * "complete" command arrives the registered image-ready callback fires.
 *
 * Target-only (Bluedroid). Non-ESP_PLATFORM callers get no-op stubs.
 */
#ifndef BLE_GATT_SERVICE_H
#define BLE_GATT_SERVICE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* UUIDs and protocol constants                                        */
/* ------------------------------------------------------------------ */

/** Custom GATT service UUID (16-bit for efficiency). */
#define BLE_GATT_SERVICE_UUID 0xF000u
/** Image Data characteristic (Write): raw 1bpp chunks. */
#define BLE_GATT_CHAR_UUID_IMAGE_DATA 0xF001u
/** Image Control characteristic (Read/Write): control + status. */
#define BLE_GATT_CHAR_UUID_IMAGE_CTRL 0xF002u
/** Device Info characteristic (Read): packed status block. */
#define BLE_GATT_CHAR_UUID_DEVICE_INFO 0xF003u

/** Target BLE MTU requested on connect (247 = common phone max). */
#define BLE_GATT_TARGET_MTU 247u

/* Image control commands (written to 0xF002). */
typedef enum {
    BLE_GATT_CMD_START        = 0x01, /**< begin transfer: [cmd, size_hi, size_lo] */
    BLE_GATT_CMD_CANCEL       = 0x02, /**< abort current transfer                */
    BLE_GATT_CMD_COMPLETE     = 0x03, /**< mark done, trigger display            */
    BLE_GATT_CMD_QUERY_STATUS = 0x04, /**< poll status (handled by Read)         */
} ble_gatt_cmd_t;

/* Image transfer status (read from 0xF002, mirrors image receiver). */
typedef enum {
    BLE_GATT_STATUS_IDLE      = 0x00,
    BLE_GATT_STATUS_RECEIVING = 0x01,
    BLE_GATT_STATUS_COMPLETE  = 0x02,
    BLE_GATT_STATUS_ERROR     = 0xFF,
} ble_gatt_status_t;

/**
 * @brief Packed device-info block returned by the Device Info characteristic.
 *
 * Layout is wire-stable (1:1 with the original C++ DeviceInfo struct).
 */
typedef struct __attribute__((packed)) {
    uint8_t battery_percent; /**< 0-100                                */
    uint8_t storage_percent; /**< LittleFS usage 0-100                 */
    uint8_t firmware_major;
    uint8_t firmware_minor;
    uint8_t firmware_patch;
    uint8_t display_width_high; /**< width  >> 8                          */
    uint8_t display_height_high; /**< height >> 8                          */
    uint8_t display_width_low; /**< width  & 0xFF                        */
    uint8_t display_height_low; /**< height & 0xFF                        */
} ble_gatt_device_info_t;

/* ------------------------------------------------------------------ */
/* Callback                                                            */
/* ------------------------------------------------------------------ */

/**
 * @brief Invoked when a complete image has been received over BLE.
 * @param data      Pointer to the 1bpp image buffer (valid until the next
 *                  transfer begins).
 * @param size      Number of bytes received.
 * @param user_data Opaque pointer registered with @ref
 *                  ble_gatt_service_set_image_ready_callback.
 */
typedef void (*ble_gatt_image_ready_cb_t)(const uint8_t *data, uint16_t size, void *user_data);

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

/**
 * @brief Register the GATT server app (call after bluetooth_manager_init()).
 *
 * Asynchronous: the service becomes ready on the ESP_GATTS_START_EVT callback.
 * @return true if the registration request was submitted successfully.
 */
bool ble_gatt_service_init(void);

/** Register a callback fired when a complete image arrives (may be NULL). */
void ble_gatt_service_set_image_ready_callback(ble_gatt_image_ready_cb_t callback, void *user_data);

/**
 * @brief Update the battery and storage figures reported by Device Info.
 *
 * Typically called periodically by the application from the battery monitor /
 * storage manager. Values are clamped to 0-100.
 */
void ble_gatt_service_update_device_info(uint8_t battery_percent, uint8_t storage_percent);

/** @return current transfer status (mirrors the image receiver). */
ble_gatt_status_t ble_gatt_service_get_status(void);

/** @return bytes received for the current transfer. */
uint16_t ble_gatt_service_get_received_bytes(void);

/** @return expected total size for the current transfer. */
uint16_t ble_gatt_service_get_expected_size(void);

/** @return true once the GATT service has been started by the stack. */
bool ble_gatt_service_is_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* BLE_GATT_SERVICE_H */
