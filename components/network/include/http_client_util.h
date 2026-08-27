/**
 * @file http_client_util.h
 * @brief Reusable esp_http_client GET wrappers shared by the network component.
 *
 * photo_downloader, weather_api and holiday_fetcher previously each carried
 * their own copy of the same "init -> event handler -> read -> cleanup"
 * boilerplate. These wrappers collapse that into one implementation.
 *
 * All functions return the number of bytes read into @p buf, or -1 on error
 * (client init failure, transport error, or a non-200 status code). They are
 * target-only (they wrap esp_http_client); the declarations are visible on the
 * host so callers compile, but the definitions live behind #ifdef ESP_PLATFORM.
 */
#ifndef HTTP_CLIENT_UTIL_H
#define HTTP_CLIENT_UTIL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief GET binary content into @p buf.
 *
 * No NUL termination is added (the payload may be raw image data).
 * @return bytes read, or -1 on error.
 */
int http_get_binary(const char *url, uint8_t *buf, size_t max_size);

/**
 * @brief GET text content into @p buf.
 *
 * The buffer is always NUL-terminated (@p max_size counts the terminator).
 * @return bytes read (excluding the NUL), or -1 on error.
 */
int http_get_text(const char *url, char *buf, size_t max_size);
/**
 * @brief GET text content, skipping TLS certificate verification.
 *
 * For endpoints whose root CA is not in the ESP-IDF crt bundle.
 * @return bytes read (excluding NUL), or -1 on error.
 */
int http_get_text_skip_tls(const char *url, char *buf, size_t max_size);

/**
 * @brief GET with custom headers and a specific root CA certificate.
 *
 * @param cert_pem PEM-encoded root CA (NUL-terminated). NULL uses crt_bundle.
 * @return bytes read (binary, no NUL termination), or -1 on error.
 */
int http_get_with_headers_cert(const char *url, const char **headers, const char **values, int header_count,
                               uint8_t *buf, size_t max_size, const char *cert_pem);

/**
 * @brief GET content with custom request headers.
 *
 * Each @p headers[i] = @p values[i] pair is applied via
 * esp_http_client_set_header() after init and before perform.
 * No NUL termination is added.
 * @return bytes read, or -1 on error.
 */
int http_get_with_headers(const char *url, const char **headers, const char **values, int header_count, uint8_t *buf,
                          size_t max_size);

/**
 * @brief DELETE returning the response body as text (NUL-terminated).
 *
 * Fridge memo DELETE carries the authoritative full items[] for both 200
 * and 404 (design doc §7.1), so the body matters, not just the status.
 * Target-only; host stub returns -1.
 * @return bytes read (excluding the NUL), or -1 on error.
 */
int http_delete_text(const char *url, char *buf, size_t max_size);

#ifdef __cplusplus
}
#endif

#endif /* HTTP_CLIENT_UTIL_H */
