#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include <stdint.h>
#include <stdbool.h>

#define STORAGE_FILENAME_MAX_LEN 64
#define STORAGE_TYPE_MAX_LEN 16
#define STORAGE_MAX_FILES 256

typedef struct {
    char     name[STORAGE_FILENAME_MAX_LEN]; /* filename e.g. "img123.bin" */
    uint32_t size; /* file size in bytes */
    char     type[STORAGE_TYPE_MAX_LEN]; /* "image", "document", etc. */
} storage_file_info_t;

typedef struct {
    uint32_t            total_bytes;
    uint32_t            used_bytes;
    uint32_t            free_bytes;
    int                 photo_count;
    int                 txt_count;
    int                 file_count;
    storage_file_info_t files[STORAGE_MAX_FILES];
} storage_info_t;

bool           storage_manager_init(void);
void           storage_manager_deinit(void);
void           storage_manager_get_info(storage_info_t *out_info);
bool           storage_manager_delete_file(const char *filename);
int            storage_manager_list_txt_files(char txt_files[][STORAGE_FILENAME_MAX_LEN], int max_files);
int            storage_manager_read_txt_file(const char *filename, char *buffer, int max_len);
bool           storage_manager_write_txt_file(const char *filename, const char *content);
void           storage_manager_format_bytes(uint32_t bytes, char *out_buf, int buf_size);

#endif // STORAGE_MANAGER_H
