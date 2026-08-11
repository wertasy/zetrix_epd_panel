/**
 * @file photo_downloader.h
 * @brief HTTP photo downloader from server to LittleFS — C port.
 *
 * Syncs the photo list from a server and downloads new photos as raw 1bpp
 * image data. Uses esp_http_client on the target; all functions are no-ops on
 * the host.
 *
 * Server API:
 *   GET  /api/photos                 -> JSON list of photos
 *   GET  /api/photos/{id}.bin        -> raw 1bpp image data
 *   POST /api/photos/{id}/downloaded -> confirm download
 */
#ifndef PHOTO_DOWNLOADER_H
#define PHOTO_DOWNLOADER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PHOTO_DOWNLOADER_URL_MAX 128

/** Photo downloader configuration. */
typedef struct {
    char server_url[PHOTO_DOWNLOADER_URL_MAX]; /**< e.g. "http://192.168.1.100:8080" */
} photo_downloader_config_t;

/**
 * @brief Initialise the photo downloader.
 * @return 0 on success, -1 on failure.
 */
int photo_downloader_init(const photo_downloader_config_t *cfg);

/**
 * @brief Sync photos from the server.
 *
 * Fetches the server photo list, compares it with the local index and
 * downloads only new photos.
 * @return number of new photos downloaded, or -1 on error.
 */
int photo_sync(void);

/**
 * @brief Download a single photo by ID.
 * @return 0 on success, -1 on failure.
 */
int photo_download_single(const char *photo_id);

bool photo_downloader_is_ready(void);
bool photo_downloader_is_syncing(void);

#ifdef __cplusplus
}
#endif

#endif /* PHOTO_DOWNLOADER_H */
