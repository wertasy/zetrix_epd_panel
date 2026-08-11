/**
 * @file bluetooth_manager.h
 * @brief BLE initialization and advertising control for ESP32-S3 — C port.
 *
 * Owns the BLE controller + Bluedroid lifecycle, the GAP advertising
 * configuration, and the GATT service / image-receiver bring-up for the phone
 * image-push feature. Also implements the NFC "Touch & Go" handoff: on boot
 * the local BLE MAC address + service UUID are encoded as an NDEF Text record
 * and written to the GT23SC6699 NFC tag so that a phone tap can auto-connect
 * and stream a background image over GATT.
 *
 * Target-only (Bluedroid). Non-ESP_PLATFORM callers get no-op stubs.
 */
#ifndef BLUETOOTH_MANAGER_H
#define BLUETOOTH_MANAGER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* BLE lifecycle                                                       */
/* ------------------------------------------------------------------ */

/**
 * @brief Initialise the BLE subsystem (controller + Bluedroid + GAP callback).
 *
 * Also initialises the image receiver and registers the GATT service. Called
 * once at boot, after nvs_flash_init().
 * @return true if BLE initialised successfully.
 */
bool bluetooth_manager_init(void);

/**
 * @brief Enable BLE: set the device name and start advertising.
 *
 * Configures raw advertising data (Flags + 0xF000 service UUID + Complete Local
 * Name "InkScreen") and begins connectable advertising.
 * @return true if advertising was configured successfully.
 */
bool bluetooth_manager_enable(void);

/**
 * @brief Restart advertising after a GATT client disconnects.
 *
 * No-op (returns true) when BLE is disabled or not yet initialised.
 * @return true if the restart was requested or BLE is disabled.
 */
bool bluetooth_manager_restart_advertising(void);

/**
 * @brief Disable BLE: stop advertising (controller stays initialised).
 * @return true if BLE was disabled (or already disabled).
 */
bool bluetooth_manager_disable(void);

/** @return true if BLE is currently enabled and advertising. */
bool bluetooth_manager_is_enabled(void);

/* ------------------------------------------------------------------ */
/**
 * @brief Raw NFC tag writer callback type.
 *
 * Implemented by the NFC driver wrapper; the bluetooth_manager calls it with a
 * ready-to-write NDEF Text-record TLV blob (tag byte 0x03 ... terminator 0xFE)
 * so the bluetooth module stays decoupled from the GT23SC6699 transport (which
 * lives in main/) while owning the NDEF encoding for the Touch & Go payload.
 *
 * @param data      NDEF TLV bytes to write at offset 0 of the tag user area.
 * @param len       Number of bytes.
 * @param user_data Opaque pointer registered alongside the callback.
 * @return 0 on success, non-zero on failure.
 */
typedef int (*bluetooth_nfc_ndef_writer_t)(const uint8_t *data, size_t len, void *user_data);

/**
 * @brief Register the NFC NDEF writer used by Touch & Go.
 *
 * Must be called before @ref bluetooth_manager_publish_touch_and_go. Pass NULL
 * to disable the handoff.
 */
void bluetooth_manager_set_nfc_writer(bluetooth_nfc_ndef_writer_t writer, void *user_data);

/**
 * @brief Encode the local BLE MAC + service UUID as an NDEF Text record and
 *        publish it to the NFC tag via the registered writer.
 *
 * Enables "Touch & Go": a phone tapping the tag reads the MAC, auto-connects
 * to the GATT service, and pushes a background image. Safe to call before BLE
 * is fully up (the MAC is read directly from the controller).
 *
 * @return 0 on success, -1 if no writer is registered, -2 if the MAC could not
 *         be read, or the writer's non-zero return code.
 */
int bluetooth_manager_publish_touch_and_go(void);

#ifdef __cplusplus
}
#endif

#endif /* BLUETOOTH_MANAGER_H */
