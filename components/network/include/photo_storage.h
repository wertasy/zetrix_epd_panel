/**
 * @file photo_storage.h
 * @brief LittleFS-backed photo storage for 1bpp images — C port.
 *
 * Stores 1bpp image data as .bin files and metadata as .meta JSON files in the
 * LittleFS "assets" partition (mounted via the @ref storage_manager component).
 * A binary index file photos.idx caches the photo_info_t records for fast load.
 *
 * Image format: raw 1bpp data, no header. Width is always 400 for the panel.
 * Bytes per row = 400/8 = 50. Total size = 50 * height.
 */
#ifndef PHOTO_STORAGE_H
#define PHOTO_STORAGE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PHOTO_MAX_PHOTOS 50
#define PHOTO_MAX_PATH 64
#define PHOTO_TITLE_LEN 64
#define PHOTO_DATE_LEN 24
#define PHOTO_LOCATION_LEN 64
#define PHOTO_BODY_LEN 256

/** Photo metadata. */
typedef struct {
    char     id[16]; /**< unique ID (from server)    */
    char     title[PHOTO_TITLE_LEN]; /**< short headline             */
    char     date[PHOTO_DATE_LEN]; /**< ISO / display date         */
    char     location[PHOTO_LOCATION_LEN]; /**< photo location             */
    char     body[PHOTO_BODY_LEN]; /**< narrative body text        */
    uint16_t width; /**< image width in pixels      */
    uint16_t height; /**< image height in pixels     */
    uint32_t file_size; /**< 1bpp data size in bytes    */
    uint32_t timestamp; /**< upload epoch seconds       */
    char     path[PHOTO_MAX_PATH]; /**< LittleFS path to .bin file */
} photo_info_t;

/** Initialise photo storage (mounts LittleFS via storage_manager, loads index). */
int photo_storage_init(void);

/**
 * @brief Save a photo (writes .bin + .meta + updates index).
 * @return 0 on success, -1 on failure.
 */
int photo_save(const photo_info_t *info, const uint8_t *data_1bpp);

/** Load a photo's 1bpp data; returns bytes read or -1. */
int photo_load(const char *id, uint8_t *out_buffer, uint32_t max_size);

/** Copy up to @p max_count photos into @p out_list; returns count copied. */
int photo_list(photo_info_t *out_list, int max_count);

/** Delete a photo by ID. @return 0 on success, -1 if not found. */
int photo_delete(const char *id);

int  photo_get_count(void);
int  photo_get_by_index(int index, photo_info_t *out);
bool photo_exists(const char *id);

/** Update editable metadata fields (title/date/location/body) by ID. */
int photo_update_info(const char *id, const photo_info_t *updates);

/** Move a photo in display order by delta (-1 up, +1 down). */
int photo_move(const char *id, int delta);

/**
 * @brief Re-read the index file from disk (picks up out-of-band changes).
 *
 * Also used by host unit tests to verify the save/load round-trip.
 * @return 0 on success, -1 on failure.
 */
int photo_storage_reload_index(void);

#ifdef __cplusplus
}
#endif

#endif /* PHOTO_STORAGE_H */
