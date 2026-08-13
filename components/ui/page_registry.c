/**
 * @file page_registry.c
 * @brief Page registration via GCC constructor functions.
 *
 * PAGE_REGISTER() constructors run before main() and append each page's
 * entry to s_entries[]. page_registry_init() builds the quick-switch index.
 *
 * The ui component links with -Wl,--whole-archive (CMakeLists.txt) so the
 * linker extracts every page object from libui.a — without it, pages that
 * have no external reference are dropped and their constructors never run.
 */
#include "page_registry.h"
#include <string.h>
#ifdef ESP_PLATFORM
#    include <esp_log.h>
#else
#    include <stdio.h>
#    define ESP_LOGI(tag, fmt, ...) printf("[%s][I] " fmt "\n", tag, ##__VA_ARGS__)
#    define ESP_LOGW(tag, fmt, ...) printf("[%s][W] " fmt "\n", tag, ##__VA_ARGS__)
#    define ESP_LOGE(tag, fmt, ...) printf("[%s][E] " fmt "\n", tag, ##__VA_ARGS__)
#endif

#define TAG "PageRegistry"

#define MAX_PAGES UI_PAGE_COUNT

static page_entry_t s_entries[MAX_PAGES];
static const page_entry_t *s_quick_switch[MAX_PAGES];
static int s_quick_switch_count;
static int s_total;
static bool s_finalized;

/* Called by PAGE_REGISTER constructors (before main). */
void page_registry_add(const page_entry_t *entry)
{
    if (!entry || entry->id < 0 || entry->id >= MAX_PAGES)
        return;
    s_entries[entry->id] = *entry;
    s_total++;
    if (entry->instance && entry->ops) {
        entry->instance->ops = entry->ops;
    }
}

void page_registry_init(void)
{
    /* Constructors have already run (before main). Collect quick-switch
     * entries, then insertion-sort by order (smaller = earlier). */
    s_quick_switch_count = 0;
    ESP_LOGI(TAG, "Page registry: %d entries in section", s_total);
    for (int i = 0; i < MAX_PAGES; i++) {
        if (s_entries[i].ops) {
            ESP_LOGI(TAG, "  registered id=%d name=\"%s\" quick=%d order=%d", i,
                     s_entries[i].name ? s_entries[i].name : "(null)", s_entries[i].show_in_quick_switch ? 1 : 0,
                     s_entries[i].order);
            if (s_entries[i].show_in_quick_switch && s_quick_switch_count < MAX_PAGES) {
                s_quick_switch[s_quick_switch_count++] = &s_entries[i];
            }
        }
    }
    for (int i = 1; i < s_quick_switch_count; i++) {
        const page_entry_t *key = s_quick_switch[i];
        int j = i - 1;
        while (j >= 0 && s_quick_switch[j]->order > key->order) {
            s_quick_switch[j + 1] = s_quick_switch[j];
            j--;
        }
        s_quick_switch[j + 1] = key;
    }
    ESP_LOGI(TAG, "Quick-switch list (%d items):", s_quick_switch_count);
    for (int i = 0; i < s_quick_switch_count; i++) {
        ESP_LOGI(TAG, "  [%d] %s (order=%d)", i, s_quick_switch[i]->name ? s_quick_switch[i]->name : "(null)",
                 s_quick_switch[i]->order);
    }
    s_finalized = true;
}

const page_entry_t *page_registry_get_entry(ui_page_id_t id)
{
    if (id >= 0 && id < MAX_PAGES) {
        const page_entry_t *e = &s_entries[id];
        if (e->ops)
            return e;
    }
    return NULL;
}

page_renderer_t *page_registry_get_instance(ui_page_id_t id)
{
    const page_entry_t *e = page_registry_get_entry(id);
    return e ? e->instance : NULL;
}

const char *page_registry_get_name(ui_page_id_t id)
{
    const page_entry_t *e = page_registry_get_entry(id);
    return e ? e->name : "未知";
}

int page_registry_quick_switch_items(const page_entry_t **out, int max)
{
    int n = (s_quick_switch_count < max) ? s_quick_switch_count : max;
    for (int i = 0; i < n; i++) {
        out[i] = s_quick_switch[i];
    }
    return n;
}

int page_registry_count(void)
{
    return s_total;
}
