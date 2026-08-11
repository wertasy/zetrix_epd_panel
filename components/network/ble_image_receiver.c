/**
 * @file ble_image_receiver.c
 * @brief Image chunk receiver and reassembly for BLE transfer — C port.
 *
 * Allocates a single contiguous image buffer (preferentially in PSRAM),
 * appends chunks pushed over the BLE GATT Image Data characteristic, and
 * exposes the completed 1bpp buffer to the application for rendering or
 * persistence via photo_storage.
 *
 * Concurrency: the module state (buffer pointer, sizes, status) is mutated by
 * the BLE-host task in @ref ble_image_receiver_receive_chunk and read/reset by
 * the application task. A binary mutex (@c s_mutex) serialises every access so
 * the non-atomic "received += len" update and the status machine cannot tear.
 *
 * Target-only: the whole translation unit is compiled out (empty functions
 * returning failure) when @c ESP_PLATFORM is not defined, so the component
 * still links cleanly in host-side tooling that does not exercise it.
 */
#include "ble_image_receiver.h"

#include <string.h>
#include <stdlib.h>

#ifdef ESP_PLATFORM
#    include <esp_log.h>
#    include <esp_heap_caps.h>
#    include <esp_random.h>
#    include <freertos/FreeRTOS.h>
#    include <freertos/semphr.h>

#    include "photo_storage.h"
#    include "style.h"
#endif

static const char *TAG = "ble_image";

/* ------------------------------------------------------------------ */
/* Module state                                                        */
/* ------------------------------------------------------------------ */
static uint8_t           *s_image_buffer  = NULL;
static uint16_t           s_expected_size = 0;
static uint16_t           s_received_size = 0;
static ble_image_status_t s_status        = BLE_IMAGE_STATUS_IDLE;
static bool               s_initialized   = false;

#ifdef ESP_PLATFORM
/* Guards all shared state below. NULL until ble_image_receiver_init(). */
static SemaphoreHandle_t s_mutex = NULL;

/* Lock helpers are no-ops before init / on host builds, so callers never fault
 * on an un-created semaphore and the unit still compiles without FreeRTOS. */
#    define BLE_LOCK()                                                                                                 \
        do {                                                                                                           \
            if (s_mutex != NULL)                                                                                       \
                xSemaphoreTake(s_mutex, portMAX_DELAY);                                                                \
        } while (0)
#    define BLE_UNLOCK()                                                                                               \
        do {                                                                                                           \
            if (s_mutex != NULL)                                                                                       \
                xSemaphoreGive(s_mutex);                                                                               \
        } while (0)
#else
#    define BLE_LOCK() ((void)0)
#    define BLE_UNLOCK() ((void)0)
#endif

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */
bool ble_image_receiver_init(void)
{
#ifdef ESP_PLATFORM
    if (s_initialized) {
        return true;
    }

    /* Create the guard mutex first; everything else depends on it. */
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return false;
    }

    /* Prefer PSRAM for the 15 kB reassembly buffer. */
    s_image_buffer = (uint8_t *)heap_caps_malloc(BLE_IMAGE_MAX_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_image_buffer == NULL) {
        /* Fallback to internal RAM. */
        s_image_buffer = (uint8_t *)malloc(BLE_IMAGE_MAX_SIZE);
        ESP_LOGW(TAG, "Image buffer allocated in internal RAM (PSRAM unavailable)");
    }
    if (s_image_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate image buffer (%u bytes)", (unsigned)BLE_IMAGE_MAX_SIZE);
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        return false;
    }

    ESP_LOGI(TAG, "Image buffer allocated: %u bytes", (unsigned)BLE_IMAGE_MAX_SIZE);
    s_initialized = true;
    return true;
#else
    return false;
#endif
}

/* ------------------------------------------------------------------ */
/* Transfer control                                                    */
/* ------------------------------------------------------------------ */
void ble_image_receiver_start_transfer(uint16_t total_size)
{
#ifdef ESP_PLATFORM
    if (!s_initialized) {
        ESP_LOGE(TAG, "Not initialised");
        s_status = BLE_IMAGE_STATUS_ERROR;
        return;
    }

    BLE_LOCK();
    if (total_size > BLE_IMAGE_MAX_SIZE) {
        ESP_LOGE(TAG, "Image too large: %u > %u", (unsigned)total_size, (unsigned)BLE_IMAGE_MAX_SIZE);
        s_status = BLE_IMAGE_STATUS_ERROR;
        BLE_UNLOCK();
        return;
    }

    s_expected_size = total_size;
    s_received_size = 0;
    /* Clear to white (1bpp 1 = white pixel). */
    memset(s_image_buffer, 0xFF, total_size);
    s_status = BLE_IMAGE_STATUS_RECEIVING;
    BLE_UNLOCK();

    ESP_LOGI(TAG, "Transfer started: expecting %u bytes", (unsigned)total_size);
#endif
}

void ble_image_receiver_receive_chunk(const uint8_t *data, uint16_t len)
{
#ifdef ESP_PLATFORM
    if (!s_initialized) {
        return;
    }

    BLE_LOCK();
    if (s_status != BLE_IMAGE_STATUS_RECEIVING) {
        BLE_UNLOCK();
        ESP_LOGW(TAG, "Not in receiving state, ignoring chunk");
        return;
    }

    if (s_image_buffer == NULL) {
        ESP_LOGE(TAG, "No buffer allocated");
        s_status = BLE_IMAGE_STATUS_ERROR;
        BLE_UNLOCK();
        return;
    }

    if ((uint32_t)s_received_size + (uint32_t)len > (uint32_t)s_expected_size) {
        ESP_LOGE(TAG, "Chunk overflow: %u + %u > %u", (unsigned)s_received_size, (unsigned)len,
                 (unsigned)s_expected_size);
        s_status = BLE_IMAGE_STATUS_ERROR;
        BLE_UNLOCK();
        return;
    }

    memcpy(s_image_buffer + s_received_size, data, len);
    s_received_size += len;

    ESP_LOGD(TAG, "Chunk received: %u bytes, total %u/%u", (unsigned)len, (unsigned)s_received_size,
             (unsigned)s_expected_size);

    if (s_received_size >= s_expected_size) {
        s_status = BLE_IMAGE_STATUS_COMPLETE;
        ESP_LOGI(TAG, "Transfer complete: %u bytes received", (unsigned)s_received_size);
    }
    BLE_UNLOCK();
#endif
}

void ble_image_receiver_reset(void)
{
    BLE_LOCK();
    s_expected_size = 0;
    s_received_size = 0;
    s_status        = BLE_IMAGE_STATUS_IDLE;
    BLE_UNLOCK();
}

/* ------------------------------------------------------------------ */
/* Accessors                                                           */
/* ------------------------------------------------------------------ */
bool ble_image_receiver_is_complete(void)
{
    BLE_LOCK();
    bool complete = (s_status == BLE_IMAGE_STATUS_COMPLETE && s_received_size == s_expected_size);
    BLE_UNLOCK();
    return complete;
}

ble_image_status_t ble_image_receiver_get_status(void)
{
    BLE_LOCK();
    ble_image_status_t st = s_status;
    BLE_UNLOCK();
    return st;
}

uint16_t ble_image_receiver_get_received_bytes(void)
{
    BLE_LOCK();
    uint16_t n = s_received_size;
    BLE_UNLOCK();
    return n;
}

uint16_t ble_image_receiver_get_expected_size(void)
{
    BLE_LOCK();
    uint16_t n = s_expected_size;
    BLE_UNLOCK();
    return n;
}

const uint8_t *ble_image_receiver_get_data(void)
{
    BLE_LOCK();
    const uint8_t *p = s_image_buffer;
    BLE_UNLOCK();
    return p;
}

uint8_t *ble_image_receiver_get_buffer(void)
{
    BLE_LOCK();
    uint8_t *p = s_image_buffer;
    BLE_UNLOCK();
    return p;
}

/* ------------------------------------------------------------------ */
/* Persistence (Touch & Go image handoff)                              */
/* ------------------------------------------------------------------ */
int ble_image_receiver_save_to_storage(void)
{
#ifdef ESP_PLATFORM
    if (!s_initialized) {
        return -1;
    }

    /* Hold the mutex only long enough to validate + snapshot the image, so the
     * slow file I/O below never blocks the BLE receive path or races a reset. */
    uint16_t expected_size = 0;
    uint16_t received_size = 0;
    uint8_t *snapshot      = NULL;

    BLE_LOCK();
    bool complete = (s_status == BLE_IMAGE_STATUS_COMPLETE && s_received_size == s_expected_size);
    expected_size = s_expected_size;
    received_size = s_received_size;
    if (complete && s_image_buffer != NULL && expected_size > 0) {
        snapshot = (uint8_t *)malloc(expected_size);
        if (snapshot != NULL) {
            memcpy(snapshot, s_image_buffer, expected_size);
        }
    }
    BLE_UNLOCK();

    if (!complete || snapshot == NULL) {
        ESP_LOGW(TAG, "Cannot save: transfer not complete (%u/%u bytes)", (unsigned)received_size,
                 (unsigned)expected_size);
        free(snapshot);
        return -1;
    }

    /* Generate a unique-ish ID from the boot-time random source. */
    photo_info_t info;
    memset(&info, 0, sizeof(info));
    uint32_t r = esp_random();
    snprintf(info.id, sizeof(info.id), "ble%08lx", (unsigned long)r);
    snprintf(info.title, sizeof(info.title), "BLE Push");
    info.width     = STYLE_SCREEN_WIDTH;
    info.height    = (uint16_t)(expected_size / (STYLE_SCREEN_WIDTH / 8u));
    info.file_size = expected_size;
    info.timestamp = 0; /* let photo_storage stamp it if it chooses */

    int rc = photo_save(&info, snapshot);
    free(snapshot);

    if (rc != 0) {
        ESP_LOGE(TAG, "photo_save failed for BLE image");
        return -1;
    }

    ESP_LOGI(TAG, "BLE image saved as '%s' (%ux%u, %u bytes)", info.id, (unsigned)info.width, (unsigned)info.height,
             (unsigned)expected_size);
    return 0;
#else
    return -1;
#endif
}
