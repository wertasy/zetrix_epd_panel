/**
 * @file app_page_runtime.h
 * @brief Application service registry — ownership state machine for
 *        background services (LAN HTTP, AP transfer).
 *
 * Plan: docs/page-runtime-framework-plan.md §3.7 (P3), D3, D5.
 *
 * Ownership model (NOT refcount — at most one PAGE owner + one USER flag
 * per service, so refcounting adds failure modes with zero benefit):
 *   - SVC_OWNER_PAGE: bound to a page; released automatically when the page
 *     leaves the foreground (unless "sticky" is set — D5 transfer-in-progress
 *     upgrade that keeps the upload alive across page switches).
 *   - SVC_OWNER_USER: user-initiated (settings toggle / gallery long-press);
 *     survives page switches until the user turns it off, the idle timeout
 *     fires (D3, LAN HTTP only) or everything is torn down for sleep.
 *
 * All entry points are called from the application main task only
 * (event queue consumer + 1s periodic loop), except the sticky setter,
 * which is driven by ap_server_state_cb — itself dispatched on the main
 * task via the async event queue.
 */
#ifndef MAIN_APP_PAGE_RUNTIME_H_
#define MAIN_APP_PAGE_RUNTIME_H_

#include <stdbool.h>
#include <stdint.h>

#include "ui_manager.h"
#include "page_runtime.h"

/* Service ids alias the canonical PAGE_SVC_* bits defined in the ui
 * component so page policies (flash constants) can reference them. */
typedef enum {
    APP_SVC_NONE = PAGE_SVC_NONE,
    APP_SVC_LAN_HTTP = PAGE_SVC_LAN_HTTP,       /* ap_transfer_server, LAN mode */
    APP_SVC_AP_TRANSFER = PAGE_SVC_AP_TRANSFER, /* ap_transfer_server, AP mode  */
} app_service_id_t;

typedef enum {
    SVC_OWNER_NONE = 0,
    SVC_OWNER_PAGE, /* owner_page is valid while this is set */
    SVC_OWNER_USER,
} svc_owner_t;

/* Registry lifecycle (call once from application_init, after
 * ap_transfer_server_init and before any acquire/release). */
void app_page_runtime_init(void);

/* Page-switch glue: releases PAGE-owned services of `from` (sticky ones
 * survive) and auto-starts services declared by `to`'s runtime policy. */
void app_page_runtime_on_page_switched(ui_page_id_t from, ui_page_id_t to);

bool app_page_runtime_service_acquire(app_service_id_t id, svc_owner_t owner, ui_page_id_t page);
void app_page_runtime_service_release_user(app_service_id_t id);
/* Explicit stop regardless of owner/sticky (BOOT long-press exit, WiFi off). */
void app_page_runtime_service_force_stop(app_service_id_t id);
/* Stop everything and reset registry state (manual sleep path). */
void app_page_runtime_service_release_all(void);
bool app_page_runtime_service_any_running(void);

/* D5 sticky upgrade: while set, release-by-page skips this service
 * (transfer in progress). Cleared on completion/failure/stop. */
void app_page_runtime_service_set_sticky(app_service_id_t id, bool sticky);
bool app_page_runtime_service_is_sticky(app_service_id_t id);

#endif /* MAIN_APP_PAGE_RUNTIME_H_ */
