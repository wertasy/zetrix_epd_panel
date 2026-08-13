/**
 * @file photo_storage.c
 * @brief LittleFS-backed photo storage — C port of photo_storage.{h,cc}.
 *
 * Filesystem access goes through the @ref storage_manager component (which
 * mounts LittleFS at /spiffs on target / creates ./spiffs on host). All file
 * I/O uses POSIX and therefore runs identically on both platforms, making the
 * metadata save/load cycle unit-testable on a Linux host.
 *
 * Storage layout (flat under the mount point):
 *   <id>.bin   raw 1bpp image data
 *   <id>.meta  JSON metadata (for human inspection / index rebuild)
 *   photos.idx binary index: magic "PHOT" + version + count + photo_info_t[]
 */
#include "photo_storage.h"
#include "storage_manager.h"
#include "cJSON.h"
#include "cjson_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>

/* Branch prediction hints for hot lookup paths. Guarded so the definitions in
 * components/rawdraw/include/rawdraw_util.h won't conflict if this translation
 * unit ever gains access to them. */
#ifndef likely
#    define likely(x) __builtin_expect(!!(x), 1)
#endif
#ifndef unlikely
#    define unlikely(x) __builtin_expect(!!(x), 0)
#endif

#ifdef ESP_PLATFORM
#    include "esp_log.h"

static const char *TAG = "PhotoStorage";
#    define LOGI(...) ESP_LOGI(TAG, __VA_ARGS__)
#    define LOGW(...) ESP_LOGW(TAG, __VA_ARGS__)
#    define LOGE(...) ESP_LOGE(TAG, __VA_ARGS__)
#    define LOGD(...) ESP_LOGD(TAG, __VA_ARGS__)
#else
#    include <unistd.h>
#    define LOGI(...)                                                                                                  \
        do {                                                                                                           \
            fprintf(stderr, "[PHO][I] " __VA_ARGS__);                                                                  \
            fputc('\n', stderr);                                                                                       \
        } while (0)
#    define LOGW(...)                                                                                                  \
        do {                                                                                                           \
            fprintf(stderr, "[PHO][W] " __VA_ARGS__);                                                                  \
            fputc('\n', stderr);                                                                                       \
        } while (0)
#    define LOGE(...)                                                                                                  \
        do {                                                                                                           \
            fprintf(stderr, "[PHO][E] " __VA_ARGS__);                                                                  \
            fputc('\n', stderr);                                                                                       \
        } while (0)
#    define LOGD(...)                                                                                                  \
        do {                                                                                                           \
        } while (0)
#endif

#ifdef ESP_PLATFORM
#    define PHOTO_BASE_PATH "/spiffs"
#else
#    define PHOTO_BASE_PATH "./spiffs"
#endif

#define PHOTO_DIR PHOTO_BASE_PATH
#define PHOTO_INDEX PHOTO_BASE_PATH "/photos.idx"

/* Index file format:
 * Bytes 0-3:   magic  "PHOT" (0x50484F54)
 * Bytes 4-5:   version (uint16)
 * Bytes 6-7:   count   (uint16)
 * Then count * sizeof(photo_info_t) records. */
#define INDEX_MAGIC 0x50484F54u /* "PHOT" */
#define INDEX_VERSION 2

#ifdef ESP_PLATFORM
#    include <freertos/FreeRTOS.h>
#    include <freertos/semphr.h>
static SemaphoreHandle_t s_photo_mutex = NULL;
#else
#    include <pthread.h>
static pthread_mutex_t s_photo_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

static void lock_photo(void)
{
#ifdef ESP_PLATFORM
    if (s_photo_mutex) {
        xSemaphoreTake(s_photo_mutex, portMAX_DELAY);
    }
#else
    pthread_mutex_lock(&s_photo_mutex);
#endif
}

static void unlock_photo(void)
{
#ifdef ESP_PLATFORM
    if (s_photo_mutex) {
        xSemaphoreGive(s_photo_mutex);
    }
#else
    pthread_mutex_unlock(&s_photo_mutex);
#endif
}

static bool is_safe_photo_id(const char *id)
{
    if (!id || id[0] == '\0') {
        return false;
    }
    size_t len = strlen(id);
    if (len > 64) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)id[i];
        if (!isalnum(c) && c != '_' && c != '-') {
            return false;
        }
    }
    return true;
}

static photo_info_t s_photos[PHOTO_MAX_PHOTOS];
static int s_photo_count = 0;
static bool s_initialized = false;

#define HASH_BUCKETS_COUNT 32
#define HASH_BUCKET_MASK (HASH_BUCKETS_COUNT - 1)

static int photo_hash_buckets[HASH_BUCKETS_COUNT] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                                                     -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
static int s_photo_hash_next[PHOTO_MAX_PHOTOS] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                                                  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                                                  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};

static unsigned int djb2_hash(const char *str)
{
    unsigned int hash = 5381;
    int c;
    while ((c = (unsigned char)*str++)) {
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }
    return hash;
}

static void rebuild_hash_table(void)
{
    int i;
    for (i = 0; i < HASH_BUCKETS_COUNT; i++) {
        photo_hash_buckets[i] = -1;
    }
    for (i = 0; i < PHOTO_MAX_PHOTOS; i++) {
        s_photo_hash_next[i] = -1;
    }
    for (i = 0; i < s_photo_count; i++) {
        unsigned int bucket = djb2_hash(s_photos[i].id) & HASH_BUCKET_MASK;
        s_photo_hash_next[i] = photo_hash_buckets[bucket];
        photo_hash_buckets[bucket] = i;
    }
}

static int find_photo_index_by_id(const char *id)
{
    if (!id || id[0] == '\0') {
        return -1;
    }
    unsigned int bucket = djb2_hash(id) & HASH_BUCKET_MASK;
    int idx = photo_hash_buckets[bucket];
    while (idx != -1) {
        if (unlikely(strcmp(s_photos[idx].id, id) == 0)) {
            return idx;
        }
        idx = s_photo_hash_next[idx];
    }
    return -1;
}

/* Forward declarations of static helpers. */
static int save_index(void);
static int write_meta_file(const photo_info_t *info);
static bool load_meta_file(const char *meta_path, photo_info_t *out_info);
static void apply_default_metadata(photo_info_t *info);

/* ============================================================ */
/* Small helpers                                                */
/* ============================================================ */

static bool is_digits_string(const char *value)
{
    const char *p;
    if (!value || value[0] == '\0')
        return false;
    for (p = value; *p; p++) {
        if (!isdigit((unsigned char)*p))
            return false;
    }
    return true;
}

static void format_epoch_date(uint64_t epoch, char *out, size_t out_size)
{
    time_t ts;
    struct tm tm_buf;
    if (!out || out_size == 0)
        return;
    if (epoch > 100000000000ULL) {
        epoch /= 1000; /* millisecond timestamp -> seconds */
    }
    ts = (time_t)epoch;
    if (localtime_r(&ts, &tm_buf) == NULL)
        return;
    strftime(out, out_size, "%Y-%m-%d", &tm_buf);
}

static void apply_default_metadata(photo_info_t *info)
{
    if (!info)
        return;
    if (info->title[0] == '\0') {
        snprintf(info->title, sizeof(info->title), "%s",
                 "\xe9\x82\xa3\xe5\xb9\xb4\xe4\xbb\x8a\xe6\x97\xa5"); /* 那年今日 */
    }
    if (info->date[0] == '\0' && info->timestamp > 0) {
        format_epoch_date(info->timestamp, info->date, sizeof(info->date));
    } else if (is_digits_string(info->date) && strlen(info->date) >= 9) {
        format_epoch_date((uint32_t)strtoull(info->date, NULL, 10), info->date, sizeof(info->date));
    }
    if (info->location[0] == '\0') {
        snprintf(info->location, sizeof(info->location), "%s",
                 "\xe6\x9c\xaa\xe7\x9f\xa5\xe5\x9c\xb0\xe7\x82\xb9"); /* 未知地点 */
    }
    if (info->body[0] == '\0') {
        snprintf(info->body, sizeof(info->body), "%s",
                 "\xe6\x9a\x82\xe6\x97\xa0\xe6\x96\x87\xe6\xa1\x88"); /* 暂无文案 */
    }
}

/* ============================================================ */
/* JSON metadata files                                          */
/* ============================================================ */

/* Write a JSON-escaped string (including surrounding quotes) to a FILE*. */
static void write_json_string(FILE *f, const char *s)
{
    fputc('"', f);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            fputc('\\', f);
            fputc((int)c, f);
        } else if (c == '\n') {
            fputs("\\n", f);
        } else if (c == '\r') {
            fputs("\\r", f);
        } else if (c == '\t') {
            fputs("\\t", f);
        } else if (c < 0x20) {
            fprintf(f, "\\u%04x", c);
        } else {
            fputc((int)c, f); /* raw UTF-8 bytes pass through */
        }
    }
    fputc('"', f);
}

static int write_meta_file(const photo_info_t *info)
{
    char meta_path[PHOTO_MAX_PATH];
    FILE *f;

    if (!info || info->id[0] == '\0')
        return -1;
    snprintf(meta_path, sizeof(meta_path), "%s/%s.meta", PHOTO_DIR, info->id);

    f = fopen(meta_path, "w");
    if (!f) {
        LOGE("Failed to open %s for writing", meta_path);
        return -1;
    }

    fputc('{', f);
    fputs("\"id\":", f);
    write_json_string(f, info->id);
    fputs(",\"title\":", f);
    write_json_string(f, info->title);
    fputs(",\"date\":", f);
    write_json_string(f, info->date);
    fputs(",\"location\":", f);
    write_json_string(f, info->location);
    fputs(",\"body\":", f);
    write_json_string(f, info->body);
    fprintf(f, ",\"width\":%u,\"height\":%u,\"file_size\":%lu,\"timestamp\":%lu,\"path\":", (unsigned)info->width,
            (unsigned)info->height, (unsigned long)info->file_size, (unsigned long)info->timestamp);
    write_json_string(f, info->path);
    fputc('}', f);

    if (ferror(f)) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

static bool load_meta_file(const char *meta_path, photo_info_t *out_info)
{
    FILE *f;
    long len;
    size_t read_len;
    char *json_buf;
    cJSON *root;
    bool ok = false;

    if (!meta_path || !out_info)
        return false;

    f = fopen(meta_path, "rb");
    if (!f) {
        LOGW("Failed to open meta file: %s", meta_path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0 || len > 4096) {
        fclose(f);
        LOGW("Invalid meta file size: %s", meta_path);
        return false;
    }

    json_buf = (char *)malloc((size_t)len + 1);
    if (!json_buf) {
        fclose(f);
        return false;
    }
    read_len = fread(json_buf, 1, (size_t)len, f);
    fclose(f);
    if (read_len != (size_t)len) {
        free(json_buf);
        LOGW("Short read for meta file: %s", meta_path);
        return false;
    }
    json_buf[read_len] = '\0';

    memset(out_info, 0, sizeof(*out_info));
    root = cJSON_Parse(json_buf);
    if (root) {
        cjson_copy_str(root, "id", out_info->id, sizeof(out_info->id));
        cjson_copy_str(root, "title", out_info->title, sizeof(out_info->title));
        cjson_copy_str(root, "date", out_info->date, sizeof(out_info->date));
        cjson_copy_str(root, "location", out_info->location, sizeof(out_info->location));
        cjson_copy_str(root, "body", out_info->body, sizeof(out_info->body));
        out_info->width = (uint16_t)cjson_get_int(root, "width", 0);
        out_info->height = (uint16_t)cjson_get_int(root, "height", 0);
        out_info->file_size = (uint32_t)cjson_get_int(root, "file_size", 0);
        out_info->timestamp = (uint32_t)cjson_get_int(root, "timestamp", 0);
        cjson_copy_str(root, "path", out_info->path, sizeof(out_info->path));
    }
    free(json_buf);
    cJSON_Delete(root);

    ok = out_info->id[0] != '\0';
    if (ok)
        apply_default_metadata(out_info);
    return ok;
}

/* ============================================================ */
/* Binary index file                                            */
/* ============================================================ */

static int save_index(void)
{
    FILE *f = fopen(PHOTO_INDEX, "wb");
    uint32_t magic = INDEX_MAGIC;
    uint16_t version = INDEX_VERSION;
    uint16_t count = (uint16_t)s_photo_count;
    if (!f) {
        LOGE("Failed to write index %s", PHOTO_INDEX);
        return -1;
    }
    fwrite(&magic, sizeof(magic), 1, f);
    fwrite(&version, sizeof(version), 1, f);
    fwrite(&count, sizeof(count), 1, f);
    if (s_photo_count > 0) {
        fwrite(s_photos, sizeof(photo_info_t), (size_t)s_photo_count, f);
    }
    if (ferror(f)) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

static int rebuild_index_from_meta_files(void)
{
    DIR *dir = opendir(PHOTO_DIR);
    struct dirent *entry;

    if (!dir) {
        LOGW("Failed to open photo dir for rebuild");
        return -1;
    }
    s_photo_count = 0;
    while ((entry = readdir(dir)) != NULL && s_photo_count < PHOTO_MAX_PHOTOS) {
        const char *name = entry->d_name;
        size_t name_len = strlen(name);
        char meta_path[320];
        photo_info_t *info;
        if (name_len < 6 || strcasecmp(name + name_len - 5, ".meta") != 0) {
            continue;
        }
        snprintf(meta_path, sizeof(meta_path), "%s/%s", PHOTO_DIR, name);
        info = &s_photos[s_photo_count];
        if (!load_meta_file(meta_path, info))
            continue;
        if (info->path[0] == '\0') {
            char idbuf[16];
            snprintf(idbuf, sizeof(idbuf), "%s", info->id);
            snprintf(info->path, sizeof(info->path), "%s/%s.bin", PHOTO_DIR, idbuf);
        }
        s_photo_count++;
    }
    closedir(dir);
    rebuild_hash_table();
    save_index();
    LOGI("Rebuilt photo index from meta files: %d photos", s_photo_count);
    return s_photo_count;
}

static int load_index(void)
{
    FILE *f = fopen(PHOTO_INDEX, "rb");
    uint32_t magic = 0;
    uint16_t version = 0;
    uint16_t count = 0;

    if (!f) {
        s_photo_count = 0;
        return 0;
    }
    if (fread(&magic, sizeof(magic), 1, f) != 1 || fread(&version, sizeof(version), 1, f) != 1 ||
        fread(&count, sizeof(count), 1, f) != 1) {
        fclose(f);
        LOGE("Failed to read index header");
        s_photo_count = 0;
        return -1;
    }
    if (magic != INDEX_MAGIC || version != INDEX_VERSION) {
        fclose(f);
        LOGW("Index version mismatch (magic=0x%08lX ver=%u), rebuilding", (unsigned long)magic, (unsigned)version);
        s_photo_count = 0;
        return rebuild_index_from_meta_files();
    }
    if (count > PHOTO_MAX_PHOTOS)
        count = PHOTO_MAX_PHOTOS;

    s_photo_count = 0;
    {
        int i;
        for (i = 0; i < count; i++) {
            if (fread(&s_photos[i], sizeof(photo_info_t), 1, f) == 1) {
                if (s_photos[i].id[0] != '\0' && s_photos[i].path[0] != '\0') {
                    apply_default_metadata(&s_photos[i]);
                    s_photo_count++;
                }
            }
        }
    }
    fclose(f);
    rebuild_hash_table();
    LOGI("Loaded %d photos from index", s_photo_count);
    return 0;
}

/* ============================================================ */
/* Public API                                                   */
/* ============================================================ */

int photo_storage_init(void)
{
#ifdef ESP_PLATFORM
    if (!s_photo_mutex) {
        s_photo_mutex = xSemaphoreCreateMutex();
    }
#endif
    lock_photo();
    if (s_initialized) {
        unlock_photo();
        return 0;
    }
    /* Mount LittleFS (target) / ensure ./spiffs exists (host). */
    if (!storage_manager_init()) {
        LOGE("storage_manager_init failed");
        unlock_photo();
        return -1;
    }
    load_index();
    s_initialized = true;
    LOGI("Photo storage initialised (%d photos)", s_photo_count);
    unlock_photo();
    return 0;
}

int photo_storage_reload_index(void)
{
    /* Mount if needed, then re-read the index from disk. */
    if (!s_initialized) {
        if (photo_storage_init() != 0)
            return -1;
        return 0;
    }
    lock_photo();
    s_photo_count = 0;
    int ret = load_index();
    unlock_photo();
    return ret;
}

int photo_save(const photo_info_t *info, const uint8_t *data_1bpp)
{
    char bin_path[PHOTO_MAX_PATH];
    FILE *f;
    size_t written;
    int existing_index = -1;
    photo_info_t *entry;
    photo_info_t backup_entry;
    bool is_update = false;
    if (!info || !data_1bpp)
        return -1;

    if (!is_safe_photo_id(info->id)) {
        LOGE("Unsafe photo ID: %s", info->id);
        return -1;
    }

    if (!s_initialized) {
        if (photo_storage_init() != 0)
            return -1;
    }

    lock_photo();
    existing_index = find_photo_index_by_id(info->id);
    if (existing_index < 0 && s_photo_count >= PHOTO_MAX_PHOTOS) {
        LOGE("Photo storage full (%d)", PHOTO_MAX_PHOTOS);
        unlock_photo();
        return -1;
    }
    if (existing_index >= 0) {
        is_update = true;
        memcpy(&backup_entry, &s_photos[existing_index], sizeof(photo_info_t));
    }
    unlock_photo();

    snprintf(bin_path, sizeof(bin_path), "%s/%s.bin", PHOTO_DIR, info->id);
    f = fopen(bin_path, "wb");
    if (!f) {
        LOGE("Failed to open %s for writing", bin_path);
        return -1;
    }
    written = fwrite(data_1bpp, 1, info->file_size, f);
    fclose(f);
    if (written != info->file_size) {
        LOGE("Failed to write full data: %lu/%lu", (unsigned long)written, (unsigned long)info->file_size);
        remove(bin_path);
        return -1;
    }

    lock_photo();
    entry = (existing_index >= 0) ? &s_photos[existing_index] : &s_photos[s_photo_count];
    memcpy(entry, info, sizeof(photo_info_t));
    snprintf(entry->path, sizeof(entry->path), "%s", bin_path);
    apply_default_metadata(entry);
    if (existing_index < 0) {
        s_photo_count++;
    }
    rebuild_hash_table();

    if (write_meta_file(entry) != 0 || save_index() != 0) {
        // Rollback memory state
        if (is_update) {
            memcpy(&s_photos[existing_index], &backup_entry, sizeof(photo_info_t));
        } else {
            s_photo_count--;
            memset(&s_photos[s_photo_count], 0, sizeof(photo_info_t));
        }
        rebuild_hash_table();
        unlock_photo();
        remove(bin_path);
        return -1;
    }
    int current_count = s_photo_count;
    unlock_photo();

    LOGI("%s photo %s (%dx%d, %lu bytes), total=%d", is_update ? "Updated" : "Saved", info->id, (int)info->width,
         (int)info->height, (unsigned long)info->file_size, current_count);
    return 0;
}

int photo_load(const char *id, uint8_t *out_buffer, uint32_t max_size)
{
    if (!s_initialized || !id || !out_buffer)
        return -1;
    if (!is_safe_photo_id(id))
        return -1;

    lock_photo();
    int idx = find_photo_index_by_id(id);
    if (idx >= 0) {
        FILE *f;
        size_t n;
        if (s_photos[idx].file_size > max_size) {
            LOGE("Buffer too small: %lu > %lu", (unsigned long)s_photos[idx].file_size, (unsigned long)max_size);
            unlock_photo();
            return -1;
        }
        char path[PHOTO_MAX_PATH];
        uint32_t file_size = s_photos[idx].file_size;
        strncpy(path, s_photos[idx].path, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
        unlock_photo();

        f = fopen(path, "rb");
        if (!f) {
            LOGE("Failed to open %s", path);
            return -1;
        }
        n = fread(out_buffer, 1, file_size, f);
        fclose(f);
        if (n != file_size) {
            LOGE("Read incomplete: %lu/%lu", (unsigned long)n, (unsigned long)file_size);
            return -1;
        }
        return (int)n;
    }
    unlock_photo();
    return -1;
}

int photo_list(photo_info_t *out_list, int max_count)
{
    int i, n;
    if (!s_initialized || !out_list || max_count <= 0)
        return 0;
    lock_photo();
    n = (max_count < s_photo_count) ? max_count : s_photo_count;
    for (i = 0; i < n; i++) {
        memcpy(&out_list[i], &s_photos[i], sizeof(photo_info_t));
    }
    unlock_photo();
    return n;
}

int photo_delete(const char *id)
{
    int idx = -1;
    int i;
    if (!s_initialized || !id)
        return -1;
    if (!is_safe_photo_id(id))
        return -1;

    lock_photo();
    idx = find_photo_index_by_id(id);
    if (idx < 0) {
        unlock_photo();
        return -1;
    }

    char path_to_remove[PHOTO_MAX_PATH] = {0};
    if (s_photos[idx].path[0] != '\0') {
        strncpy(path_to_remove, s_photos[idx].path, sizeof(path_to_remove) - 1);
    }

    for (i = idx; i < s_photo_count - 1; i++) {
        s_photos[i] = s_photos[i + 1];
    }
    s_photo_count--;
    rebuild_hash_table();
    save_index();
    unlock_photo();

    if (path_to_remove[0] != '\0') {
        remove(path_to_remove);
    }
    char meta_path[PHOTO_MAX_PATH];
    snprintf(meta_path, sizeof(meta_path), "%s/%s.meta", PHOTO_DIR, id);
    remove(meta_path);

    LOGI("Deleted photo %s", id);
    return 0;
}

int photo_get_count(void)
{
    if (!s_initialized) {
        if (photo_storage_init() != 0)
            return 0;
    }
    lock_photo();
    int count = s_photo_count;
    unlock_photo();
    return count;
}

int photo_get_by_index(int index, photo_info_t *out)
{
    if (!s_initialized) {
        if (photo_storage_init() != 0)
            return -1;
    }
    lock_photo();
    if (!out || index < 0 || index >= s_photo_count) {
        unlock_photo();
        return -1;
    }
    memcpy(out, &s_photos[index], sizeof(photo_info_t));
    unlock_photo();
    return 0;
}

bool photo_exists(const char *id)
{
    if (!s_initialized || !id)
        return false;
    if (!is_safe_photo_id(id))
        return false;
    lock_photo();
    int idx = find_photo_index_by_id(id);
    unlock_photo();
    return idx >= 0;
}

int photo_update_info(const char *id, const photo_info_t *updates)
{
    if (!s_initialized) {
        if (photo_storage_init() != 0)
            return -1;
    }
    if (!id || !updates)
        return -1;
    if (!is_safe_photo_id(id))
        return -1;

    lock_photo();
    int idx = find_photo_index_by_id(id);
    if (idx >= 0) {
        photo_info_t backup = s_photos[idx];

        snprintf(s_photos[idx].title, sizeof(s_photos[idx].title), "%s", updates->title);
        snprintf(s_photos[idx].date, sizeof(s_photos[idx].date), "%s", updates->date);
        snprintf(s_photos[idx].location, sizeof(s_photos[idx].location), "%s", updates->location);
        snprintf(s_photos[idx].body, sizeof(s_photos[idx].body), "%s", updates->body);
        apply_default_metadata(&s_photos[idx]);

        if (write_meta_file(&s_photos[idx]) != 0 || save_index() != 0) {
            s_photos[idx] = backup;
            unlock_photo();
            return -1;
        }
        unlock_photo();
        LOGI("Updated photo metadata %s", id);
        return 0;
    }
    unlock_photo();
    return -1;
}

int photo_move(const char *id, int delta)
{
    int idx = -1;
    int target;
    photo_info_t tmp;
    if (!s_initialized) {
        if (photo_storage_init() != 0)
            return -1;
    }
    if (!id || delta == 0)
        return -1;
    if (!is_safe_photo_id(id))
        return -1;

    lock_photo();
    idx = find_photo_index_by_id(id);
    if (idx < 0) {
        unlock_photo();
        return -1;
    }
    target = idx + (delta < 0 ? -1 : 1);
    if (target < 0 || target >= s_photo_count) {
        unlock_photo();
        return -1;
    }
    tmp = s_photos[idx];
    s_photos[idx] = s_photos[target];
    s_photos[target] = tmp;
    rebuild_hash_table();
    if (save_index() != 0) {
        s_photos[target] = s_photos[idx];
        s_photos[idx] = tmp;
        rebuild_hash_table();
        unlock_photo();
        return -1;
    }
    unlock_photo();
    LOGI("Moved photo %s from %d to %d", id, idx, target);
    return 0;
}
