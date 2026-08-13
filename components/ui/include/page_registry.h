#ifndef COMPONENTS_UI_INCLUDE_PAGE_REGISTRY_H_
#define COMPONENTS_UI_INCLUDE_PAGE_REGISTRY_H_

#include <stdbool.h>
#ifdef ESP_PLATFORM
#    include "esp_attr.h"
#else
#    ifndef EXT_RAM_BSS_ATTR
#        define EXT_RAM_BSS_ATTR
#    endif
#endif
#include "page_renderer.h"
#include "ui_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Page registry — GCC constructor auto-registration.
 *
 * Each page .c ends with PAGE_REGISTER(...), which emits a constructor
 * that appends the page's metadata to the registry at startup.
 *
 * Why this works despite --gc-sections / archive extraction: the ui
 * component links itself with -Wl,--whole-archive (see CMakeLists.txt),
 * which forces the linker to extract every object file in libui.a. Every
 * constructor's .ctors entry is therefore always linked, and the linker
 * script's KEEP(... .ctors.*) protects it from section GC. Without
 * --whole-archive, pages with no external reference are never extracted
 * from the archive and their constructors silently never run.
 *
 * Adding a new page:
 *   1. Add enum value to ui_page_id_t (ui_manager.h)
 *   2. Write the page .c/.h; end the .c with PAGE_REGISTER(...)
 *   3. Add the .c to CMakeLists.txt SRCS
 */

typedef struct {
    ui_page_id_t id;
    const char *name; /* localized display name ("相册") */
    const char *icon; /* FA icon string or NULL */
    bool show_in_quick_switch;
    int order; /* quick-switch sort key: smaller first */
    const page_renderer_ops_t *ops;
    page_renderer_t *instance; /* pointer to the page's instance */
} page_entry_t;

/* Internal: called by PAGE_REGISTER constructors. */
void page_registry_add(const page_entry_t *entry);

/*
 * Emit a constructor that registers the page. Used once per page .c.
 * The constructor runs before main(), populating the internal array.
 */
#define PAGE_REGISTER(id, name, icon, quick, order_val, ops_ptr, inst_ptr)                                             \
    static const page_entry_t _page_entry_##id = {(id), (name), (icon), (quick), (order_val), (ops_ptr), (inst_ptr)};  \
    void __attribute__((constructor(200))) _page_register_##id(void)                                                   \
    {                                                                                                                  \
        page_registry_add(&_page_entry_##id);                                                                          \
    }

/* --- Public API ------------------------------------------------------- */

/*
 * Build the quick-switch index from registered pages (sorted by order).
 * Called once from ui_manager_init().
 */
void page_registry_init(void);

/* O(1) lookup by page id. Returns NULL for unregistered ids. */
const page_entry_t *page_registry_get_entry(ui_page_id_t id);

/* Convenience: returns the page_renderer_t* for the given id. */
page_renderer_t *page_registry_get_instance(ui_page_id_t id);

/* Convenience: returns the display name for the given id. */
const char *page_registry_get_name(ui_page_id_t id);

/*
 * Fill `out` with pointers to entries visible in the quick-switch
 * overlay, sorted by order. Returns the count.
 */
int page_registry_quick_switch_items(const page_entry_t **out, int max);

/* Total number of registered pages (for diagnostics). */
int page_registry_count(void);

#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_UI_INCLUDE_PAGE_REGISTRY_H_ */
