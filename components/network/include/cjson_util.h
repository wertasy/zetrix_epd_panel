/**
 * @file cjson_util.h
 * @brief Small shared cJSON accessors used across the network component.
 *
 * These helpers centralise the common pattern of pulling a string or integer
 * member out of a cJSON object with safe defaults, so every caller behaves
 * identically. They depend only on cJSON itself and therefore build and run
 * on both the ESP-IDF target and a Linux host (used by the unit tests).
 */
#ifndef CJSON_UTIL_H
#define CJSON_UTIL_H

#include "cJSON.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Copy a JSON string member into a fixed buffer (safe truncation).
 *
 * If @p key is missing, null, or not a string, @p dst is left unchanged
 * (callers typically pre-zero the destination buffer).
 */
void cjson_copy_str(cJSON *obj, const char *key, char *dst, size_t dst_size);

/**
 * Return an integer JSON member, also accepting string-encoded numbers
 * (e.g. `"42"`) to match the original scanner's behaviour.
 *
 * Returns @p def_val when the member is missing or null.
 */
int cjson_get_int(cJSON *obj, const char *key, int def_val);

#ifdef __cplusplus
}
#endif

#endif /* CJSON_UTIL_H */
