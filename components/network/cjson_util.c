/**
 * @file cjson_util.c
 * @brief Shared cJSON accessors — see cjson_util.h.
 */
#include "cjson_util.h"

#include <stdio.h>
#include <stdlib.h>

void cjson_copy_str(cJSON *obj, const char *key, char *dst, size_t dst_size)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(item) && item->valuestring) {
        snprintf(dst, dst_size, "%s", item->valuestring);
    }
}

int cjson_get_int(cJSON *obj, const char *key, int def_val)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!item)
        return def_val;
    if (cJSON_IsString(item) && item->valuestring)
        return atoi(item->valuestring);
    return (int)cJSON_GetNumberValue(item);
}
