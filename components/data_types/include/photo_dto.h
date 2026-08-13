/**
 * @file photo_dto.h
 * @brief Photo storage DTO types — shared type-only layer.
 *
 * Pure data types used by both the storage module (photo_storage) and the
 * UI page renderers. No functions, no implementation.
 */
#ifndef DATA_TYPES_PHOTO_DTO_H
#define DATA_TYPES_PHOTO_DTO_H

#include <stdint.h>

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

#ifdef __cplusplus
}
#endif

#endif /* DATA_TYPES_PHOTO_DTO_H */
