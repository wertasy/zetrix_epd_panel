/**
 * @file photo_downloader.c
 * @brief HTTP photo downloader — C port of photo_downloader.{h,cc}.
 *
 * HTTP transport (esp_http_client) is target-only. The server photo-list JSON
 * is parsed with the official cJSON library. All public functions
 * are no-ops (return -1/false) on the host.
 *
 * Note: the server provides already-quantised 1bpp image data; there is no
 * client-side 4-colour quantisation in the original C++ source, so none is
 * ported here. If future server payloads carry colour data, a quantiser would
 * be added at the download step.
 */
#include "photo_downloader.h"
#include "photo_storage.h"
#include "cJSON.h"
#include "cjson_util.h"
#include "http_client_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#    include "esp_log.h"
#    include "esp_http_client.h"
#    include "esp_crt_bundle.h"

static const char *TAG = "PhotoDL";
#    define LOGI(...) ESP_LOGI(TAG, __VA_ARGS__)
#    define LOGW(...) ESP_LOGW(TAG, __VA_ARGS__)
#    define LOGE(...) ESP_LOGE(TAG, __VA_ARGS__)
#    define LOGD(...) ESP_LOGD(TAG, __VA_ARGS__)
#else
#    include <stdio.h>
#    define LOGI(...)                                                                                                  \
        do {                                                                                                           \
            fprintf(stderr, "[PDL][I] " __VA_ARGS__);                                                                  \
            fputc('\n', stderr);                                                                                       \
        } while (0)
#    define LOGW(...)                                                                                                  \
        do {                                                                                                           \
            fprintf(stderr, "[PDL][W] " __VA_ARGS__);                                                                  \
            fputc('\n', stderr);                                                                                       \
        } while (0)
#    define LOGE(...)                                                                                                  \
        do {                                                                                                           \
            fprintf(stderr, "[PDL][E] " __VA_ARGS__);                                                                  \
            fputc('\n', stderr);                                                                                       \
        } while (0)
#    define LOGD(...)                                                                                                  \
        do {                                                                                                           \
        } while (0)
#endif

/* ============================================================ */
/* Static state                                                 */
/* ============================================================ */

static char s_server_url[PHOTO_DOWNLOADER_URL_MAX] = {0};
static bool s_initialized = false;
static bool s_syncing = false;

#ifdef ESP_PLATFORM
/* Download buffer: 400x300 1bpp = 15000 bytes. */
#    define PHOTO_DL_BUF_SIZE (400 * 300 / 8)
static uint8_t s_photo_buf[PHOTO_DL_BUF_SIZE];
#endif

/* ============================================================ */
/* Server photo list entry                                      */
/* ============================================================ */

#ifdef ESP_PLATFORM

typedef struct {
    char id[16];
    char title[64];
    uint16_t width;
    uint16_t height;
    uint32_t file_size;
    uint32_t timestamp;
} server_photo_entry_t;

/* Parse a JSON array of photo objects. Returns count filled, or -1 on error. */
static int parse_photo_list(const char *json, server_photo_entry_t *entries, int max_entries)
{
    cJSON *root;
    int count = 0;
    int i;
    if (!json || !entries || max_entries <= 0)
        return -1;

    root = cJSON_Parse(json);
    if (!root)
        return 0; /* unparseable payload: no entries */

    if (cJSON_IsArray(root)) {
        count = cJSON_GetArraySize(root);
        if (count > max_entries)
            count = max_entries;

        for (i = 0; i < count; i++) {
            cJSON *elem = cJSON_GetArrayItem(root, i);
            if (!elem) {
                entries[i].id[0] = '\0';
                continue;
            }
            memset(&entries[i], 0, sizeof(entries[i]));
            cjson_copy_str(elem, "id", entries[i].id, sizeof(entries[i].id));
            cjson_copy_str(elem, "title", entries[i].title, sizeof(entries[i].title));
            entries[i].width = (uint16_t)cjson_get_int(elem, "width", 0);
            entries[i].height = (uint16_t)cjson_get_int(elem, "height", 0);
            entries[i].file_size = (uint32_t)cjson_get_int(elem, "size", 0);
            entries[i].timestamp = (uint32_t)cjson_get_int(elem, "ts", 0);
        }
    }
    cJSON_Delete(root);
    return count;
}

#endif /* ESP_PLATFORM */

/* ============================================================ */
/* HTTP helpers (target only)                                   */
/* ============================================================ */

#ifdef ESP_PLATFORM

static int http_post(const char *url)
{
    esp_http_client_config_t config = {0};
    esp_http_client_handle_t client;
    esp_err_t err;
    int status;

    config.url = url;
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = 5000;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    client = esp_http_client_init(&config);
    if (!client)
        return -1;
    err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        LOGW("POST %s failed: %s", url, esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return -1;
    }
    status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    return (status == 200) ? 0 : -1;
}

static int download_photo_binary(const char *photo_id, uint8_t *out_buf, uint32_t max_size)
{
    char url[512];
    int n;

    snprintf(url, sizeof(url), "%.127s/api/photos/%.15s.bin", s_server_url, photo_id);
    n = http_get_binary(url, out_buf, max_size);
    if (n <= 0) {
        LOGE("Download %s failed", photo_id);
        return -1;
    }
    return n;
}

#endif /* ESP_PLATFORM */

/* ============================================================ */
/* Public API                                                   */
/* ============================================================ */

int photo_downloader_init(const photo_downloader_config_t *cfg)
{
    if (!cfg || !cfg->server_url[0]) {
        LOGE("Invalid config");
        return -1;
    }
    snprintf(s_server_url, sizeof(s_server_url), "%s", cfg->server_url);
    s_initialized = true;
    s_syncing = false;
    LOGI("Photo downloader initialised: %s", s_server_url);
    return 0;
}

int photo_sync(void)
{
#ifdef ESP_PLATFORM
    char url[512];
    char list_buf[4096];
    int resp_len;
    int server_count, downloaded = 0;
    int i;
    server_photo_entry_t server_entries[PHOTO_MAX_PHOTOS];

    if (!s_initialized) {
        LOGE("Not initialised");
        return -1;
    }
    if (s_syncing) {
        LOGW("Sync already in progress");
        return -1;
    }
    s_syncing = true;

    snprintf(url, sizeof(url), "%s/api/photos", s_server_url);
    LOGI("Fetching photo list from %s", url);
    resp_len = http_get_text(url, list_buf, sizeof(list_buf));
    if (resp_len < 0) {
        LOGE("Failed to fetch photo list");
        s_syncing = false;
        return -1;
    }
    server_count = parse_photo_list(list_buf, server_entries, PHOTO_MAX_PHOTOS);
    if (server_count < 0) {
        LOGE("Failed to parse server photo list");
        s_syncing = false;
        return -1;
    }
    LOGI("Server has %d photos", server_count);

    for (i = 0; i < server_count; i++) {
        photo_info_t info;
        int bytes;
        if (photo_exists(server_entries[i].id))
            continue;

        bytes = download_photo_binary(server_entries[i].id, s_photo_buf, sizeof(s_photo_buf));
        if (bytes <= 0) {
            LOGE("Failed to download %s", server_entries[i].id);
            continue;
        }
        memset(&info, 0, sizeof(info));
        /* Precision-limited %s gives gcc a static bound (id<=15), so
         * -Wformat-truncation knows it fits in the 16-byte field. */
        snprintf(info.id, sizeof(info.id), "%.15s", server_entries[i].id);
        snprintf(info.title, sizeof(info.title), "%s", server_entries[i].title);
        info.width = server_entries[i].width;
        info.height = server_entries[i].height;
        info.file_size = (uint32_t)bytes;
        info.timestamp = server_entries[i].timestamp;

        if (photo_save(&info, s_photo_buf) == 0) {
            char confirm_url[256];
            downloaded++;
            /* Precision-limited %s gives gcc a static bound (server_url<=127,
             * id<=15), so -Wformat-truncation knows it fits in 256 bytes. */
            snprintf(confirm_url, sizeof(confirm_url), "%.127s/api/photos/%.15s/downloaded", s_server_url,
                     server_entries[i].id);
            http_post(confirm_url);
        } else {
            LOGE("Failed to save photo %s", server_entries[i].id);
        }
    }

    s_syncing = false;
    LOGI("Sync complete: %d new photos (total: %d)", downloaded, photo_get_count());
    return downloaded;
#else
    return -1;
#endif
}

int photo_download_single(const char *photo_id)
{
#ifdef ESP_PLATFORM
    char url[256];
    char list_buf[4096];
    int resp_len, bytes;
    photo_info_t info;

    if (!s_initialized || !photo_id)
        return -1;
    if (photo_exists(photo_id)) {
        LOGI("Photo %s already exists locally", photo_id);
        return 0;
    }

    bytes = download_photo_binary(photo_id, s_photo_buf, sizeof(s_photo_buf));
    if (bytes <= 0)
        return -1;

    /* Best-effort metadata fetch from the server list. */
    bool found_meta = false;
    server_photo_entry_t matched_entry;
    server_photo_entry_t *server_entries = malloc(sizeof(server_photo_entry_t) * PHOTO_MAX_PHOTOS);

    if (server_entries) {
        snprintf(url, sizeof(url), "%.127s/api/photos", s_server_url);
        resp_len = http_get_text(url, list_buf, sizeof(list_buf));
        if (resp_len > 0) {
            int server_count = parse_photo_list(list_buf, server_entries, PHOTO_MAX_PHOTOS);
            for (int i = 0; i < server_count; i++) {
                if (strcmp(server_entries[i].id, photo_id) == 0) {
                    matched_entry = server_entries[i];
                    found_meta = true;
                    break;
                }
            }
        }
        free(server_entries);
    }

    memset(&info, 0, sizeof(info));
    snprintf(info.id, sizeof(info.id), "%s", photo_id);
    if (found_meta) {
        snprintf(info.title, sizeof(info.title), "%s", matched_entry.title);
        info.width = matched_entry.width;
        info.height = matched_entry.height;
    } else {
        snprintf(info.title, sizeof(info.title), "%.15s", photo_id);
        info.width = 400;
        info.height = 300;
    }
    info.file_size = (uint32_t)bytes;
    return photo_save(&info, s_photo_buf);
#else
    (void)photo_id;
    return -1;
#endif
}

bool photo_downloader_is_ready(void)
{
    return s_initialized;
}

bool photo_downloader_is_syncing(void)
{
    return s_syncing;
}
