/**
 * @file http_client_util.c
 * @brief esp_http_client GET boilerplate, shared by the network component.
 *
 * Target-only: wraps esp_http_client init -> event handler -> read -> cleanup.
 * The response is streamed through a per-call context (no module-global
 * buffers) so concurrent callers do not stomp on each other's state.
 */
#include "http_client_util.h"

#ifdef ESP_PLATFORM
#    include "esp_http_client.h"
#    include "esp_crt_bundle.h"
#    include "esp_log.h"

static const char *TAG = "HttpUtil";

/** Per-call receive context threaded through esp_http_client user_data. */
typedef struct {
    uint8_t *buf;
    size_t   max_size;
    int      len;
} http_recv_ctx_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_recv_ctx_t *ctx = (http_recv_ctx_t *)evt->user_data;
    int              n;
    if (!ctx) {
        return ESP_OK;
    }
    if (evt->event_id != HTTP_EVENT_ON_DATA) {
        return ESP_OK;
    }
    n = evt->data_len;
    if (ctx->len + n > (int)ctx->max_size) {
        n = (int)ctx->max_size - ctx->len;
    }
    if (n > 0) {
        memcpy(ctx->buf + ctx->len, evt->data, (size_t)n);
        ctx->len += n;
    }
    return ESP_OK;
}

/* A single timeout suits every caller today: weather JSON (~2 KB) and the
 * 15 KB photo blobs both finish in well under this on a healthy link. */
#    define HTTP_UTIL_TIMEOUT_MS 15000

static int http_get_impl(const char *url, const char **headers, const char **values, int header_count, uint8_t *buf,
                         size_t max_size, int timeout_ms, const char *cert_pem)
{
    http_recv_ctx_t          ctx;
    esp_http_client_config_t config = {0};
    esp_http_client_handle_t client;
    esp_err_t                err;
    int                      status;
    int                      i;

    if (!url || !buf || max_size == 0) {
        return -1;
    }

    ctx.buf      = buf;
    ctx.max_size = max_size;
    ctx.len      = 0;

    config.url                   = url;
    config.method                = HTTP_METHOD_GET;
    config.event_handler         = http_event_handler;
    config.user_data             = &ctx;
    config.timeout_ms            = timeout_ms;
    config.disable_auto_redirect = false;
    if (cert_pem) {
        config.cert_pem = cert_pem;
    } else {
        config.crt_bundle_attach = esp_crt_bundle_attach;
    }
    /* Tx buffer holds request line + all headers. The JWT authorization
     * header alone is ~360 bytes; 512 default overflows. */
    config.buffer_size_tx = 2048;
    client                = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "init failed: %s", url);
        return -1;
    }

    for (i = 0; i < header_count; i++) {
        if (headers && values && headers[i] && values[i]) {
            esp_http_client_set_header(client, headers[i], values[i]);
        }
    }

    err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "perform failed: %s (%s)", url, esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return -1;
    }

    status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (status != 200) {
        ESP_LOGE(TAG, "HTTP %d: %s", status, url);
        return -1;
    }
    return ctx.len;
}

int http_get_binary(const char *url, uint8_t *buf, size_t max_size)
{
    return http_get_impl(url, NULL, NULL, 0, buf, max_size, HTTP_UTIL_TIMEOUT_MS, NULL);
}

int http_get_text(const char *url, char *buf, size_t max_size)
{
    int n = http_get_impl(url, NULL, NULL, 0, (uint8_t *)buf, max_size, HTTP_UTIL_TIMEOUT_MS, NULL);
    if (n < 0) {
        return -1;
    }
    if (n < (int)max_size) {
        buf[n] = '\0';
    } else if (max_size > 0) {
        buf[max_size - 1] = '\0';
        n                 = (int)max_size - 1;
    }
    return n;
}

int http_get_with_headers(const char *url, const char **headers, const char **values, int header_count, uint8_t *buf,
                          size_t max_size)
{
    return http_get_impl(url, headers, values, header_count, buf, max_size, HTTP_UTIL_TIMEOUT_MS, NULL);
}

int http_get_with_headers_cert(const char *url, const char **headers, const char **values, int header_count,
                               uint8_t *buf, size_t max_size, const char *cert_pem)
{
    return http_get_impl(url, headers, values, header_count, buf, max_size, HTTP_UTIL_TIMEOUT_MS, cert_pem);
}

#endif /* ESP_PLATFORM */
