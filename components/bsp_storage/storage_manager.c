#include "storage_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef ESP_PLATFORM
#    include <esp_littlefs.h>
#    include <esp_log.h>
#    include <esp_err.h>

static const char *TAG = "StorageMgr";
#    define STORAGE_BASE_PATH "/spiffs"
#else
#    include <unistd.h>
#    include <errno.h>
#    define STORAGE_BASE_PATH "./spiffs"
#endif

static int compare_strings(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

static bool filename_is_safe(const char *filename)
{
    if (!filename || filename[0] == '\0')
        return false;
    if (strstr(filename, ".."))
        return false;
    if (strstr(filename, "/"))
        return false;
    if (strstr(filename, "\\"))
        return false;
    return true;
}

bool storage_manager_init(void)
{
#ifdef ESP_PLATFORM
    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "assets",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };
    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret == ESP_ERR_INVALID_STATE) {
        /* Already mounted — treat as success */
        return true;
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount LittleFS: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "LittleFS mounted successfully");
    return true;
#else
    struct stat st = {0};
    if (stat(STORAGE_BASE_PATH, &st) == -1) {
        if (mkdir(STORAGE_BASE_PATH, 0777) != 0) {
            if (errno != EEXIST) {
                return false;
            }
        }
    }
    return true;
#endif
}

void storage_manager_deinit(void)
{
#ifdef ESP_PLATFORM
    esp_vfs_littlefs_unregister("assets");
#endif
}

void storage_manager_get_info(storage_info_t *out_info)
{
    if (!out_info) {
        return;
    }
    memset(out_info, 0, sizeof(*out_info));

#ifdef ESP_PLATFORM
    size_t total = 0, used = 0;
    esp_err_t ret = esp_littlefs_info("assets", &total, &used);
    if (ret == ESP_OK) {
        out_info->total_bytes = (uint32_t)total;
        out_info->used_bytes = (uint32_t)used;
        out_info->free_bytes = out_info->total_bytes - out_info->used_bytes;
    } else {
        ESP_LOGE(TAG, "esp_littlefs_info failed");
    }
#else
    out_info->total_bytes = 8 * 1024 * 1024; // 8MB mock
#endif

    DIR *dir = opendir(STORAGE_BASE_PATH);
    if (!dir) {
#ifdef ESP_PLATFORM
        ESP_LOGW(TAG, "Cannot open %s directory", STORAGE_BASE_PATH);
#else
        printf("Cannot open %s directory\n", STORAGE_BASE_PATH);
#endif
        return;
    }

    struct dirent *entry;
    uint32_t calculated_used_bytes = 0;
    while ((entry = readdir(dir)) != NULL) {
        const char *name = entry->d_name;
        if (!name || name[0] == '\0')
            continue;

        // Skip index/meta files, "." and ".."
        if (strstr(name, ".idx") || strstr(name, ".meta") || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            continue;
        }

        char path[280];
        snprintf(path, sizeof(path), "%s/%s", STORAGE_BASE_PATH, name);

        struct stat st;
        if (stat(path, &st) != 0)
            continue;

        // Classify by extension (case-insensitive)
        const char *ext = strrchr(name, '.');
        bool is_photo = false;
        bool is_txt = false;
        if (ext) {
            char lower_ext[8];
            int ei;
            for (ei = 0; ei < 7 && ext[ei]; ei++) {
                lower_ext[ei] = (char)tolower((unsigned char)ext[ei]);
            }
            lower_ext[ei] = '\0';
            if (strcmp(lower_ext, ".bin") == 0 || strcmp(lower_ext, ".pbm") == 0 || strcmp(lower_ext, ".jpg") == 0 ||
                strcmp(lower_ext, ".png") == 0) {
                is_photo = true;
            } else if (strcmp(lower_ext, ".txt") == 0) {
                is_txt = true;
            }
        }

        if (is_photo)
            out_info->photo_count++;
        if (is_txt)
            out_info->txt_count++;

        // Add file info if there's space
        if (out_info->file_count < STORAGE_MAX_FILES) {
            storage_file_info_t *f_info = &out_info->files[out_info->file_count];
            strncpy(f_info->name, name, STORAGE_FILENAME_MAX_LEN - 1);
            f_info->name[STORAGE_FILENAME_MAX_LEN - 1] = '\0';
            f_info->size = (uint32_t)st.st_size;
            strncpy(f_info->type, is_photo ? "image" : (is_txt ? "document" : "other"), STORAGE_TYPE_MAX_LEN - 1);
            f_info->type[STORAGE_TYPE_MAX_LEN - 1] = '\0';
            out_info->file_count++;
        }

        calculated_used_bytes += (uint32_t)st.st_size;
    }
    closedir(dir);

#ifndef ESP_PLATFORM
    out_info->used_bytes = calculated_used_bytes;
    out_info->free_bytes =
        out_info->total_bytes > out_info->used_bytes ? out_info->total_bytes - out_info->used_bytes : 0;
#endif
}

bool storage_manager_delete_file(const char *filename)
{
    if (!filename_is_safe(filename))
        return false;
    char path[280];
    snprintf(path, sizeof(path), "%s/%s", STORAGE_BASE_PATH, filename);

    // Also delete associated .meta file if it exists
    char meta_path[280];
    snprintf(meta_path, sizeof(meta_path), "%s/%s.meta", STORAGE_BASE_PATH, filename);
    remove(meta_path);

    int ret = remove(path);
    if (ret != 0) {
#ifdef ESP_PLATFORM
        ESP_LOGW(TAG, "Failed to delete %s", path);
#else
        printf("Failed to delete %s\n", path);
#endif
        return false;
    }
#ifdef ESP_PLATFORM
    ESP_LOGI(TAG, "Deleted %s", path);
#else
    printf("Deleted %s\n", path);
#endif
    return true;
}

int storage_manager_list_txt_files(char txt_files[][STORAGE_FILENAME_MAX_LEN], int max_files)
{
    if (max_files <= 0)
        return 0;
    DIR *dir = opendir(STORAGE_BASE_PATH);
    if (!dir)
        return 0;

    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        const char *name = entry->d_name;
        if (!name)
            continue;
        const char *ext = strrchr(name, '.');
        if (ext) {
            char lower_ext[8];
            int ei;
            for (ei = 0; ei < 7 && ext[ei]; ei++) {
                lower_ext[ei] = (char)tolower((unsigned char)ext[ei]);
            }
            lower_ext[ei] = '\0';
            if (strcmp(lower_ext, ".txt") == 0) {
                if (count < max_files) {
                    strncpy(txt_files[count], name, STORAGE_FILENAME_MAX_LEN - 1);
                    txt_files[count][STORAGE_FILENAME_MAX_LEN - 1] = '\0';
                    count++;
                } else {
                    break;
                }
            }
        }
    }

    if (count > 1) {
        qsort(txt_files, count, STORAGE_FILENAME_MAX_LEN, compare_strings);
    }
    return count;
}

int storage_manager_read_txt_file(const char *filename, char *buffer, int max_len)
{
    if (!filename_is_safe(filename) || !buffer || max_len <= 0)
        return -1;
    char path[280];
    snprintf(path, sizeof(path), "%s/%s", STORAGE_BASE_PATH, filename);

    FILE *f = fopen(path, "rb");
    if (!f) {
#ifdef ESP_PLATFORM
        ESP_LOGW(TAG, "Cannot open %s", path);
#else
        printf("Cannot open %s\n", path);
#endif
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < 0) {
        fclose(f);
        return -1;
    }
    if (size > 512000) { // Max 500KB TXT
        size = 512000;
    }
    if (size >= max_len) {
        size = max_len - 1;
    }

    size_t read_bytes = fread(buffer, 1, size, f);
    fclose(f);

    buffer[read_bytes] = '\0';
    return (int)read_bytes;
}

bool storage_manager_write_txt_file(const char *filename, const char *content)
{
    if (!filename_is_safe(filename) || !content)
        return false;
    char path[280];
    snprintf(path, sizeof(path), "%s/%s", STORAGE_BASE_PATH, filename);

    FILE *f = fopen(path, "wb");
    if (!f) {
#ifdef ESP_PLATFORM
        ESP_LOGW(TAG, "Cannot open %s for writing", path);
#else
        printf("Cannot open %s for writing\n", path);
#endif
        return false;
    }

    size_t len = strlen(content);
    size_t written = fwrite(content, 1, len, f);
    fclose(f);

    return written == len;
}

void storage_manager_format_bytes(uint32_t bytes, char *out_buf, int buf_size)
{
    if (!out_buf || buf_size <= 0)
        return;

    if (bytes < 1024) {
        snprintf(out_buf, buf_size, "%uB", (unsigned int)bytes);
    } else if (bytes < 1024 * 1024) {
        snprintf(out_buf, buf_size, "%.1fKB", bytes / 1024.0f);
    } else {
        snprintf(out_buf, buf_size, "%.1fMB", bytes / (1024.0f * 1024.0f));
    }
}
