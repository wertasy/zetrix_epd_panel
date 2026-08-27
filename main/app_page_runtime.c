/**
 * @file app_page_runtime.c
 * @brief Application service registry implementation (see header for the
 *        ownership model). Plan §3.7 P3 + D3/D5.
 */
#include "app_page_runtime.h"
#include "application_internal.h"

#include <string.h>

#include <esp_log.h>

#include "ap_transfer_server.h"
#include "wifi_manager.h"
#include "page_registry.h"
#include "page_runtime.h"

#define TAG "AppPageRuntime"

typedef struct {
    app_service_id_t id;
    const char *name;
    bool (*is_running)(void);
    bool (*start)(void);
} app_service_def_t;

typedef struct {
    svc_owner_t owner_kind;
    ui_page_id_t owner_page;
    bool sticky; /* D5: transfer in progress — survive page exit */
} app_service_state_t;

static app_service_state_t s_state[2]; /* indexed by service bit position */

/* ---- service adapters (context via the s_transfer_server singleton) ---- */

static bool lan_http_is_running(void)
{
    return ap_transfer_server_is_running(&s_transfer_server) &&
           ap_transfer_server_is_lan_mode(&s_transfer_server);
}

static bool lan_http_start(void)
{
    char ip[32] = {0};
    wifi_manager_get_ip(ip, sizeof(ip));
    if (ip[0] == '\0') {
        ESP_LOGW(TAG, "LAN HTTP start: no station IP");
        return false;
    }
    return ap_transfer_server_start_lan(&s_transfer_server, ip);
}

static bool ap_transfer_is_running(void)
{
    return ap_transfer_server_is_running(&s_transfer_server) &&
           ap_transfer_server_is_ap_mode(&s_transfer_server);
}

static bool ap_transfer_start(void)
{
    ap_transfer_server_start(&s_transfer_server); /* async; state cb reports */
    return true;
}

static const app_service_def_t s_defs[] = {
    [0] = {.id = APP_SVC_LAN_HTTP, .name = "lan_http", .is_running = lan_http_is_running, .start = lan_http_start},
    [1] = {.id = APP_SVC_AP_TRANSFER, .name = "ap_transfer", .is_running = ap_transfer_is_running, .start = ap_transfer_start},
};

#define SVC_COUNT (int)(sizeof(s_defs) / sizeof(s_defs[0]))

static int svc_index(app_service_id_t id)
{
    for (int i = 0; i < SVC_COUNT; i++) {
        if (s_defs[i].id == id)
            return i;
    }
    return -1;
}

static void svc_stop(int i)
{
    s_state[i].owner_kind = SVC_OWNER_NONE;
    s_state[i].owner_page = UI_PAGE_CHAT; /* unused; keep deterministic */
    s_state[i].sticky = false;
    if (s_defs[i].is_running() || (i == 0 && ap_transfer_is_running()) || (i == 1 && lan_http_is_running())) {
        ap_transfer_server_stop(&s_transfer_server); /* clears whichever mode runs */
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void app_page_runtime_init(void)
{
    memset(s_state, 0, sizeof(s_state));
    ESP_LOGI(TAG, "Service registry initialised (%d services)", SVC_COUNT);
}

bool app_page_runtime_service_acquire(app_service_id_t id, svc_owner_t owner, ui_page_id_t page)
{
    int i = svc_index(id);
    if (i < 0 || owner == SVC_OWNER_NONE)
        return false;
    /* Exclusive modes of one server: stop the sibling first. */
    for (int j = 0; j < SVC_COUNT; j++) {
        if (j != i && s_defs[j].is_running()) {
            ESP_LOGI(TAG, "acquire(%s): stopping sibling %s first", s_defs[i].name, s_defs[j].name);
            svc_stop(j);
        }
    }
    if (!s_defs[i].is_running()) {
        if (!s_defs[i].start()) {
            ESP_LOGW(TAG, "acquire(%s): start failed", s_defs[i].name);
            return false;
        }
    } else if (s_state[i].owner_kind == SVC_OWNER_PAGE && !s_state[i].sticky && s_state[i].owner_page == page) {
        return true; /* already owned by this page */
    }
    s_state[i].owner_kind = owner;
    s_state[i].owner_page = (owner == SVC_OWNER_PAGE) ? page : UI_PAGE_CHAT;
    s_state[i].sticky = false;
    ESP_LOGI(TAG, "acquire(%s, owner=%s, page=%d)", s_defs[i].name,
             owner == SVC_OWNER_PAGE ? "PAGE" : "USER", (int)page);
    return true;
}

void app_page_runtime_service_release_user(app_service_id_t id)
{
    int i = svc_index(id);
    if (i < 0)
        return;
    if (s_state[i].owner_kind == SVC_OWNER_USER || s_defs[i].is_running()) {
        ESP_LOGI(TAG, "release_user(%s)", s_defs[i].name);
        svc_stop(i);
    }
}

void app_page_runtime_service_force_stop(app_service_id_t id)
{
    int i = svc_index(id);
    if (i < 0)
        return;
    ESP_LOGI(TAG, "force_stop(%s)", s_defs[i].name);
    svc_stop(i);
}

void app_page_runtime_service_release_all(void)
{
    for (int i = 0; i < SVC_COUNT; i++) {
        if (s_state[i].owner_kind != SVC_OWNER_NONE || s_defs[i].is_running()) {
            ESP_LOGI(TAG, "release_all(%s)", s_defs[i].name);
            svc_stop(i);
        }
    }
}

bool app_page_runtime_service_any_running(void)
{
    for (int i = 0; i < SVC_COUNT; i++) {
        if (s_defs[i].is_running() || s_state[i].owner_kind != SVC_OWNER_NONE)
            return true;
    }
    return false;
}

void app_page_runtime_service_set_sticky(app_service_id_t id, bool sticky)
{
    int i = svc_index(id);
    if (i < 0)
        return;
    if (s_state[i].sticky != sticky) {
        ESP_LOGI(TAG, "sticky(%s) = %d", s_defs[i].name, sticky ? 1 : 0);
    }
    s_state[i].sticky = sticky;
}

bool app_page_runtime_service_is_sticky(app_service_id_t id)
{
    int i = svc_index(id);
    return i >= 0 && s_state[i].sticky;
}

void app_page_runtime_on_page_switched(ui_page_id_t from, ui_page_id_t to)
{
    /* Freeze semantics: release PAGE-owned services of the leaving page
     * (sticky ones survive — D5 transfer-in-progress). */
    for (int i = 0; i < SVC_COUNT; i++) {
        if (s_state[i].owner_kind == SVC_OWNER_PAGE && s_state[i].owner_page == from) {
            if (s_state[i].sticky) {
                ESP_LOGI(TAG, "page %d left; %s sticky — kept running", (int)from, s_defs[i].name);
            } else {
                ESP_LOGI(TAG, "page %d left; stopping %s", (int)from, s_defs[i].name);
                svc_stop(i);
            }
        }
    }
    /* Auto-start services declared by the entering page's policy. */
    const page_runtime_policy_t *pol = page_runtime_policy(to);
    if (pol->services & APP_SVC_AP_TRANSFER) {
        if (!ap_transfer_is_running()) {
            ESP_LOGI(TAG, "page %d entered; auto-starting ap_transfer", (int)to);
            app_page_runtime_service_acquire(APP_SVC_AP_TRANSFER, SVC_OWNER_PAGE, to);
        } else {
            /* Already running (e.g. started by the WiFi-page long-press just
             * before switching): adopt it as this page's own. */
            s_state[svc_index(APP_SVC_AP_TRANSFER)].owner_kind = SVC_OWNER_PAGE;
            s_state[svc_index(APP_SVC_AP_TRANSFER)].owner_page = to;
        }
    }
}
