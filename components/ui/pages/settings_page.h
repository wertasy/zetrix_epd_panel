/**
 * @file settings_page.h
 * @brief Settings page renderer — C port of C++ rawdraw::SettingsRenderer.
 *
 * Card-based settings page with a left category sidebar (sections) and a
 * right option pane (Normal / Checkbox / Action rows). Multiple modal
 * dialogs overlay the base page: about, volume, storage, server address,
 * server address history list, theme picker, and OTA update (+ confirm).
 *
 * Implementation is split across four translation units that share the
 * settings_page_t state through this single header:
 *   - settings_page.c    (core: init / render / input / items / layout)
 *   - settings_dialogs.c (volume / storage / server / server list / OTA)
 *   - settings_about.c   (about dialog)
 *   - settings_themes.c  (theme selection data table + theme dialog)
 */
#ifndef COMPONENTS_UI_PAGES_SETTINGS_PAGE_H_
#define COMPONENTS_UI_PAGES_SETTINGS_PAGE_H_

#include "page_renderer.h"
#include "theme.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SETTINGS_PAGE_MAX_ITEMS 32 /* max option rows (incl. sections) */
#define SETTINGS_PAGE_MAX_SERVER_ADDRS 10 /* history addresses in server list */
#define SETTINGS_PAGE_MAX_OTA_VERSIONS 8 /* OTA firmware versions */
#define SETTINGS_PAGE_ADDR_LEN 64
#define SETTINGS_PAGE_VERSION_LEN 48
#define SETTINGS_PAGE_ITEM_LABEL_LEN 32
#define SETTINGS_PAGE_ITEM_VALUE_LEN 64
#define SETTINGS_PAGE_OTA_FIRMWARE_LEN 48
#define SETTINGS_PAGE_OTA_STATUS_LEN 64

typedef enum {
    SETTINGS_ITEM_NORMAL = 0, /* navigable row with right-aligned value */
    SETTINGS_ITEM_CHECKBOX, /* ON/OFF switch row */
    SETTINGS_ITEM_ACTION, /* highlighted action row */
    SETTINGS_ITEM_SECTION, /* sidebar category header */
} settings_page_item_type_t;

typedef struct settings_page settings_page_t;

/**
 * @brief One settings option row (C port of C++ SettingsItemDef).
 *
 * icon points to a static icon glyph string (fa_settings.h constants);
 * strings label/value are stored inline. on_click is the item's action
 * callback (C++ std::function<void()> -> fn pointer + ctx).
 */
typedef struct {
    char                      label[SETTINGS_PAGE_ITEM_LABEL_LEN];
    char                      value[SETTINGS_PAGE_ITEM_VALUE_LEN];
    const char               *icon;
    settings_page_item_type_t type;
    bool                      checked;
    void (*on_click)(void *ctx);
    void *on_click_ctx;
} settings_page_item_t;

/* Dialog handler typedefs (C++ std::function -> fn pointer + void* ctx). */
typedef void (*settings_page_volume_handler_t)(int volume, bool commit, void *ctx);
typedef void (*settings_page_server_handler_t)(int selection, void *ctx);
typedef void (*settings_page_server_list_handler_t)(const char *address, void *ctx);
typedef void (*settings_page_theme_handler_t)(rawdraw_theme_id_t id, void *ctx);
typedef void (*settings_page_ota_handler_t)(int delta, bool commit, bool cancel, void *ctx);

struct settings_page {
    page_renderer_t base;

    /* Declarative menu model. */
    settings_page_item_t items[SETTINGS_PAGE_MAX_ITEMS];
    int                  item_count;
    int                  selected_index;
    int                  scroll_offset; /* preserved for compatibility; item-window scrolling is primary */
    int                  first_visible_index;

    /* Debug / category hint state. */
    bool    showing_debug_info;
    int64_t debug_hint_until_us;
    int64_t category_hint_until_us;
    char    firmware_version[SETTINGS_PAGE_VERSION_LEN];
    char    mac_address[32];
    char    chip_model[32];

    /* About dialog state. */
    bool showing_about_dialog;

    /* Volume dialog state. */
    bool                           showing_volume_dialog;
    int                            volume_dialog_value;
    settings_page_volume_handler_t volume_dialog_handler;
    void                          *volume_dialog_ctx;

    /* Storage dialog state. */
    bool showing_storage_dialog;
    char storage_used[32];
    char storage_total[32];
    int  storage_photos;
    int  storage_txts;

    /* Server address dialog state. */
    bool                           showing_server_dialog;
    char                           server_current_addr[SETTINGS_PAGE_ADDR_LEN];
    char                           server_local_addr[SETTINGS_PAGE_ADDR_LEN];
    char                           server_remote_addr[SETTINGS_PAGE_ADDR_LEN];
    int                            server_selected; /* 0=local, 1=remote */
    settings_page_server_handler_t server_dialog_handler;
    void                          *server_dialog_ctx;

    /* Server address list dialog state. */
    bool                                showing_server_list_dialog;
    char                                server_list_addresses[SETTINGS_PAGE_MAX_SERVER_ADDRS][SETTINGS_PAGE_ADDR_LEN];
    int                                 server_list_count;
    char                                server_list_current[SETTINGS_PAGE_ADDR_LEN];
    int                                 server_list_selected;
    int                                 server_list_scroll_offset;
    settings_page_server_list_handler_t server_list_dialog_handler;
    void                               *server_list_dialog_ctx;

    /* Theme picker dialog state. */
    bool                          showing_theme_dialog;
    int                           theme_selected;
    settings_page_theme_handler_t theme_dialog_handler;
    void                         *theme_dialog_ctx;

    /* OTA update dialog state. */
    bool                        showing_ota_dialog;
    char                        ota_versions[SETTINGS_PAGE_MAX_OTA_VERSIONS][SETTINGS_PAGE_OTA_FIRMWARE_LEN];
    int                         ota_version_count;
    char                        ota_current_version[SETTINGS_PAGE_OTA_FIRMWARE_LEN];
    int                         ota_selected_index;
    int                         ota_progress_percent;
    char                        ota_status_text[SETTINGS_PAGE_OTA_STATUS_LEN];
    int                         ota_state; /* 2=selecting, 4/5/6=downloading, 7=failed */
    settings_page_ota_handler_t ota_dialog_handler;
    void                       *ota_dialog_ctx;

    /* OTA confirm dialog state. */
    bool showing_ota_confirm_dialog;
    int  ota_confirm_selected; /* 0=confirm update, 1=cancel */
    char ota_confirm_firmware_name[SETTINGS_PAGE_OTA_FIRMWARE_LEN];

    /* Fonts. */
    const lv_font_t *font; /* body text */
    const lv_font_t *title_font; /* titles / emphasized text */
    const lv_font_t *icon_font; /* fa_settings_16 vector icons */
    const lv_font_t *value_font; /* row values / small text */
};

/* PageRenderer vtable entry points. */
void settings_page_init(page_renderer_t *self, int width, int height);
void settings_page_render(page_renderer_t *self, uint8_t *fb, int width, int height);
bool settings_page_handle_input(page_renderer_t *self, const ui_button_event_t *event);

/* Declarative menu data interface. */
void settings_page_set_items(page_renderer_t *self, const settings_page_item_t *items, int count);
void settings_page_update_item(page_renderer_t *self, int index, const char *value);
void settings_page_update_checked(page_renderer_t *self, int index, bool checked);
int  settings_page_get_item_count(const page_renderer_t *self);
int  settings_page_get_selected_index(const page_renderer_t *self);

/* Debug info + device identity. */
void settings_page_show_debug_info(page_renderer_t *self);
void settings_page_set_firmware_version(page_renderer_t *self, const char *version);
void settings_page_set_device_info(page_renderer_t *self, const char *mac, const char *chip);

/* Volume dialog. */
void settings_page_show_volume_dialog(page_renderer_t *self, int volume);
void settings_page_set_volume_dialog_handler(page_renderer_t *self, settings_page_volume_handler_t handler, void *ctx);

/* Category hint (transient bottom hint; visible while timer active). */
void settings_page_show_category_hint(page_renderer_t *self, int duration_ms);
bool settings_page_is_category_hint_visible(const page_renderer_t *self);

/* About dialog. */
void settings_page_show_about_dialog(page_renderer_t *self);
void settings_page_hide_about_dialog(page_renderer_t *self);
bool settings_page_is_about_dialog_showing(const page_renderer_t *self);

/* Storage dialog. */
void settings_page_show_storage_dialog(page_renderer_t *self, const char *used, const char *total, int photos,
                                       int txts);
void settings_page_hide_storage_dialog(page_renderer_t *self);
bool settings_page_is_storage_dialog_showing(const page_renderer_t *self);

/* Server address dialog. */
void settings_page_show_server_dialog(page_renderer_t *self, const char *current_addr, const char *local_addr,
                                      const char *remote_addr);
void settings_page_hide_server_dialog(page_renderer_t *self);
bool settings_page_is_server_dialog_showing(const page_renderer_t *self);
int  settings_page_get_server_dialog_selection(const page_renderer_t *self);
void settings_page_set_server_dialog_handler(page_renderer_t *self, settings_page_server_handler_t handler, void *ctx);

/* Server address list dialog (history). */
void        settings_page_show_server_list_dialog(page_renderer_t *self, const char *const *addresses, int count,
                                                  const char *current_addr);
void        settings_page_hide_server_list_dialog(page_renderer_t *self);
bool        settings_page_is_server_list_dialog_showing(const page_renderer_t *self);
const char *settings_page_get_server_list_selection(const page_renderer_t *self);
void settings_page_set_server_list_dialog_handler(page_renderer_t *self, settings_page_server_list_handler_t handler,
                                                  void *ctx);

/* Theme picker dialog. */
void settings_page_show_theme_dialog(page_renderer_t *self, rawdraw_theme_id_t current_theme);
void settings_page_hide_theme_dialog(page_renderer_t *self);
bool settings_page_is_theme_dialog_showing(const page_renderer_t *self);
void settings_page_set_theme_dialog_handler(page_renderer_t *self, settings_page_theme_handler_t handler, void *ctx);

/* OTA update dialog (+ nested confirm dialog). */
void settings_page_show_ota_dialog(page_renderer_t *self, const char *const *versions, int version_count,
                                   const char *current_version, int selected_index, int progress_percent,
                                   const char *status_text, int state);
void settings_page_hide_ota_dialog(page_renderer_t *self);
bool settings_page_is_ota_dialog_showing(const page_renderer_t *self);
void settings_page_set_ota_dialog_handler(page_renderer_t *self, settings_page_ota_handler_t handler, void *ctx);

/* ------------------------------------------------------------------ */
/* Internal renderers shared across the settings TUs.                  */
/* ------------------------------------------------------------------ */

void settings_page_render_about_dialog(settings_page_t *self, uint8_t *fb, int width, int height);
void settings_page_render_volume_dialog(settings_page_t *self, uint8_t *fb, int width, int height);
void settings_page_render_storage_dialog(settings_page_t *self, uint8_t *fb, int width, int height);
void settings_page_render_server_dialog(settings_page_t *self, uint8_t *fb, int width, int height);
void settings_page_render_server_list_dialog(settings_page_t *self, uint8_t *fb, int width, int height);
void settings_page_render_theme_dialog(settings_page_t *self, uint8_t *fb, int width, int height);
void settings_page_render_ota_dialog(settings_page_t *self, uint8_t *fb, int width, int height);
void settings_page_render_ota_confirm_dialog(settings_page_t *self, uint8_t *fb, int width, int height);

/* Shared helpers (implemented in settings_page.c, used by dialog TUs). */
rawdraw_color_t settings_page_token_ink_on_paper(rawdraw_theme_token_t token);
void settings_page_draw_vector_icon(uint8_t *fb, int width, int height, const char *label, int x, int center_y,
                                    rawdraw_color_t color);
void settings_page_clear_dialog_region(uint8_t *fb, int width, int height, int x, int y, int w, int h, int radius,
                                       int pad);

/* ------------------------------------------------------------------ */
/* Theme selection data table (settings_themes.c).                     */
/* ------------------------------------------------------------------ */

typedef struct {
    rawdraw_theme_id_t id;
    const char        *name; /* display name */
} settings_theme_entry_t;

int                           settings_page_theme_count(void);
const settings_theme_entry_t *settings_page_theme_at(int index);

#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_UI_PAGES_SETTINGS_PAGE_H_ */
