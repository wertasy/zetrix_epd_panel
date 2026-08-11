/**
 * @file ble_image_receiver.h
 * @brief Image chunk receiver and reassembly for BLE transfer — C port.
 *
 * Accumulates image data pushed over the BLE GATT Image Data characteristic
 * (0xF001) into a single contiguous buffer, then hands the completed image to
 * the application via the GATT image-ready callback or @ref
 * ble_image_receiver_save_to_storage.
 *
 * 400x300 1bpp image = 15000 bytes; with a ~247-byte MTU that is ~61 chunks.
 * The buffer is allocated in PSRAM when available, falling back to internal RAM.
 *
 * Target-only: every function is a no-op / returns failure unless built with
 * ESP_PLATFORM (ESP-IDF Bluedroid target).
 */
#ifndef BLE_IMAGE_RECEIVER_H
#define BLE_IMAGE_RECEIVER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum supported image size: 400 * 300 / 8 = 15000 bytes (1bpp). */
#define BLE_IMAGE_MAX_SIZE 15000u

/** Transfer status (mirrors ble_gatt_service transfer status). */
typedef enum {
    BLE_IMAGE_STATUS_IDLE      = 0x00, /**< no transfer in progress            */
    BLE_IMAGE_STATUS_RECEIVING = 0x01, /**< chunks arriving                    */
    BLE_IMAGE_STATUS_COMPLETE  = 0x02, /**< all expected bytes received        */
    BLE_IMAGE_STATUS_ERROR     = 0xFF, /**< overflow / not initialised         */
} ble_image_status_t;

/**
 * @brief Initialise the receiver (allocate the reassembly buffer).
 *
 * Safe to call more than once; subsequent calls are no-ops.
 * @return true on success (or if already initialised).
 */
bool ble_image_receiver_init(void);

/**
 * @brief Begin a new image transfer.
 * @param total_size Expected total size in bytes (<= @ref BLE_IMAGE_MAX_SIZE).
 *
 * Clears the buffer to white (0xFF, 1bpp) and switches to RECEIVING.
 */
void ble_image_receiver_start_transfer(uint16_t total_size);

/**
 * @brief Append a chunk of image data to the buffer.
 *
 * Ignored unless in RECEIVING state. Triggers COMPLETE once @p len pushes the
 * received total to the expected size. Overflow transitions to ERROR.
 */
void ble_image_receiver_receive_chunk(const uint8_t *data, uint16_t len);

/** Cancel any in-progress transfer and return to IDLE. */
void ble_image_receiver_reset(void);

/** @return true once all expected bytes have been received. */
bool ble_image_receiver_is_complete(void);

/** @return current transfer status. */
ble_image_status_t ble_image_receiver_get_status(void);

/** @return bytes received for the current transfer. */
uint16_t ble_image_receiver_get_received_bytes(void);

/** @return expected total size for the current transfer. */
uint16_t ble_image_receiver_get_expected_size(void);

/** @return read-only pointer to the received image data (1bpp), or NULL. */
const uint8_t *ble_image_receiver_get_data(void);

/** @return writable pointer to the internal buffer (for direct rendering). */
uint8_t *ble_image_receiver_get_buffer(void);

/**
 * @brief Persist the currently-buffered image to photo_storage.
 *
 * Used by the BLE image-ready handler to save a phone-pushed photo so that the
 * Touch & Go transfer survives a reboot. Generates a timestamped ID.
 * @return 0 on success, -1 on failure (no complete image / save error).
 */
int ble_image_receiver_save_to_storage(void);

#ifdef __cplusplus
}
#endif

#endif /* BLE_IMAGE_RECEIVER_H */
