/**
 * @file data_refresh.h
 * @brief Decoupled data-refresh request channel for page renderers.
 *
 * Pages call data_refresh_request(page) instead of directly invoking network
 * API fetch functions. The orchestrator (application.c) registers a callback
 * via data_refresh_set_callback() and triggers the appropriate data sync.
 */
#ifndef COMPONENTS_UI_INCLUDE_DATA_REFRESH_H_
#define COMPONENTS_UI_INCLUDE_DATA_REFRESH_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Page ID — same values as ui_page_id_t (UI_PAGE_WEATHER, UI_PAGE_CODING_PLAN, etc.)
 * Declared as int to avoid pulling ui_manager.h into every page renderer. */
typedef int data_refresh_page_t;

typedef void (*data_refresh_cb_t)(data_refresh_page_t page, void *ctx);

/* Register the global data-refresh callback (called once by application.c). */
void data_refresh_set_callback(data_refresh_cb_t cb, void *ctx);

/* Called by page renderers to request a data refresh for their page. */
void data_refresh_request(data_refresh_page_t page);

#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_UI_INCLUDE_DATA_REFRESH_H_ */
