/**
 * @file app_sntp.c
 * @brief SNTP clock synchronisation — init and callback registration.
 *
 * Extracted from application.c (Phase 2.1 module split).
 */
#include "application_internal.h"

#include <stdlib.h>
#include <esp_log.h>
#include <esp_sntp.h>

#define TAG "Application"

void app_sntp_start_once(void)
{
    static bool s_started = false;
    if (s_started)
        return;

    setenv("TZ", "CST-8", 1);
    tzset();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp.aliyun.com");
    esp_sntp_setservername(1, "cn.pool.ntp.org");
    esp_sntp_set_time_sync_notification_cb(app_sync_on_sntp_sync);
    esp_sntp_init();
    s_started = true;
    ESP_LOGI(TAG, "SNTP started: tz=Asia/Shanghai "
                  "servers=ntp.aliyun.com,cn.pool.ntp.org,pool.ntp.org");
}
