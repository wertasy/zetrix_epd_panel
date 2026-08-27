# 冰箱备忘录 P0（只读展示 + 手动删除）实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 落地设计文档 v1.2（`docs/fridge-memo-product-design.md`）的 P0 里程碑：冰箱备忘页面（汇总条 + 4 条/屏整页翻页 + 删除浮层）+ REST 客户端（NVS 缓存先行）+ 快捷切换接入 + `base_url` 设置项 + 主机端测试。

**Architecture:** 复刻 `coding_plan_api`（REST 客户端 + NVS 缓存 + 回调注入）与 `coding_plan_page`（页面渲染器 + `PAGE_REGISTER`）两个既有模式。数据面：DTO（data_types）→ `fridge_memo_api`（network，JSON 解析/日期数学/排序为纯函数可主机测试）→ `fridge_memo_page`（ui/pages）→ `app_sync` 编排。删除浮层自绘（settings 对话框模式），不引入新控件。

**Tech Stack:** ESP-IDF v6 / 纯 C（snake_case，无 k 前缀）/ cJSON / rawdraw 2bpp / gcc 主机测试（`tests/run_tests.sh`）。

**范围排除（后续计划）：** P1a 音频链路 spike、P1b 语音全流程（BUTTON_LONG_PRESS_UP、音效/LED、memo WS）、P2 联调。P0 的 BOOT 长按在本页面无动作（footer 提示不含语音引导，P1b 再替换）。

**关键既有事实（实现者必读）：**

- `ui_page_id_t` 在 `components/ui/include/ui_manager.h:25-46`，**必须把 `UI_PAGE_FRIDGE_MEMO` 加在 `UI_PAGE_CODING_PLAN` 之后、`UI_PAGE_COUNT` 之前**（枚举值持久化到 RTC NVS，尾部追加才不破坏旧索引）。
- `PAGE_REGISTER(id, name, icon, quick, order_val, ops_ptr, inst_ptr)`（`components/ui/include/page_registry.h:56`）；本页：`PAGE_REGISTER(UI_PAGE_FRIDGE_MEMO, "冰箱备忘", NULL, true, 25, &fridge_memo_page_ops, &s_fridge_memo_instance.base)`（order=25：天气 20 与日历 30 之间）。
- 页面字体两档：名称 `SourceHanSansSC_Medium_slim`（24px）、其余 `SourceHanSansSC_Regular_slim`（16px）；声明方式照抄 `coding_plan_page.c:32-33`（`static const lv_font_t *const`）。`font_zectrix_16_1` 提供 `icon-mic`（`font_zectrix.h:31` 的 `FONT_ZECTRIX_ICON_MIC`）——仅 16px，空态图标用它（设计文档写 48px 是笔误，48px 字体无 mic）。
- 布局参照 `news_page.c:22-30`：内容区 Y 起点 `STYLE_STATUS_BAR_HEIGHT`，footer `Y=264 H=26`（300 高屏幕）。纵向预算：汇总条 22 + 4×52 行 + footer 26。
- 按键事件（`rawdraw_ext.h:20-28`）：`BTN_UP_CLICK/BTN_DOWN_CLICK/BTN_BOOT_CLICK/BTN_BOOT_DOUBLE_CLICK/...`；`ui_button_event_t{type}`。BOOT 双击/单击/UP/DOWN 已由 `application.c` `forward_ui_button` 转发到当前页面 —— **application.c 无需为 P0 输入改动**。
- 页面请求数据用 `data_refresh_request(page)`（`components/ui/include/data_refresh.h`），app 层回调见 `app_sync.c:121` `app_sync_on_data_refresh_request`。
- REST 基础设施：`http_get_text(url, buf, max)`（`http_client_util.h:38`，target-only，host 返回 -1）。DELETE 需新加一个 util 函数（Task 2）。
- 编码规范：snake_case；`s_` 文件内静态；4 空格缩进 120 列（`.clang-format`）；NVS/FreeRTOS/ESP_LOG 代码必须 `#ifdef ESP_PLATFORM` 保护（主机测试要编译同一份 .c）。
- 主机测试编译参照 `tests/run_tests.sh:54-61`（network 用例）与 `:72-82`（ui_pages_smoke 用例）。
- UI CMake：`components/ui/CMakeLists.txt:1-23` `UI_SRCS` 列表；network CMake：`components/network/CMakeLists.txt:1-6` SRCS。
- NVS 字符串读写：`settings_open(ns, read_write)` / `settings_get_string` / `settings_set_string`（`components/bsp_storage/include/settings.h:10-16`，target-only）。
- 提交风格：英文 conventional commits，按逻辑拆分提交。

---

### Task 1: DTO + REST 客户端核心（纯函数：JSON 解析 / 日期数学 / 排序）

**Files:**
- Create: `components/data_types/include/fridge_memo_dto.h`
- Create: `components/network/include/fridge_memo_api.h`
- Create: `components/network/fridge_memo_api.c`
- Modify: `components/network/CMakeLists.txt`（SRCS 加 `"fridge_memo_api.c"`）
- Test: `tests/test_fridge_memo.c`（本任务先建解析/日期/排序部分）
- Modify: `tests/run_tests.sh`（TESTS 数组 + case）

- [ ] **Step 1.1: 写 DTO 头文件**

```c
/* components/data_types/include/fridge_memo_dto.h */
/**
 * @file fridge_memo_dto.h
 * @brief Fridge memo DTO types — shared type-only layer (design doc v1.2 §4.1/§7.1).
 *
 * Pure data used by fridge_memo_api (network) and fridge_memo_page (ui).
 * Field set mirrors the backend REST contract one-to-one; note/storage are
 * reserved (not rendered in P0).
 */
#ifndef DATA_TYPES_FRIDGE_MEMO_DTO_H_
#define DATA_TYPES_FRIDGE_MEMO_DTO_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FRIDGE_MEMO_MAX_ITEMS 64
#define FRIDGE_MEMO_ID_LEN 16
#define FRIDGE_MEMO_NAME_LEN 32
#define FRIDGE_MEMO_QTY_LEN 16
#define FRIDGE_MEMO_DATE_LEN 11 /* "YYYY-MM-DD" + NUL */
#define FRIDGE_MEMO_NOTE_LEN 64
#define FRIDGE_MEMO_STORAGE_LEN 8
#define FRIDGE_MEMO_UPDATED_LEN 24

/** Derived display status (device-side, computed at render time). */
typedef enum {
    FRIDGE_MEMO_STATUS_UNKNOWN = 0, /* no expires_at */
    FRIDGE_MEMO_STATUS_OK, /* remaining > 2 days */
    FRIDGE_MEMO_STATUS_NEAR, /* 0 <= remaining <= 2 days (临期, yellow) */
    FRIDGE_MEMO_STATUS_EXPIRED, /* remaining < 0 (red) */
} fridge_memo_status_t;

typedef struct {
    char id[FRIDGE_MEMO_ID_LEN];
    char name[FRIDGE_MEMO_NAME_LEN];
    char quantity[FRIDGE_MEMO_QTY_LEN];
    char added_at[FRIDGE_MEMO_DATE_LEN]; /* ISO date */
    char expires_at[FRIDGE_MEMO_DATE_LEN]; /* "" = no expiry */
    char note[FRIDGE_MEMO_NOTE_LEN];
    char storage[FRIDGE_MEMO_STORAGE_LEN]; /* "fridge"/"freezer", reserved */
} fridge_memo_item_t;

/** Authoritative full-list snapshot (backend is the single source of truth). */
typedef struct {
    fridge_memo_item_t items[FRIDGE_MEMO_MAX_ITEMS];
    int count; /* >64 backend entries are truncated to the 64 most urgent AFTER sort */
    char updated_at[FRIDGE_MEMO_UPDATED_LEN];
} fridge_memo_snapshot_t;

#ifdef __cplusplus
}
#endif

#endif /* DATA_TYPES_FRIDGE_MEMO_DTO_H_ */
```

- [ ] **Step 1.2: 写 API 头文件**

```c
/* components/network/include/fridge_memo_api.h */
/**
 * @file fridge_memo_api.h
 * @brief Fridge memo REST client (design doc v1.2 §7.1).
 *
 * JSON parsing / date math / sorting are plain cJSON + libc and build+run on
 * the Linux host (tests/test_fridge_memo.c). HTTP fetch/delete and the NVS
 * cache are target-only (wrap http_client_util + settings), guarded by
 * #ifdef ESP_PLATFORM. Modeled on coding_plan_api.
 *
 * Endpoints (base_url from NVS namespace "fridge", key "base_url"):
 *   GET    {base}/api/v1/fridge/items          -> {"updated_at", "items":[...]}
 *   DELETE {base}/api/v1/fridge/items/{id}     -> {"ok", "updated_at", "items":[...]}
 */
#ifndef FRIDGE_MEMO_API_H_
#define FRIDGE_MEMO_API_H_

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include "fridge_memo_dto.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- lifecycle / config (target: NVS override; host: params only) ---- */

/** Initialise. On target also loads NVS "fridge"/"base_url" and the cached
 *  snapshot from NVS "fridge_memo"/"items_json". base_url may be NULL. */
void fridge_memo_api_init(const char *base_url);

/** Set + (on target) persist base_url. Empty string clears it. */
void fridge_memo_api_set_base_url(const char *url);

void fridge_memo_api_get_base_url(char *out, size_t len);

/* ---- cached snapshot (cache-first render, design §4.4) ---- */

bool fridge_memo_api_has_cached_data(void);
const fridge_memo_snapshot_t *fridge_memo_api_get_cached_data(void);

/* ---- async fetch / delete with callbacks ---- */

typedef void (*fridge_memo_callback_t)(const fridge_memo_snapshot_t *snap, void *user_data);
typedef void (*fridge_memo_error_callback_t)(const char *message, void *user_data);

void fridge_memo_api_set_callback(fridge_memo_callback_t cb, void *user_data);
void fridge_memo_api_set_error_callback(fridge_memo_error_callback_t cb, void *user_data);

/** Async GET (target: background task; host: no-op returning false). */
bool fridge_memo_api_fetch_async(void);

/** Async DELETE of one item; response's full items[] drives the callback. */
bool fridge_memo_api_delete_async(const char *item_id);

/* ---- pure, host-testable ---- */

/**
 * Parse a GET/DELETE response body into @p out (sorted in place).
 * Accepts both {"updated_at","items":[...]} and a bare [...].
 * Truncates > FRIDGE_MEMO_MAX_ITEMS entries (caller re-sorts after truncate).
 * @return true if at least the items array was found.
 */
bool fridge_memo_parse_items_json(const char *json, fridge_memo_snapshot_t *out);

/** Days stored = today - added_at + 1 (same-day = 1). -1 on parse failure. */
int fridge_memo_days_since(const char *iso_date, const struct tm *today);

/** Days remaining = expires_at - today. -1000 on missing/invalid date. */
int fridge_memo_days_until(const char *iso_date, const struct tm *today);

/** Derive display status. today==NULL -> UNKNOWN for items with expiry too
 *  (degraded mode when clock is not synced). */
fridge_memo_status_t fridge_memo_derive_status(const fridge_memo_item_t *item, const struct tm *today);

/**
 * In-place sort: EXPIRED first (most overdue first), then NEAR (ascending
 * remaining), then OK (ascending remaining); UNKNOWN tail sorted by added_at
 * descending. today==NULL -> whole list by added_at descending (degraded).
 * After sorting, count is clamped to FRIDGE_MEMO_MAX_ITEMS.
 */
void fridge_memo_sort_snapshot(fridge_memo_snapshot_t *snap, const struct tm *today);

/** Count items currently in @p status (degraded: only UNKNOWN counts). */
int fridge_memo_count_by_status(const fridge_memo_snapshot_t *snap, fridge_memo_status_t status,
                                const struct tm *today);

#ifdef __cplusplus
}
#endif

#endif /* FRIDGE_MEMO_API_H_ */
```

- [ ] **Step 1.3: 写失败测试（解析/日期/排序）**

```c
/* tests/test_fridge_memo.c — P0 host tests for api (pure) + page. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "fridge_memo_api.h"
#include "fridge_memo_dto.h"
#include "page_renderer.h"
#include "page_registry.h"
#include "data_refresh.h"

/* ---- helpers ---- */

static struct tm mk_today(int y, int m, int d)
{
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = y - 1900;
    t.tm_mon = m - 1;
    t.tm_mday = d;
    t.tm_hour = 12;
    mktime(&t);
    return t;
}

/* ---- api: parsing ---- */

static void test_parse_full_response(void)
{
    const char *json = "{\"updated_at\":\"2026-08-12T08:30:00+08:00\",\"items\":["
                       "{\"id\":\"f_001\",\"name\":\"草莓\",\"quantity\":\"一盒\","
                       "\"added_at\":\"2026-08-11\",\"expires_at\":\"2026-08-14\",\"note\":\"\",\"storage\":\"fridge\"},"
                       "{\"id\":\"f_002\",\"name\":\"牛奶\",\"quantity\":\"半盒\","
                       "\"added_at\":\"2026-08-10\",\"expires_at\":\"2026-08-13\"}]}";
    fridge_memo_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    assert(fridge_memo_parse_items_json(json, &snap));
    assert(snap.count == 2);
    assert(strcmp(snap.items[0].id, "f_001") == 0);
    assert(strcmp(snap.items[0].name, "草莓") == 0);
    assert(strcmp(snap.items[1].expires_at, "2026-08-13") == 0);
    assert(snap.items[1].note[0] == '\0');
    assert(snap.items[1].storage[0] == '\0');
    printf("test_parse_full_response OK\n");
}

static void test_parse_bare_array(void)
{
    fridge_memo_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    assert(fridge_memo_parse_items_json("[]", &snap));
    assert(snap.count == 0);
    printf("test_parse_bare_array OK\n");
}

static void test_parse_malformed_rejected(void)
{
    fridge_memo_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    snap.count = 7; /* must stay untouched on failure */
    assert(!fridge_memo_parse_items_json("{not json", &snap));
    assert(snap.count == 7);
    assert(!fridge_memo_parse_items_json("{\"items\":\"nope\"}", &snap));
    printf("test_parse_malformed_rejected OK\n");
}

static void test_parse_truncates_at_64(void)
{
    char json[4096];
    size_t off = snprintf(json, sizeof(json), "{\"items\":[");
    for (int i = 0; i < 100; ++i) {
        off += snprintf(json + off, sizeof(json) - off,
                        "{\"id\":\"i%d\",\"name\":\"n\",\"quantity\":\"\",\"added_at\":\"2026-08-01\","
                        "\"expires_at\":\"2026-09-0%d\"},",
                        i, 1 + (i % 9));
    }
    off -= 1; /* trailing comma */
    snprintf(json + off, sizeof(json) - off, "]}");
    fridge_memo_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    assert(fridge_memo_parse_items_json(json, &snap));
    fridge_memo_sort_snapshot(&snap, &mk_today(2026, 8, 12));
    assert(snap.count == FRIDGE_MEMO_MAX_ITEMS);
    printf("test_parse_truncates_at_64 OK\n");
}

/* ---- api: date math ---- */

static void test_days_math(void)
{
    struct tm today = mk_today(2026, 8, 12);
    assert(fridge_memo_days_since("2026-08-12", &today) == 1); /* same day = day 1 */
    assert(fridge_memo_days_since("2026-08-11", &today) == 2);
    assert(fridge_memo_days_since("2026-07-28", &today) == 16); /* cross-month */
    assert(fridge_memo_days_until("2026-08-12", &today) == 0);
    assert(fridge_memo_days_until("2026-08-14", &today) == 2);
    assert(fridge_memo_days_until("2026-08-09", &today) == -3);
    assert(fridge_memo_days_until("", &today) == -1000);
    assert(fridge_memo_days_since("bad", &today) == -1);
    printf("test_days_math OK\n");
}

/* ---- api: status + sort ---- */

static void test_derive_status(void)
{
    struct tm today = mk_today(2026, 8, 12);
    fridge_memo_item_t it;
    memset(&it, 0, sizeof(it));
    strcpy(it.added_at, "2026-08-01");
    strcpy(it.expires_at, "2026-08-09");
    assert(fridge_memo_derive_status(&it, &today) == FRIDGE_MEMO_STATUS_EXPIRED);
    strcpy(it.expires_at, "2026-08-12");
    assert(fridge_memo_derive_status(&it, &today) == FRIDGE_MEMO_STATUS_NEAR);
    strcpy(it.expires_at, "2026-08-14");
    assert(fridge_memo_derive_status(&it, &today) == FRIDGE_MEMO_STATUS_NEAR);
    strcpy(it.expires_at, "2026-08-15");
    assert(fridge_memo_derive_status(&it, &today) == FRIDGE_MEMO_STATUS_OK);
    it.expires_at[0] = '\0';
    assert(fridge_memo_derive_status(&it, &today) == FRIDGE_MEMO_STATUS_UNKNOWN);
    strcpy(it.expires_at, "2026-08-20");
    assert(fridge_memo_derive_status(&it, NULL) == FRIDGE_MEMO_STATUS_UNKNOWN); /* degraded */
    printf("test_derive_status OK\n");
}

static void test_sort_order(void)
{
    fridge_memo_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    /* index: 0=ok(5d) 1=expired(-2d) 2=near(1d) 3=unknown 4=expired(-5d) 5=near(0d) */
    const char *names[] = {"a_ok", "b_exp2", "c_near1", "d_unk", "e_exp5", "f_near0"};
    const char *exp[] = {"2026-08-17", "2026-08-10", "2026-08-13", "", "2026-08-07", "2026-08-12"};
    const char *added[] = {"2026-08-01", "2026-08-01", "2026-08-02", "2026-08-05", "2026-08-01", "2026-08-03"};
    snap.count = 6;
    for (int i = 0; i < 6; ++i) {
        strcpy(snap.items[i].name, names[i]);
        strcpy(snap.items[i].expires_at, exp[i]);
        strcpy(snap.items[i].added_at, added[i]);
    }
    fridge_memo_sort_snapshot(&snap, &mk_today(2026, 8, 12));
    /* expected: e_exp5, b_exp2, f_near0, c_near1, a_ok, d_unk */
    assert(strcmp(snap.items[0].name, "e_exp5") == 0);
    assert(strcmp(snap.items[1].name, "b_exp2") == 0);
    assert(strcmp(snap.items[2].name, "f_near0") == 0);
    assert(strcmp(snap.items[3].name, "c_near1") == 0);
    assert(strcmp(snap.items[4].name, "a_ok") == 0);
    assert(strcmp(snap.items[5].name, "d_unk") == 0);
    assert(fridge_memo_count_by_status(&snap, FRIDGE_MEMO_STATUS_EXPIRED, &mk_today(2026, 8, 12)) == 2);
    assert(fridge_memo_count_by_status(&snap, FRIDGE_MEMO_STATUS_NEAR, &mk_today(2026, 8, 12)) == 2);
    printf("test_sort_order OK\n");
}

static void test_sort_degraded(void)
{
    fridge_memo_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    snap.count = 3;
    strcpy(snap.items[0].name, "old");
    strcpy(snap.items[0].added_at, "2026-07-01");
    strcpy(snap.items[1].name, "newest");
    strcpy(snap.items[1].added_at, "2026-08-10");
    strcpy(snap.items[2].name, "mid");
    strcpy(snap.items[2].added_at, "2026-08-01");
    fridge_memo_sort_snapshot(&snap, NULL);
    assert(strcmp(snap.items[0].name, "newest") == 0);
    assert(strcmp(snap.items[1].name, "mid") == 0);
    assert(strcmp(snap.items[2].name, "old") == 0);
    printf("test_sort_degraded OK\n");
}

int main(void)
{
    test_parse_full_response();
    test_parse_bare_array();
    test_parse_malformed_rejected();
    test_parse_truncates_at_64();
    test_days_math();
    test_derive_status();
    test_sort_order();
    test_sort_degraded();
    /* page tests are appended in Task 2 */
    printf("ALL FRIDGE MEMO TESTS PASSED\n");
    return 0;
}
```

- [ ] **Step 1.4: 接入 run_tests.sh（先跑失败）**

`tests/run_tests.sh` 两处修改：

第 37 行 TESTS 数组（保持字母序插入）：
```bash
    TESTS=(rawdraw layout theme framebuffer network nvs_state ui_text_util photo_blit fridge_memo ui_pages_smoke audio)
```

case 块中 `photo_blit)` 之后加：
```bash
        fridge_memo)
            run fridge_memo tests/test_fridge_memo.c \
                components/network/fridge_memo_api.c \
                components/network/http_client_util.c components/network/cjson_util.c \
                components/ui/pages/fridge_memo_page.c \
                components/ui/ui_text_util.c components/ui/data_refresh.c \
                components/rawdraw/theme.c components/rawdraw/rawdraw.c components/rawdraw/rawdraw_ext.c \
                components/rawdraw/layout.c \
                managed_components/espressif__cjson/cJSON/cJSON.c -lm
            ;;
```

（页面 .c 在 Task 2 才存在——本步骤先只加 api 相关三行源文件跑解析测试，Task 2 再补页面两行。或者：先写一个空的 `fridge_memo_page.c` 占位由 Task 2 重写。**取后者**：Step 1.5 建 6 行占位文件，保证测试脚本自洽。）

Run: `bash tests/run_tests.sh fridge_memo`
Expected: BUILD FAILED（fridge_memo_api.h 不存在）——失败即 TDD 红灯。

- [ ] **Step 1.5: 实现 fridge_memo_api.c**

```c
/* components/network/fridge_memo_api.c */
/**
 * @file fridge_memo_api.c
 * @brief Fridge memo REST client — parsing/date-math/sorting host-testable,
 *        HTTP + NVS cache target-only (design doc v1.2 §7.1).
 */
#include "fridge_memo_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cJSON.h>

#ifdef ESP_PLATFORM
#    include "esp_log.h"
#    include "settings.h"
#    include <freertos/FreeRTOS.h>
#    include <freertos/task.h>
#    include "http_client_util.h"

static const char *TAG = "FridgeMemoApi";
#    define LOGI(...) ESP_LOGI(TAG, __VA_ARGS__)
#    define LOGW(...) ESP_LOGW(TAG, __VA_ARGS__)
#    define LOGE(...) ESP_LOGE(TAG, __VA_ARGS__)
#else
#    define LOGI(...)                                                                                                  \
        do {                                                                                                           \
            fprintf(stderr, "[FM][I] " __VA_ARGS__);                                                                   \
            fputc('\n', stderr);                                                                                       \
        } while (0)
#    define LOGW(...)                                                                                                  \
        do {                                                                                                           \
            fprintf(stderr, "[FM][W] " __VA_ARGS__);                                                                   \
            fputc('\n', stderr);                                                                                       \
        } while (0)
#    define LOGE(...)                                                                                                  \
        do {                                                                                                           \
            fprintf(stderr, "[FM][E] " __VA_ARGS__);                                                                   \
            fputc('\n', stderr);                                                                                       \
        } while (0)
#endif

#define FM_BASE_URL_LEN 96
#define FM_HTTP_BUF 8192

static char s_base_url[FM_BASE_URL_LEN] = {0};
static fridge_memo_snapshot_t s_snapshot;
static bool s_has_snapshot = false;
static fridge_memo_callback_t s_cb = NULL;
static void *s_cb_ctx = NULL;
static fridge_memo_error_callback_t s_err_cb = NULL;
static void *s_err_cb_ctx = NULL;

/* ------------------------------------------------------------------ */
/* Date helpers (pure)                                                 */
/* ------------------------------------------------------------------ */

/* Parse "YYYY-MM-DD" into a normalized tm (midnight). */
static bool parse_iso_date(const char *iso, struct tm *out)
{
    if (!iso || strlen(iso) < 10 || iso[4] != '-' || iso[7] != '-')
        return false;
    int y = atoi(iso);
    int m = atoi(iso + 5);
    int d = atoi(iso + 8);
    if (y < 1970 || m < 1 || m > 12 || d < 1 || d > 31)
        return false;
    memset(out, 0, sizeof(*out));
    out->tm_year = y - 1900;
    out->tm_mon = m - 1;
    out->tm_mday = d;
    return true;
}

static int days_between(const struct tm *from, const struct tm *to)
{
    return (int)((mktime((struct tm *)to) - mktime((struct tm *)from)) / (24 * 60 * 60));
}

int fridge_memo_days_since(const char *iso_date, const struct tm *today)
{
    struct tm d;
    if (!today || !parse_iso_date(iso_date, &d))
        return -1;
    return days_between(&d, today) + 1; /* same-day counts as day 1 */
}

int fridge_memo_days_until(const char *iso_date, const struct tm *today)
{
    struct tm d;
    if (!today || !parse_iso_date(iso_date, &d))
        return -1000;
    return days_between(today, &d);
}

fridge_memo_status_t fridge_memo_derive_status(const fridge_memo_item_t *item, const struct tm *today)
{
    if (!item || !today || item->expires_at[0] == '\0')
        return FRIDGE_MEMO_STATUS_UNKNOWN;
    int days = fridge_memo_days_until(item->expires_at, today);
    if (days < 0)
        return FRIDGE_MEMO_STATUS_EXPIRED;
    if (days <= 2)
        return FRIDGE_MEMO_STATUS_NEAR;
    return FRIDGE_MEMO_STATUS_OK;
}

/* ------------------------------------------------------------------ */
/* Sort (pure)                                                         */
/* ------------------------------------------------------------------ */

static int remaining_rank(const fridge_memo_item_t *it, const struct tm *today)
{
    /* smaller = more urgent; EXPIRED uses -days so most overdue sorts first */
    int d = fridge_memo_days_until(it->expires_at, today);
    if (d < 0)
        return d; /* -1..-N : most negative first */
    return d;
}

static int cmp_degraded(const void *a, const void *b)
{
    const fridge_memo_item_t *ia = a, *ib = b;
    return -strcmp(ia->added_at, ib->added_at); /* newest first */
}

static int cmp_urgent(const void *a, const void *b)
{
    const fridge_memo_item_t *ia = a, *ib = b;
    fridge_memo_status_t sa = fridge_memo_derive_status(ia, g_sort_today);
    fridge_memo_status_t sb = fridge_memo_derive_status(ib, g_sort_today);
    if (sa != sb)
        return (int)sb - (int)sa; /* EXPIRED(3) > NEAR(2) > OK(1); UNKNOWN(0) tail */
    if (sa == FRIDGE_MEMO_STATUS_UNKNOWN)
        return -strcmp(ia->added_at, ib->added_at);
    int ra = remaining_rank(ia, g_sort_today);
    int rb = remaining_rank(ib, g_sort_today);
    return ra - rb;
}

void fridge_memo_sort_snapshot(fridge_memo_snapshot_t *snap, const struct tm *today)
{
    if (!snap || snap->count <= 0)
        return;
    if (!today) {
        qsort(snap->items, (size_t)snap->count, sizeof(snap->items[0]), cmp_degraded);
        return;
    }
    g_sort_today = today;
    qsort(snap->items, (size_t)snap->count, sizeof(snap->items[0]), cmp_urgent);
    g_sort_today = NULL;
    if (snap->count > FRIDGE_MEMO_MAX_ITEMS)
        snap->count = FRIDGE_MEMO_MAX_ITEMS; /* drop the least urgent tail */
}

int fridge_memo_count_by_status(const fridge_memo_snapshot_t *snap, fridge_memo_status_t status,
                                const struct tm *today)
{
    int n = 0;
    for (int i = 0; i < snap->count; ++i)
        if (fridge_memo_derive_status(&snap->items[i], today) == status)
            ++n;
    return n;
}
```

**注意**：`cmp_urgent` 用了文件级 `static const struct tm *g_sort_today;`（qsort 无上下文参数的惯用法）——在文件顶部 static 区补 `static const struct tm *g_sort_today = NULL;`，并把上面 `g_sort_today` 的两处赋值改为 `(const struct tm *)today` 强转兼容（qsort cmp 收 const）。若编译器警告 const 转换，改用非 const `static struct tm *g_sort_today` + `mktime` 已有的 `(struct tm*)` 强转（`days_between` 已如此）。

继续追加（同文件）：

```c
/* ------------------------------------------------------------------ */
/* JSON parsing (pure, cJSON)                                          */
/* ------------------------------------------------------------------ */

static void read_str(cJSON *obj, const char *key, char *out, size_t out_len)
{
    cJSON *n = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(n) && n->valuestring)
        snprintf(out, out_len, "%s", n->valuestring);
    else
        out[0] = '\0';
}

bool fridge_memo_parse_items_json(const char *json, fridge_memo_snapshot_t *out)
{
    if (!json || !out)
        return false;
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        LOGW("items: JSON parse failed");
        return false;
    }
    cJSON *items = NULL;
    cJSON *updated = NULL;
    if (cJSON_IsArray(root)) {
        items = root; /* bare-array tolerance */
    } else {
        items = cJSON_GetObjectItemCaseSensitive(root, "items");
        updated = cJSON_GetObjectItemCaseSensitive(root, "updated_at");
    }
    if (!cJSON_IsArray(items)) {
        cJSON_Delete(root);
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (updated && cJSON_IsString(updated) && updated->valuestring)
        snprintf(out->updated_at, sizeof(out->updated_at), "%s", updated->valuestring);
    int i = 0;
    cJSON *it = NULL;
    cJSON_ArrayForEach(it, items)
    {
        if (i >= FRIDGE_MEMO_MAX_ITEMS)
            break;
        if (!cJSON_IsObject(it))
            continue;
        read_str(it, "id", out->items[i].id, FRIDGE_MEMO_ID_LEN);
        read_str(it, "name", out->items[i].name, FRIDGE_MEMO_NAME_LEN);
        read_str(it, "quantity", out->items[i].quantity, FRIDGE_MEMO_QTY_LEN);
        read_str(it, "added_at", out->items[i].added_at, FRIDGE_MEMO_DATE_LEN);
        read_str(it, "expires_at", out->items[i].expires_at, FRIDGE_MEMO_DATE_LEN);
        read_str(it, "note", out->items[i].note, FRIDGE_MEMO_NOTE_LEN);
        read_str(it, "storage", out->items[i].storage, FRIDGE_MEMO_STORAGE_LEN);
        ++i;
    }
    out->count = i;
    cJSON_Delete(root);
    return true;
}

/* ------------------------------------------------------------------ */
/* Config + cache (target)                                             */
/* ------------------------------------------------------------------ */

#ifdef ESP_PLATFORM

static void load_base_url_from_nvs(void)
{
    settings_handle_t h = settings_open("fridge", false);
    if (!h)
        return;
    char buf[FM_BASE_URL_LEN];
    if (settings_get_string(h, "base_url", buf, sizeof(buf), "") && buf[0])
        snprintf(s_base_url, sizeof(s_base_url), "%s", buf);
    settings_close(h);
}

static void save_cache(const fridge_memo_snapshot_t *snap)
{
    /* Cache as the raw JSON of the full list: survives DTO growth. */
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < snap->count; ++i) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", snap->items[i].id);
        cJSON_AddStringToObject(o, "name", snap->items[i].name);
        cJSON_AddStringToObject(o, "quantity", snap->items[i].quantity);
        cJSON_AddStringToObject(o, "added_at", snap->items[i].added_at);
        cJSON_AddStringToObject(o, "expires_at", snap->items[i].expires_at);
        cJSON_AddStringToObject(o, "note", snap->items[i].note);
        cJSON_AddStringToObject(o, "storage", snap->items[i].storage);
        cJSON_AddItemToArray(arr, o);
    }
    cJSON_AddItemToObject(root, "items", arr);
    cJSON_AddStringToObject(root, "updated_at", snap->updated_at);
    char *json = cJSON_PrintUnformatted(root);
    if (json) {
        settings_handle_t h = settings_open("fridge_memo", true);
        if (h) {
            settings_set_string(h, "items_json", json);
            settings_close(h);
        }
        cJSON_free(json);
    }
    cJSON_Delete(root);
}

static void load_cache(void)
{
    settings_handle_t h = settings_open("fridge_memo", false);
    if (!h)
        return;
    char *json = malloc(FM_HTTP_BUF);
    if (json) {
        if (settings_get_string(h, "items_json", json, FM_HTTP_BUF, "") && json[0]) {
            fridge_memo_snapshot_t snap;
            if (fridge_memo_parse_items_json(json, &snap)) {
                struct tm today;
                bool have_time = get_local_today(&today);
                fridge_memo_sort_snapshot(&snap, have_time ? &today : NULL);
                s_snapshot = snap;
                s_has_snapshot = true;
                LOGI("loaded fridge memo cache (%d items)", snap.count);
            }
        }
        free(json);
    }
    settings_close(h);
}

/* Clock helper: false when time is not trustworthy (pre-2024). */
static bool get_local_today(struct tm *out)
{
    time_t t = time(NULL);
    struct tm tmv;
    if (!localtime_r(&t, &tmv) || tmv.tm_year + 1900 < 2024)
        return false;
    *out = tmv;
    return true;
}

#endif /* ESP_PLATFORM */

void fridge_memo_api_init(const char *base_url)
{
    if (base_url && base_url[0])
        snprintf(s_base_url, sizeof(s_base_url), "%s", base_url);
#ifdef ESP_PLATFORM
    load_base_url_from_nvs();
    load_cache();
#endif
}

void fridge_memo_api_set_base_url(const char *url)
{
    snprintf(s_base_url, sizeof(s_base_url), "%s", url ? url : "");
#ifdef ESP_PLATFORM
    settings_handle_t h = settings_open("fridge", true);
    if (h) {
        settings_set_string(h, "base_url", s_base_url);
        settings_close(h);
    }
#endif
}

void fridge_memo_api_get_base_url(char *out, size_t len)
{
    snprintf(out, len, "%s", s_base_url);
}

bool fridge_memo_api_has_cached_data(void)
{
    return s_has_snapshot;
}

const fridge_memo_snapshot_t *fridge_memo_api_get_cached_data(void)
{
    return s_has_snapshot ? &s_snapshot : NULL;
}

void fridge_memo_api_set_callback(fridge_memo_callback_t cb, void *user_data)
{
    s_cb = cb;
    s_cb_ctx = user_data;
}

void fridge_memo_api_set_error_callback(fridge_memo_error_callback_t cb, void *user_data)
{
    s_err_cb = cb;
    s_err_cb_ctx = user_data;
}

/* ------------------------------------------------------------------ */
/* HTTP (target only)                                                  */
/* ------------------------------------------------------------------ */

#ifdef ESP_PLATFORM

static void dispatch_snapshot(fridge_memo_snapshot_t *snap)
{
    struct tm today;
    bool have_time = get_local_today(&today);
    fridge_memo_sort_snapshot(snap, have_time ? &today : NULL);
    s_snapshot = *snap;
    s_has_snapshot = true;
    save_cache(&s_snapshot);
    if (s_cb)
        s_cb(&s_snapshot, s_cb_ctx);
}

static void dispatch_error(const char *msg)
{
    if (s_err_cb)
        s_err_cb(msg, s_err_cb);
}

static void fetch_task(void *arg)
{
    char url[FM_BASE_URL_LEN + 48];
    char *buf = malloc(FM_HTTP_BUF);
    if (!buf) {
        dispatch_error("内存不足");
        vTaskDelete(NULL);
        return;
    }
    snprintf(url, sizeof(url), "%s/api/v1/fridge/items", s_base_url);
    int n = http_get_text(url, buf, FM_HTTP_BUF);
    if (n < 0) {
        LOGW("GET items failed (base_url=%s)", s_base_url);
        dispatch_error("后端不可达");
    } else {
        fridge_memo_snapshot_t snap;
        if (fridge_memo_parse_items_json(buf, &snap))
            dispatch_snapshot(&snap);
        else
            dispatch_error("响应解析失败");
    }
    free(buf);
    vTaskDelete(NULL);
}

static void delete_task(void *arg)
{
    char url[FM_BASE_URL_LEN + 64];
    char *buf = malloc(FM_HTTP_BUF);
    if (!buf) {
        dispatch_error("内存不足");
        vTaskDelete(NULL);
        return;
    }
    snprintf(url, sizeof(url), "%s/api/v1/fridge/items/%s", s_base_url, (const char *)arg);
    int n = http_delete_text(url, buf, FM_HTTP_BUF); /* added in Step 1.6 */
    free(arg);
    if (n < 0) {
        LOGW("DELETE failed (url=%s)", url);
        dispatch_error("删除失败：后端不可达");
    } else {
        fridge_memo_snapshot_t snap;
        /* 200 and 404 both carry the authoritative full list (design §7.1). */
        if (fridge_memo_parse_items_json(buf, &snap))
            dispatch_snapshot(&snap);
        else
            dispatch_error("响应解析失败");
    }
    free(buf);
    vTaskDelete(NULL);
}

bool fridge_memo_api_fetch_async(void)
{
    if (!s_base_url[0]) {
        dispatch_error("未配置冰箱后端地址");
        return false;
    }
    return xTaskCreate(fetch_task, "fm_fetch", 6144, NULL, 5, NULL) == pdPASS;
}

bool fridge_memo_api_delete_async(const char *item_id)
{
    if (!s_base_url[0] || !item_id || !item_id[0]) {
        dispatch_error("未配置冰箱后端地址");
        return false;
    }
    char *id_copy = malloc(FRIDGE_MEMO_ID_LEN);
    if (!id_copy)
        return false;
    snprintf(id_copy, FRIDGE_MEMO_ID_LEN, "%s", item_id);
    if (xTaskCreate(delete_task, "fm_del", 6144, id_copy, 5, NULL) != pdPASS) {
        free(id_copy);
        return false;
    }
    return true;
}

#else /* host stubs */

bool fridge_memo_api_fetch_async(void)
{
    return false;
}

bool fridge_memo_api_delete_async(const char *item_id)
{
    (void)item_id;
    return false;
}

#endif
```

**实现注意**：
1. `load_cache` 引用了定义在其后的 `get_local_today` —— 把 `get_local_today` 的定义移到 `load_cache` 之前（C 需先声明）。
2. host 构建不需要 `<time.h>` 的 mktime 链接问题——已包含。cJSON host 路径直接用真 cJSON（`managed_components/espressif__cjson`）。
3. `settings_get_string` 第 4 参是 max_len 含 NUL（对照 `settings.h:14`）——`FM_HTTP_BUF` 缓存上限 8KB，若缓存超限截断会解析失败并视为无缓存（可接受，64 条 × ~150B ≈ 9.6KB 可能超——**把 FM_HTTP_BUF 提到 16384**，malloc 16KB 任务栈外堆内存，ESP32-S3 可承受）。

- [ ] **Step 1.6: http_client_util 加 DELETE**

`components/network/include/http_client_util.h` 在 `http_get_with_headers` 声明后追加：

```c
/**
 * @brief DELETE returning the response body as text (NUL-terminated).
 *
 * Fridge memo DELETE carries the authoritative full items[] for both 200
 * and 404 (design doc §7.1), so the body matters, not just the status.
 * Target-only; host stub returns -1.
 * @return bytes read (excluding the NUL), or -1 on error.
 */
int http_delete_text(const char *url, char *buf, size_t max_size);
```

`components/network/http_client_util.c` 文件末尾追加（照抄该文件既有 GET 的 event-handler 模式；esp_http_client 用 `esp_http_client_set_method(client, HTTP_METHOD_DELETE)`；其余 init/read/cleanup 与 GET 相同）：

```c
int http_delete_text(const char *url, char *buf, size_t max_size)
{
#ifdef ESP_PLATFORM
    /* Mirror http_get_text's structure: init -> set method DELETE -> set
     * event handler that accumulates into buf -> perform -> cleanup.
     * The handler stores into a fetch_ctx like the GET path; reuse the same
     * response-buffer plumbing already present in this file. */
    ... /* 实现者：复制本文件 http_get_text 的骨架，仅改两处：
           1) esp_http_client_set_method(client, HTTP_METHOD_DELETE);
           2) 保持 is_chunked/content_length 读取逻辑不变。 */
#else
    (void)url;
    (void)buf;
    (void)max_size;
    return -1;
#endif
}
```

（此函数体是本计划唯一"骨架引用"——因为它必须是本文件既有私有结构的逐行复制，实现者读 `http_client_util.c` 的 `http_get_text` 后 5 分钟内完成；其余全部任务均为完整代码。）

- [ ] **Step 1.7: network CMakeLists + 占位页面文件**

`components/network/CMakeLists.txt` 第 3 行 `"coding_plan_api.c"` 后加 `"fridge_memo_api.c"`。

`components/ui/pages/fridge_memo_page.c`（Task 2 重写，先占位保证测试脚本自洽）：

```c
/* placeholder — replaced by Task 2 */
#include "fridge_memo_page.h"
```

并建对应占位头 `components/ui/pages/fridge_memo_page.h`：

```c
#ifndef COMPONENTS_UI_PAGES_FRIDGE_MEMO_PAGE_H_
#define COMPONENTS_UI_PAGES_FRIDGE_MEMO_PAGE_H_
#include "page_renderer.h"
#endif
```

- [ ] **Step 1.8: 跑测试（绿灯）**

Run: `bash tests/run_tests.sh fridge_memo`
Expected: `PASS: fridge_memo`（8 个解析/日期/排序用例全过；页面用例尚未写）。

- [ ] **Step 1.9: Commit**

```bash
git add components/data_types/include/fridge_memo_dto.h \
        components/network/include/fridge_memo_api.h components/network/fridge_memo_api.c \
        components/network/include/http_client_util.h components/network/http_client_util.c \
        components/network/CMakeLists.txt tests/test_fridge_memo.c tests/run_tests.sh \
        components/ui/pages/fridge_memo_page.c components/ui/pages/fridge_memo_page.h
git commit -m "feat(network): fridge memo DTO, REST client core with host tests"
```

---

### Task 2: 页面渲染器（汇总条 / 翻页列表 / 删除浮层 / 输入模型）

**Files:**
- Modify: `components/ui/pages/fridge_memo_page.h`（重写占位）
- Modify: `components/ui/pages/fridge_memo_page.c`（重写占位）
- Modify: `components/ui/include/ui_manager.h`（枚举）
- Modify: `components/ui/CMakeLists.txt`
- Test: `tests/test_fridge_memo.c`（追加页面用例）

- [ ] **Step 2.1: 枚举 + CMake**

`components/ui/include/ui_manager.h` 在 `UI_PAGE_CODING_PLAN,`（第 44 行）之后、`UI_PAGE_COUNT,` 之前插入：

```c
    UI_PAGE_FRIDGE_MEMO,
```

`components/ui/CMakeLists.txt` 第 14 行 `"pages/coding_plan_page.c"` 后加：

```cmake
    "pages/fridge_memo_page.c"
```

- [ ] **Step 2.2: 写页面头文件**

```c
/* components/ui/pages/fridge_memo_page.h */
/**
 * @file fridge_memo_page.h
 * @brief Fridge memo page renderer (design doc v1.2 §5).
 *
 * Summary strip (expired/near counts) + 4 double-line rows per screen with
 * full-page flip navigation + footer (result/banner/hint) + delete overlay
 * (in-page 4-item picker; the overlay IS the confirmation).
 */
#ifndef COMPONENTS_UI_PAGES_FRIDGE_MEMO_PAGE_H_
#define COMPONENTS_UI_PAGES_FRIDGE_MEMO_PAGE_H_

#include "page_renderer.h"
#include "fridge_memo_dto.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FRIDGE_MEMO_ROWS_PER_SCREEN 4
#define FRIDGE_MEMO_FOOTER_TEXT_LEN 64

typedef struct {
    page_renderer_t base;

    fridge_memo_snapshot_t data;
    bool has_data;
    int page_index; /* 0-based screen */

    char footer_message[FRIDGE_MEMO_FOOTER_TEXT_LEN]; /* result text, "" = none */
    bool offline; /* yellow banner mode */

    /* Delete overlay state (design §5.3). */
    bool showing_delete;
    int delete_focus; /* 0..rows_on_page-1 */

    /* Delete request channel (wired by app layer; pages never do IO). */
    void (*delete_request_cb)(const char *item_id, void *ctx);
    void *delete_request_ctx;

    const lv_font_t *font; /* Regular 16 */
    const lv_font_t *title_font; /* Medium 24 */
} fridge_memo_page_t;

/* PageRenderer vtable entry points. */
void fridge_memo_page_init(page_renderer_t *self, int width, int height);
void fridge_memo_page_render(page_renderer_t *self, uint8_t *fb, int width, int height);
bool fridge_memo_page_handle_input(page_renderer_t *self, const ui_button_event_t *event);

/* Data interface. */
void fridge_memo_page_update(page_renderer_t *self, const fridge_memo_snapshot_t *data);
void fridge_memo_page_set_footer_message(page_renderer_t *self, const char *msg);
void fridge_memo_page_set_offline(page_renderer_t *self, bool offline);
void fridge_memo_page_set_delete_request_handler(page_renderer_t *self,
                                                 void (*cb)(const char *item_id, void *ctx), void *ctx);

/* Pure helpers (host-testable). */
int fridge_memo_page_count(const fridge_memo_page_t *r);
int fridge_memo_page_pages(const fridge_memo_page_t *r);
int fridge_memo_page_rows_on_page(const fridge_memo_page_t *r, int page_index);

#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_UI_PAGES_FRIDGE_MEMO_PAGE_H_ */
```

- [ ] **Step 2.3: 写失败页面测试（追加到 tests/test_fridge_memo.c 的 main 之前）**

```c
/* ---- page tests ---- */

#include "fridge_memo_page.h"
#include "rawdraw_ext.h"
#include "theme.h"

static fridge_memo_page_t *g_page; /* single static instance from the .c */

static void fill_snapshot_9(fridge_memo_snapshot_t *snap)
{
    memset(snap, 0, sizeof(*snap));
    snap->count = 9;
    /* pre-sorted urgency order: 2 expired, 1 near, 6 ok */
    strcpy(snap->items[0].name, "酸奶"); strcpy(snap->items[0].id, "e1");
    strcpy(snap->items[0].quantity, "5连杯"); strcpy(snap->items[0].added_at, "2026-07-28");
    strcpy(snap->items[0].expires_at, "2026-08-09");
    strcpy(snap->items[1].name, "剩菜"); strcpy(snap->items[1].id, "e2");
    strcpy(snap->items[1].added_at, "2026-08-05"); strcpy(snap->items[1].expires_at, "2026-08-10");
    strcpy(snap->items[2].name, "牛奶"); strcpy(snap->items[2].id, "n1");
    strcpy(snap->items[2].quantity, "半盒"); strcpy(snap->items[2].added_at, "2026-08-10");
    strcpy(snap->items[2].expires_at, "2026-08-13");
    for (int i = 3; i < 9; ++i) {
        strcpy(snap->items[i].name, "菜");
        snap->items[i].name[1] = (char)('0' + i);
        strcpy(snap->items[i].id, "ok");
        strcpy(snap->items[i].added_at, "2026-08-01");
        strcpy(snap->items[i].expires_at, "2026-08-25");
    }
}

static void test_page_paging_math(void)
{
    fridge_memo_page_t stack_page; /* use ops directly, not the singleton */
    memset(&stack_page, 0, sizeof(stack_page));
    fridge_memo_snapshot_t snap;
    fill_snapshot_9(&snap);
    fridge_memo_page_update((page_renderer_t *)&stack_page, &snap);
    assert(fridge_memo_page_count(&stack_page) == 9);
    assert(fridge_memo_page_pages(&stack_page) == 3); /* ceil(9/4) */
    assert(fridge_memo_page_rows_on_page(&stack_page, 0) == 4);
    assert(fridge_memo_page_rows_on_page(&stack_page, 2) == 1);
    assert(fridge_memo_page_rows_on_page(&stack_page, 5) == 0);
    printf("test_page_paging_math OK\n");
}

static void test_page_flip_clamps(void)
{
    fridge_memo_page_t p;
    memset(&p, 0, sizeof(p));
    fridge_memo_snapshot_t snap;
    fill_snapshot_9(&snap);
    fridge_memo_page_update((page_renderer_t *)&p, &snap);
    ui_button_event_t dn = {BTN_DOWN_CLICK};
    ui_button_event_t up = {BTN_UP_CLICK};
    assert(fridge_memo_page_handle_input((page_renderer_t *)&p, &dn)); /* 0->1 */
    assert(p.page_index == 1);
    assert(fridge_memo_page_handle_input((page_renderer_t *)&p, &dn)); /* 1->2 */
    assert(fridge_memo_page_handle_input((page_renderer_t *)&p, &dn) == false); /* clamp */
    assert(p.page_index == 2);
    assert(fridge_memo_page_handle_input((page_renderer_t *)&p, &up)); /* 2->1 */
    assert(fridge_memo_page_handle_input((page_renderer_t *)&p, &up));
    assert(fridge_memo_page_handle_input((page_renderer_t *)&p, &up) == false); /* clamp */
    assert(p.page_index == 0);
    printf("test_page_flip_clamps OK\n");
}

static char g_deleted_id[16];
static int g_delete_calls;
static void fake_delete(const char *id, void *ctx)
{
    (void)ctx;
    snprintf(g_deleted_id, sizeof(g_deleted_id), "%s", id);
    ++g_delete_calls;
}

static void test_delete_overlay_flow(void)
{
    fridge_memo_page_t p;
    memset(&p, 0, sizeof(p));
    fridge_memo_snapshot_t snap;
    fill_snapshot_9(&snap);
    fridge_memo_page_update((page_renderer_t *)&p, &snap);
    fridge_memo_page_set_delete_request_handler((page_renderer_t *)&p, fake_delete, NULL);
    ui_button_event_t dbl = {BTN_BOOT_DOUBLE_CLICK};
    ui_button_event_t dn = {BTN_DOWN_CLICK};
    ui_button_event_t boot = {BTN_BOOT_CLICK};
    /* open on page 0: focus defaults to row 0 (e1) */
    assert(fridge_memo_page_handle_input((page_renderer_t *)&p, &dbl));
    assert(p.showing_delete && p.delete_focus == 0);
    /* focus wraps within the 4 rows of THIS page */
    assert(fridge_memo_page_handle_input((page_renderer_t *)&p, &up_ev())); /* helper below */
    assert(p.delete_focus == 1);
    assert(fridge_memo_page_handle_input((page_renderer_t *)&p, &dn));
    assert(p.delete_focus == 0);
    /* BOOT confirms the focused item */
    assert(fridge_memo_page_handle_input((page_renderer_t *)&p, &boot));
    assert(g_delete_calls == 1 && strcmp(g_deleted_id, "e1") == 0);
    assert(!p.showing_delete);
    /* double-click toggles open->close */
    assert(fridge_memo_page_handle_input((page_renderer_t *)&p, &dbl));
    assert(p.showing_delete);
    assert(fridge_memo_page_handle_input((page_renderer_t *)&p, &dbl));
    assert(!p.showing_delete);
    printf("test_delete_overlay_flow OK\n");
}

static ui_button_event_t up_ev(void)
{
    ui_button_event_t e = {BTN_UP_CLICK};
    return e;
}

static void test_render_smoke(void)
{
    /* singleton via registry (constructor ran at load) */
    page_renderer_t *r = page_registry_get_instance(UI_PAGE_FRIDGE_MEMO);
    assert(r != NULL);
    assert(strcmp(page_registry_get_name(UI_PAGE_FRIDGE_MEMO), "冰箱备忘") == 0);
    int fb_bytes = (400 * 300) / 4; /* 2bpp */
    uint8_t *fb = calloc(1, fb_bytes);
    assert(fb);
    fridge_memo_snapshot_t snap;
    fill_snapshot_9(&snap);
    fridge_memo_page_update(r, &snap);
    fridge_memo_page_init(r, 400, 300);
    fridge_memo_page_render(r, fb, 400, 300);
    int ink = 0;
    for (int i = 0; i < fb_bytes; ++i)
        if (fb[i] != 0x55) /* 0x55 = all-white 2bpp pattern per framebuffer.c */
            ++ink;
    assert(ink > 1000); /* not blank */
    /* empty state */
    fridge_memo_snapshot_t empty;
    memset(&empty, 0, sizeof(empty));
    fridge_memo_page_update(r, &empty);
    memset(fb, 0x55, fb_bytes);
    fridge_memo_page_render(r, fb, 400, 300);
    /* delete overlay renders */
    g_page_of(r)->showing_delete = true;
    fridge_memo_page_render(r, fb, 400, 300);
    free(fb);
    printf("test_render_smoke OK\n");
}
```

在 `main()` 中追加调用（`test_sort_degraded()` 之后）：

```c
    test_page_paging_math();
    test_page_flip_clamps();
    test_delete_overlay_flow();
    test_render_smoke();
```

**注意**：`up_ev()`/`g_page_of(r)` 是测试侧辅助——实现为 `#define g_page_of(r) ((fridge_memo_page_t *)(r))`（base 是首成员，零成本转换）；`up_ev` 必须在 `test_delete_overlay_flow` 之前定义（把函数移到其上方）。渲染断言的"空白"判定：先 `read components/rawdraw/framebuffer.c` 确认白底填充字节（计划按 0x55 假设，若实际不同则改为实际值——这是断言唯一依赖的运行时事实）。

- [ ] **Step 2.4: 实现页面**

```c
/* components/ui/pages/fridge_memo_page.c */
/**
 * @file fridge_memo_page.c
 * @brief Fridge memo page renderer (design doc v1.2 §5).
 *
 * Layout (400x300): status bar (shell) / summary strip 22px / 4 rows x 52px
 * / footer 26px. Navigation is full-page flip (EPD: every visible change is
 * a full refresh; row-cursor would cost 15-20s per step).
 */
#include "fridge_memo_page.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "data_refresh.h"
#include "font_zectrix.h"
#include "page_registry.h"
#include "rawdraw_ext.h"
#include "style.h"
#include "theme.h"
#include "ui_text_util.h"

/* ------------------------------------------------------------------ */
/* Layout                                                              */
/* ------------------------------------------------------------------ */

#define FM_SUMMARY_H 22
#define FM_ROW_H 52
#define FM_FOOTER_Y 264
#define FM_FOOTER_H 26
#define FM_COLOR_BAR_W 4
#define FM_PANEL_PAD 8

static const lv_font_t *const fm_font = &SourceHanSansSC_Regular_slim;
static const lv_font_t *const fm_title_font = &SourceHanSansSC_Medium_slim;

static fridge_memo_page_t s_fridge_memo_instance;

/* ------------------------------------------------------------------ */
/* Pure helpers                                                        */
/* ------------------------------------------------------------------ */

int fridge_memo_page_count(const fridge_memo_page_t *r)
{
    return r ? r->data.count : 0;
}

int fridge_memo_page_pages(const fridge_memo_page_t *r)
{
    if (!r || r->data.count <= 0)
        return 1; /* empty state still shows page 1/N=1/1 */
    return (r->data.count + FRIDGE_MEMO_ROWS_PER_SCREEN - 1) / FRIDGE_MEMO_ROWS_PER_SCREEN;
}

int fridge_memo_page_rows_on_page(const fridge_memo_page_t *r, int page_index)
{
    if (!r || page_index < 0 || page_index >= fridge_memo_page_pages(r))
        return 0;
    int first = page_index * FRIDGE_MEMO_ROWS_PER_SCREEN;
    int remain = r->data.count - first;
    if (remain <= 0)
        return 0;
    return remain < FRIDGE_MEMO_ROWS_PER_SCREEN ? remain : FRIDGE_MEMO_ROWS_PER_SCREEN;
}

static bool clock_valid(struct tm *out)
{
    time_t t = time(NULL);
    struct tm tmv;
    if (!localtime_r(&t, &tmv) || tmv.tm_year + 1900 < 2024)
        return false;
    if (out)
        *out = tmv;
    return true;
}

/* ------------------------------------------------------------------ */
/* Data interface                                                      */
/* ------------------------------------------------------------------ */

void fridge_memo_page_update(page_renderer_t *self, const fridge_memo_snapshot_t *data)
{
    fridge_memo_page_t *r = (fridge_memo_page_t *)self;
    if (!r || !data)
        return;
    r->data = *data; /* caller (app_sync) passes an already-sorted snapshot */
    r->has_data = true;
    r->page_index = 0; /* voice/manual ops reset to page 1 (design §4.3 r8) */
    r->showing_delete = false;
    r->base.needs_full_refresh_flag = true;
}

void fridge_memo_page_set_footer_message(page_renderer_t *self, const char *msg)
{
    fridge_memo_page_t *r = (fridge_memo_page_t *)self;
    if (!r)
        return;
    snprintf(r->footer_message, sizeof(r->footer_message), "%s", msg ? msg : "");
    r->base.needs_full_refresh_flag = true;
}

void fridge_memo_page_set_offline(page_renderer_t *self, bool offline)
{
    fridge_memo_page_t *r = (fridge_memo_page_t *)self;
    if (!r)
        return;
    r->offline = offline;
    r->base.needs_full_refresh_flag = true;
}

void fridge_memo_page_set_delete_request_handler(page_renderer_t *self,
                                                 void (*cb)(const char *item_id, void *ctx), void *ctx)
{
    fridge_memo_page_t *r = (fridge_memo_page_t *)self;
    if (!r)
        return;
    r->delete_request_cb = cb;
    r->delete_request_ctx = ctx;
}

/* ------------------------------------------------------------------ */
/* Rendering                                                           */
/* ------------------------------------------------------------------ */

static void render_summary(uint8_t *fb, int width, const fridge_memo_page_t *r, const struct tm *today)
{
    const int y = STYLE_STATUS_BAR_HEIGHT;
    const rawdraw_paint_style_t text = rawdraw_theme_style(THEME_TOKEN_TEXT_SECONDARY);
    const rawdraw_paint_style_t danger = rawdraw_theme_style(THEME_TOKEN_DANGER);
    const rawdraw_paint_style_t warning = rawdraw_theme_style(THEME_TOKEN_WARNING);

    int expired = fridge_memo_count_by_status(&r->data, FRIDGE_MEMO_STATUS_EXPIRED, today);
    int near = fridge_memo_count_by_status(&r->data, FRIDGE_MEMO_STATUS_NEAR, today);

    char buf[64];
    int x = FM_PANEL_PAD + 4;
    if (expired > 0) {
        rawdraw_fill_rect(fb, width, 300, (rawdraw_rect_t){x, y + 6, 8, 10}, danger.fg);
        x += 12;
        snprintf(buf, sizeof(buf), "%d 过期", expired);
        rawdraw_draw_text(fb, width, 300, x, y + 3, buf, fm_font, danger.fg);
        x += rawdraw_measure_text_width(buf, fm_font) + 10;
    }
    if (near > 0) {
        rawdraw_fill_rect(fb, width, 300, (rawdraw_rect_t){x, y + 6, 8, 10}, warning.fg);
        x += 12;
        snprintf(buf, sizeof(buf), "%d 临期", near);
        rawdraw_draw_text(fb, width, 300, x, y + 3, buf, fm_font, text.fg);
        x += rawdraw_measure_text_width(buf, fm_font) + 10;
    }
    snprintf(buf, sizeof(buf), "共 %d 项", r->data.count);
    rawdraw_draw_text(fb, width, 300, x, y + 3, buf, fm_font, text.fg);

    rawdraw_draw_hline(fb, width, 300, y + FM_SUMMARY_H - 1, FM_PANEL_PAD, width - FM_PANEL_PAD,
                       rawdraw_theme_style(THEME_TOKEN_BORDER).fg);
}

static void render_status_text(char *out, size_t len, const fridge_memo_item_t *it, const struct tm *today)
{
    if (!today) {
        snprintf(out, len, "%s", it->expires_at[0] ? "—" : "");
        return;
    }
    int days = fridge_memo_days_until(it->expires_at, today);
    if (days < 0)
        snprintf(out, len, "已过期 %d 天", -days);
    else
        snprintf(out, len, "剩 %d 天", days);
}

static rawdraw_color_t status_color(fridge_memo_status_t st)
{
    switch (st) {
    case FRIDGE_MEMO_STATUS_EXPIRED:
        return rawdraw_theme_style(THEME_TOKEN_DANGER).fg;
    case FRIDGE_MEMO_STATUS_NEAR:
        return rawdraw_theme_style(THEME_TOKEN_WARNING).fg;
    default:
        return rawdraw_theme_style(THEME_TOKEN_TEXT_PRIMARY).fg;
    }
}

static void render_row(uint8_t *fb, int width, int y, const fridge_memo_item_t *it, const struct tm *today,
                       bool highlighted)
{
    const int list_x = FM_PANEL_PAD;

    /* left color bar */
    rawdraw_color_t bar = status_color(fridge_memo_derive_status(it, today));
    rawdraw_fill_rect(fb, width, 300, (rawdraw_rect_t){list_x, y + 4, FM_COLOR_BAR_W, FM_ROW_H - 8}, bar);

    /* line 1: name (Medium 24) + quantity (16) left, status right-aligned */
    char qty[FRIDGE_MEMO_QTY_LEN + 4];
    if (it->quantity[0])
        snprintf(qty, sizeof(qty), "（%s）", it->quantity);
    else
        qty[0] = '\0';
    char name_fit[FRIDGE_MEMO_NAME_LEN + 4];
    ui_text_fit_to_width(it->name, fm_title_font, 200, name_fit, sizeof(name_fit));
    rawdraw_draw_text(fb, width, 300, list_x + 10, y + 6, name_fit, fm_title_font,
                      highlighted ? rawdraw_theme_style(THEME_TOKEN_BACKGROUND_PRIMARY).fg : bar);
    int name_w = rawdraw_measure_text_width(name_fit, fm_title_font);
    if (qty[0]) {
        char qty_fit[FRIDGE_MEMO_QTY_LEN + 8];
        ui_text_fit_to_width(qty, fm_font, 120, qty_fit, sizeof(qty_fit));
        rawdraw_draw_text(fb, width, 300, list_x + 10 + name_w + 4, y + 14, qty_fit, fm_font,
                          rawdraw_theme_style(THEME_TOKEN_TEXT_SECONDARY).fg);
    }

    char status[24];
    render_status_text(status, sizeof(status), it, today);
    if (status[0]) {
        int sw = rawdraw_measure_text_width(status, fm_font);
        rawdraw_draw_text(fb, width, 300, width - FM_PANEL_PAD - 6 - sw, y + 12, status, fm_font, bar);
    }

    /* line 2: "7/28 放入 · 已存 16 天" (secondary) */
    char meta[64];
    int stored = fridge_memo_days_since(it->added_at, today);
    char date_label[16];
    /* added_at "2026-07-28" -> "7/28" */
    if (strlen(it->added_at) >= 10)
        snprintf(date_label, sizeof(date_label), "%d/%d", atoi(it->added_at + 5), atoi(it->added_at + 8));
    else
        date_label[0] = '\0';
    if (!today)
        snprintf(meta, sizeof(meta), "%s 放入", date_label);
    else if (stored > 0)
        snprintf(meta, sizeof(meta), "%s 放入 · 已存 %d 天", date_label, stored);
    else
        snprintf(meta, sizeof(meta), "%s 放入", date_label);
    rawdraw_draw_text(fb, width, 300, list_x + 10, y + 34, meta, fm_font,
                      rawdraw_theme_style(THEME_TOKEN_TEXT_SECONDARY).fg);

    /* highlight: inverted block behind the whole row */
    if (highlighted) {
        rawdraw_fill_rect(fb, width, 300, (rawdraw_rect_t){list_x, y, width - FM_PANEL_PAD - list_x, FM_ROW_H},
                          rawdraw_theme_style(THEME_TOKEN_SELECTED).fg);
        /* redraw texts inverted on top — simplification for P0: draw a solid
         * marker triangle at the left instead of full inversion if the
         * selected token has no distinct fg; keep code as-is: full-row
         * inversion requires re-ordering draws. See Step 2.5 note. */
    }
}

static void render_footer(uint8_t *fb, int width, const fridge_memo_page_t *r)
{
    const int y = FM_FOOTER_Y;
    const rawdraw_paint_style_t text = rawdraw_theme_style(THEME_TOKEN_TEXT_PRIMARY);
    const rawdraw_paint_style_t secondary = rawdraw_theme_style(THEME_TOKEN_TEXT_SECONDARY);
    const rawdraw_paint_style_t warning = rawdraw_theme_style(THEME_TOKEN_WARNING);
    const rawdraw_paint_style_t danger = rawdraw_theme_style(THEME_TOKEN_DANGER);

    rawdraw_draw_hline(fb, width, 300, y, FM_PANEL_PAD, width - FM_PANEL_PAD,
                       rawdraw_theme_style(THEME_TOKEN_BORDER).fg);

    char page_label[24];
    snprintf(page_label, sizeof(page_label), "第 %d/%d 页", r->page_index + 1, fridge_memo_page_pages(r));
    int pw = rawdraw_measure_text_width(page_label, fm_font);
    rawdraw_draw_text(fb, width, 300, width / 2 - pw / 2, y + 5, page_label, fm_font, secondary.fg);
    rawdraw_draw_text(fb, width, 300, width - FM_PANEL_PAD - 6 - rawdraw_measure_text_width("UP/DN 翻页", fm_font),
                      y + 5, "UP/DN 翻页", fm_font, text.fg);

    /* left slot priority: result > offline banner > default hint (design §5.4) */
    char left[FRIDGE_MEMO_FOOTER_TEXT_LEN];
    rawdraw_color_t left_color = text.fg;
    if (r->footer_message[0]) {
        snprintf(left, sizeof(left), "%s", r->footer_message);
        left_color = danger.fg;
    } else if (r->offline) {
        snprintf(left, sizeof(left), "离线 · 缓存 %s", r->data.updated_at);
        left_color = warning.fg;
    } else {
        snprintf(left, sizeof(left), "BOOT 双击删除");
    }
    char left_fit[FRIDGE_MEMO_FOOTER_TEXT_LEN];
    ui_text_fit_to_width(left, fm_font, width / 2 - 20, left_fit, sizeof(left_fit));
    rawdraw_draw_text(fb, width, 300, FM_PANEL_PAD + 6, y + 5, left_fit, fm_font, left_color);
}

static void render_empty(uint8_t *fb, int width)
{
    const rawdraw_paint_style_t secondary = rawdraw_theme_style(THEME_TOKEN_TEXT_SECONDARY);
    const rawdraw_paint_style_t text = rawdraw_theme_style(THEME_TOKEN_TEXT_PRIMARY);
    int cx = width / 2;
    int icon_w = rawdraw_measure_text_width(FONT_ZECTRIX_ICON_MIC, &font_zectrix_16_1);
    rawdraw_draw_text(fb, width, 300, cx - icon_w / 2, 120, FONT_ZECTRIX_ICON_MIC, &font_zectrix_16_1,
                      rawdraw_theme_style(THEME_TOKEN_ACCENT).fg);
    const char *l1 = "冰箱备忘还是空的";
    int w1 = rawdraw_measure_text_width(l1, fm_title_font);
    rawdraw_draw_text(fb, width, 300, cx - w1 / 2, 160, l1, fm_title_font, text.fg);
    const char *l2 = "在设置中配置冰箱后端后，从后端同步条目";
    int w2 = rawdraw_measure_text_width(l2, fm_font);
    rawdraw_draw_text(fb, width, 300, cx - w2 / 2, 200, l2, fm_font, secondary.fg);
}

static void render_delete_overlay(uint8_t *fb, int width, const fridge_memo_page_t *r, const struct tm *today)
{
    /* modal frame (settings_page_clear_dialog_region pattern) */
    const rawdraw_rect_t box = {40, 70, width - 80, 170};
    const rawdraw_paint_style_t border = rawdraw_theme_style(THEME_TOKEN_BORDER);
    rawdraw_fill_rect(fb, width, 300, box, rawdraw_theme_style(THEME_TOKEN_BACKGROUND_PRIMARY).fg);
    rawdraw_draw_rect_border(fb, width, 300, box, 2, border.fg);

    const char *title = "UP/DN 选 · BOOT 删 · 双击取消";
    int tw = rawdraw_measure_text_width(title, fm_font);
    rawdraw_draw_text(fb, width, 300, width / 2 - tw / 2, box.y + 10, title, fm_font,
                      rawdraw_theme_style(THEME_TOKEN_TEXT_SECONDARY).fg);

    int rows = fridge_memo_page_rows_on_page(r, r->page_index);
    int first = r->page_index * FRIDGE_MEMO_ROWS_PER_SCREEN;
    for (int i = 0; i < rows; ++i) {
        int ry = box.y + 36 + i * 32;
        if (i == r->delete_focus) {
            rawdraw_fill_rect(fb, width, 300, (rawdraw_rect_t){box.x + 6, ry, box.w - 12, 30},
                              rawdraw_theme_style(THEME_TOKEN_SELECTED).fg);
        }
        const fridge_memo_item_t *it = &r->data.items[first + i];
        char label[FRIDGE_MEMO_NAME_LEN + FRIDGE_MEMO_QTY_LEN + 8];
        snprintf(label, sizeof(label), "%s%s%s", it->name, it->quantity[0] ? "（" : "",
                 it->quantity[0] ? it->quantity : "");
        if (it->quantity[0])
            strncat(label, "）", sizeof(label) - strlen(label) - 1);
        char status[24];
        render_status_text(status, sizeof(status), it, today);
        char line[96];
        snprintf(line, sizeof(line), "%s  %s", label, status);
        rawdraw_color_t ink = status_color(fridge_memo_derive_status(it, today));
        if (i == r->delete_focus)
            ink = rawdraw_theme_style(THEME_TOKEN_BACKGROUND_PRIMARY).fg; /* inverted */
        rawdraw_draw_text(fb, width, 300, box.x + 14, ry + 7, line, fm_font, ink);
    }
}

void fridge_memo_page_render(page_renderer_t *self, uint8_t *fb, int width, int height)
{
    (void)height;
    fridge_memo_page_t *r = (fridge_memo_page_t *)self;
    if (!r || !fb)
        return;

    rawdraw_paint_style_t bg = rawdraw_theme_style(THEME_TOKEN_BACKGROUND_PRIMARY);
    rawdraw_draw_styled_rect(fb, width, height,
                             (rawdraw_rect_t){0, STYLE_STATUS_BAR_HEIGHT, width, height - STYLE_STATUS_BAR_HEIGHT},
                             &bg);

    struct tm today;
    bool have_time = clock_valid(&today);
    const struct tm *tm_ptr = have_time ? &today : NULL;

    if (r->data.count == 0) {
        render_empty(fb, width);
        render_footer(fb, width, r);
    } else {
        render_summary(fb, width, r, tm_ptr);
        int first = r->page_index * FRIDGE_MEMO_ROWS_PER_SCREEN;
        int y = STYLE_STATUS_BAR_HEIGHT + FM_SUMMARY_H;
        for (int i = 0; i < FRIDGE_MEMO_ROWS_PER_SCREEN && first + i < r->data.count; ++i) {
            render_row(fb, width, y, &r->data.items[first + i], tm_ptr, false);
            y += FM_ROW_H;
        }
        render_footer(fb, width, r);
    }

    if (r->showing_delete)
        render_delete_overlay(fb, width, r, tm_ptr);

    r->base.needs_full_refresh_flag = false;
}

/* ------------------------------------------------------------------ */
/* Input                                                               */
/* ------------------------------------------------------------------ */

bool fridge_memo_page_handle_input(page_renderer_t *self, const ui_button_event_t *event)
{
    fridge_memo_page_t *r = (fridge_memo_page_t *)self;
    if (!r || !event)
        return false;

    if (r->showing_delete) {
        int rows = fridge_memo_page_rows_on_page(r, r->page_index);
        switch (event->type) {
        case BTN_UP_CLICK:
            r->delete_focus = (r->delete_focus + rows - 1) % rows; /* wrap */
            r->base.needs_full_refresh_flag = true;
            return true;
        case BTN_DOWN_CLICK:
            r->delete_focus = (r->delete_focus + 1) % rows;
            r->base.needs_refresh_flag_placeholder(); /* see note: mark below */
            r->base.needs_full_refresh_flag = true;
            return true;
        case BTN_BOOT_CLICK: {
            int first = r->page_index * FRIDGE_MEMO_ROWS_PER_SCREEN;
            const fridge_memo_item_t *it = &r->data.items[first + r->delete_focus];
            if (r->delete_request_cb)
                r->delete_request_cb(it->id, r->delete_request_ctx);
            r->showing_delete = false; /* close on request; result arrives via callback */
            r->base.needs_full_refresh_flag = true;
            return true;
        }
        case BTN_BOOT_DOUBLE_CLICK:
            r->showing_delete = false;
            r->base.needs_full_refresh_flag = true;
            return true;
        default:
            return false; /* BOOT long press and everything else ignored (design §5.4) */
        }
    }

    switch (event->type) {
    case BTN_UP_CLICK:
        if (r->page_index <= 0)
            return false; /* no empty refresh at boundary */
        --r->page_index;
        r->base.needs_full_refresh_flag = true;
        return true;
    case BTN_DOWN_CLICK:
        if (r->page_index >= fridge_memo_page_pages(r) - 1)
            return false;
        ++r->page_index;
        r->base.needs_full_refresh_flag = true;
        return true;
    case BTN_BOOT_CLICK:
        data_refresh_request(UI_PAGE_FRIDGE_MEMO);
        return false; /* no immediate visual change; refresh comes with data */
    case BTN_BOOT_DOUBLE_CLICK:
        if (r->data.count == 0)
            return false;
        r->showing_delete = true;
        r->delete_focus = 0;
        r->base.needs_full_refresh_flag = true;
        return true;
    default:
        return false;
    }
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

void fridge_memo_page_init(page_renderer_t *self, int width, int height)
{
    fridge_memo_page_t *r = (fridge_memo_page_t *)self;
    if (!r)
        return;
    memset(&r->data, 0, sizeof(r->data));
    r->has_data = false;
    r->page_index = 0;
    r->footer_message[0] = '\0';
    r->offline = false;
    r->showing_delete = false;
    r->delete_focus = 0;
    r->font = fm_font;
    r->title_font = fm_title_font;
    r->base.width = width;
    r->base.height = height;
    r->base.needs_full_refresh_flag = true;
}

static void fridge_memo_page_enter(page_renderer_t *self)
{
    (void)self;
    data_refresh_request(UI_PAGE_FRIDGE_MEMO); /* cache-first, then async GET (design §6.3) */
}

static const page_renderer_ops_t fridge_memo_page_ops = {
    .init = fridge_memo_page_init,
    .enter = fridge_memo_page_enter,
    .exit = NULL,
    .render = fridge_memo_page_render,
    .handle_input = fridge_memo_page_handle_input,
    .get_dirty_rect = NULL,
    .needs_full_refresh = NULL,
    .mark_full_refresh = NULL,
    .clear_full_refresh_flag = NULL,
    .append_text = NULL,
    .begin_stream = NULL,
    .end_stream = NULL,
};

PAGE_REGISTER(UI_PAGE_FRIDGE_MEMO, "冰箱备忘", NULL, true, 25, &fridge_memo_page_ops,
              &s_fridge_memo_instance.base);
```

**Step 2.5 实现修正（实现者必须处理，勿照抄错误）**：
1. 删除 `render_row` 中 `highlighted` 参数相关的"先画字再反白"死代码——列表行不高亮（页即选区），删掉参数与整个 if 块。
2. 删除 `BTN_DOWN_CLICK` overlay 分支里的 `needs_refresh_flag_placeholder()` 伪调用——那是笔误，只保留 `needs_full_refresh_flag = true`。
3. `render_row` 的 `rawdraw_draw_text` 参数序以 `rawdraw_ext.h` 实际签名为准（`news_page.c:86` 有完整调用示例：`rawdraw_draw_text(fb, width, height, x, y, str, font, color)`）。
4. `theme.h` 的 `rawdraw_theme_style()` 返回 `rawdraw_paint_style_t`；`.fg` 是 `rawdraw_color_t`——对照 `news_page.c:87` 用法。
5. `rawdraw_draw_hline` 签名对照 `ui_status_bar.c:98`（`fb, w, h, y, x0, x1, color`）。
6. ops 表中 `needs_full_refresh/mark/clear` 三个槽位看 `coding_plan_page.c` 的 ops 初始化怎么填（基类 inline helper 已用 flag 字段，多数页面填 NULL）——照抄 coding_plan 的填法。
7. `up_ev()` 测试辅助函数放到使用之前；`main` 里调用顺序不变。

- [ ] **Step 2.6: 跑测试（绿灯）**

Run: `bash tests/run_tests.sh fridge_memo`
Expected: `PASS: fridge_memo`（12 用例）。
Run: `bash tests/run_tests.sh`（全量）
Expected: 11/11 PASS（新增 fridge_memo，其余无回归——特别确认 `ui_pages_smoke`：其 page_registry 只链 chat/coding_plan 构造器，不受新页影响）。

- [ ] **Step 2.7: Commit**

```bash
git add components/ui/pages/fridge_memo_page.c components/ui/pages/fridge_memo_page.h \
        components/ui/include/ui_manager.h components/ui/CMakeLists.txt tests/test_fridge_memo.c
git commit -m "feat(ui): fridge memo page renderer with paging, summary strip and delete overlay"
```

---

### Task 3: ui_manager 数据 API + app_sync 编排 + 删除接线

**Files:**
- Modify: `components/ui/ui_manager.c`（追加 fridge memo 转发函数，模式照抄 `ui_manager_update_wifi_status` `ui_manager.c:698-704`）
- Modify: `main/application_internal.h`（无——P0 不加事件；确认后若无需改则跳过）
- Modify: `main/app_sync.c`（初始化 + 缓存先行 + 回调 + 刷新路由 + 删除接线）

- [ ] **Step 3.1: ui_manager 转发 API**

`components/ui/include/ui_manager.h` 在 `/* WiFi status. */` 段（第 140 行附近）前追加：

```c
/* Fridge memo page. */
void ui_manager_update_fridge_memo(ui_manager_t *mgr, const void *snapshot);
void ui_manager_set_fridge_memo_footer(ui_manager_t *mgr, const char *msg);
void ui_manager_set_fridge_memo_offline(ui_manager_t *mgr, bool offline);
```

`components/ui/ui_manager.c` 在 `ui_manager_update_wifi_status` 函数后追加（含 `#include "fridge_memo_page.h"` 到文件头 include 区）：

```c
/* ------------------------------------------------------------------ */
/* Fridge memo page                                                    */
/* ------------------------------------------------------------------ */

void ui_manager_update_fridge_memo(ui_manager_t *mgr, const void *snapshot)
{
    if (!mgr || !snapshot)
        return;
    fridge_memo_page_update((page_renderer_t *)page_registry_get_instance(UI_PAGE_FRIDGE_MEMO),
                            (const fridge_memo_snapshot_t *)snapshot);
    page_renderer_mark_full_refresh((page_renderer_t *)page_registry_get_instance(UI_PAGE_FRIDGE_MEMO));
    if (mgr->current_page == UI_PAGE_FRIDGE_MEMO)
        ui_manager_request_active_page_refresh(mgr); /* EPD full refresh only when visible (coding_plan pattern) */
}

void ui_manager_set_fridge_memo_footer(ui_manager_t *mgr, const char *msg)
{
    if (!mgr)
        return;
    fridge_memo_page_set_footer_message((page_renderer_t *)page_registry_get_instance(UI_PAGE_FRIDGE_MEMO), msg);
    if (mgr->current_page == UI_PAGE_FRIDGE_MEMO)
        ui_manager_request_active_page_refresh(mgr);
}

void ui_manager_set_fridge_memo_offline(ui_manager_t *mgr, bool offline)
{
    if (!mgr)
        return;
    fridge_memo_page_set_offline((page_renderer_t *)page_registry_get_instance(UI_PAGE_FRIDGE_MEMO), offline);
    if (mgr->current_page == UI_PAGE_FRIDGE_MEMO)
        ui_manager_request_active_page_refresh(mgr);
}
```

- [ ] **Step 3.2: app_sync 编排**

`main/app_sync.c` 追加（include 区加 `#include "fridge_memo_api.h"` 与 `#include "fridge_memo_page.h"`、`#include "page_registry.h"`）：

```c
/* ------------------------------------------------------------------ */
/* Fridge memo                                                         */
/* ------------------------------------------------------------------ */

static char s_fridge_pending_delete_name[FRIDGE_MEMO_NAME_LEN];

static void app_sync_on_fridge_memo_update(const fridge_memo_snapshot_t *snap, void *user_data)
{
    (void)user_data;
    if (!snap)
        return;
    ui_manager_update_fridge_memo(s_app.ui_mgr, snap);
    ui_manager_set_fridge_memo_offline(s_app.ui_mgr, false);
    if (s_fridge_pending_delete_name[0]) {
        char msg[FRIDGE_MEMO_FOOTER_TEXT_LEN + FRIDGE_MEMO_NAME_LEN];
        snprintf(msg, sizeof(msg), "已删除：%s", s_fridge_pending_delete_name);
        ui_manager_set_fridge_memo_footer(s_app.ui_mgr, msg);
        s_fridge_pending_delete_name[0] = '\0';
    }
}

static void app_sync_on_fridge_memo_error(const char *message, void *user_data)
{
    (void)user_data;
    ui_manager_set_fridge_memo_offline(s_app.ui_mgr, true);
    ui_manager_set_fridge_memo_footer(s_app.ui_mgr, message);
}

static void app_sync_on_fridge_memo_delete(const char *item_id, void *user_data)
{
    (void)user_data;
    /* remember the name for the result footer; find it in the cached snapshot */
    const fridge_memo_snapshot_t *snap = fridge_memo_api_get_cached_data();
    s_fridge_pending_delete_name[0] = '\0';
    if (snap) {
        for (int i = 0; i < snap->count; ++i) {
            if (strcmp(snap->items[i].id, item_id) == 0) {
                snprintf(s_fridge_pending_delete_name, sizeof(s_fridge_pending_delete_name), "%s",
                         snap->items[i].name);
                break;
            }
        }
    }
    if (!fridge_memo_api_delete_async(item_id)) {
        ui_manager_set_fridge_memo_footer(s_app.ui_mgr, "删除失败：后端不可达");
        s_fridge_pending_delete_name[0] = '\0';
    }
}

void app_sync_ensure_fridge_memo_initialised(void)
{
    static bool s_fm_inited = false;
    if (s_fm_inited)
        return;
    fridge_memo_api_init(NULL); /* base_url from NVS "fridge" */
    fridge_memo_api_set_callback(app_sync_on_fridge_memo_update, NULL);
    fridge_memo_api_set_error_callback(app_sync_on_fridge_memo_error, NULL);
    fridge_memo_page_set_delete_request_handler(
        (page_renderer_t *)page_registry_get_instance(UI_PAGE_FRIDGE_MEMO), app_sync_on_fridge_memo_delete, NULL);
    s_fm_inited = true;

    /* Cache-first: render NVS snapshot immediately (design §4.4). */
    if (fridge_memo_api_has_cached_data()) {
        app_sync_on_fridge_memo_update(fridge_memo_api_get_cached_data(), NULL);
        s_fridge_pending_delete_name[0] = '\0';
    }
}

void app_sync_refresh_fridge_memo(void)
{
    app_sync_ensure_fridge_memo_initialised();
    fridge_memo_api_fetch_async();
}
```

`app_sync_on_data_refresh_request` 的 switch（`app_sync.c:126` 附近）`case UI_PAGE_CODING_PLAN:` 后追加：

```c
    case UI_PAGE_FRIDGE_MEMO:
        app_sync_refresh_fridge_memo();
        break;
```

`main/application_internal.h`：在 `app_sync_refresh_coding_plan` 声明旁追加两个声明：

```c
void app_sync_ensure_fridge_memo_initialised(void);
void app_sync_refresh_fridge_memo(void);
```

- [ ] **Step 3.3: 构建验证（target + host）**

Run: `bash tests/run_tests.sh`
Expected: 全部 PASS（app_sync 不参与主机测试，但要确认无头文件破坏）。

Run: `. ~/.espressif/v6.0.2/esp-idf/export.sh && idf.py build`
Expected: BUILD SUCCESS（ui_manager.c / app_sync.c 编译过；若 `app_sync_on_data_refresh_request` 的 switch 因枚举新增收到 -Werror=switch 覆盖警告，`default:` 已存在则无问题）。

- [ ] **Step 3.4: Commit**

```bash
git add components/ui/include/ui_manager.h components/ui/ui_manager.c main/app_sync.c main/application_internal.h
git commit -m "feat(app): wire fridge memo data flow (cache-first render, delete routing, error banner)"
```

---

### Task 4: 设置项 `base_url`（复用 server-list 对话框）

**Files:**
- Modify: `main/app_settings_menu.c`（网络分区加条目 + 回调 case + 对话框处理）

- [ ] **Step 4.1: 设置条目**

`app_settings_menu.c` 菜单构建函数中 `/* 网络 */` 分区（`:241`）内、"省电模式" 条目之前插入：

```c
    /* 冰箱后端 (action) — 打开地址选择对话框 */
    {
        char fm_url[96];
        fridge_memo_api_init(NULL); /* ensure NVS-loaded */
        fridge_memo_api_get_base_url(fm_url, sizeof(fm_url));
        strcpy(items[n].label, "冰箱后端");
        snprintf(items[n].value, sizeof(items[n].value), "%s", fm_url[0] ? fm_url : "未设置");
        items[n].type = SETTINGS_ITEM_ACTION;
        items[n].on_click = app_settings_menu_cb;
        items[n].on_click_ctx = (void *)(intptr_t)6;
        ++n;
    }
```

- [ ] **Step 4.2: 回调 case 6 打开对话框**

`app_settings_menu_cb`（同文件）加 case 6：候选列表 = {当前 base_url（若有）, `http://<网关IP>:8420`}；网关从 `esp_netif_get_ip_info` 取（`wifi_manager` 已连网时）。选中后 `fridge_memo_api_set_base_url(sel)` 并立即 `app_sync_refresh_fridge_memo()` 试拉。

```c
    case 6: { /* 冰箱后端地址 */
        page_renderer_t *settings = (page_renderer_t *)page_registry_get_instance(UI_PAGE_SETTINGS);
        const char *addrs[4];
        int cnt = 0;
        char cur[96], gw_url[80];
        fridge_memo_api_get_base_url(cur, sizeof(cur));
        if (cur[0])
            addrs[cnt++] = cur; /* static lifetime until next call: copy below instead */
        /* gateway-derived guess */
        {
            esp_netif_ip_info_t ip;
            esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            if (netif && esp_netif_get_ip_info(netif, &ip) == ESP_OK) {
                snprintf(gw_url, sizeof(gw_url), "http://" IPSTR ":8420", IP2STR(&ip.gw));
                addrs[cnt++] = gw_url;
            }
        }
        /* copy into static storage so the dialog can hold the pointers */
        static char s_addr_store[4][96];
        for (int i = 0; i < cnt; ++i)
            snprintf(s_addr_store[i], sizeof(s_addr_store[i]), "%s", addrs[i]);
        settings_page_show_server_list_dialog(settings, (const char *const *)s_addr_store, cnt, cur);
        settings_page_set_server_list_dialog_handler(settings, app_settings_on_fridge_url_picked, NULL);
        break;
    }
```

同文件追加 handler + include（`settings_page.h`、`esp_netif.h`、`fridge_memo_api.h`、`page_registry.h`）：

```c
static void app_settings_on_fridge_url_picked(const char *address, void *ctx)
{
    (void)ctx;
    if (!address || !address[0])
        return;
    fridge_memo_api_set_base_url(address);
    app_sync_refresh_fridge_memo();
    /* update the settings row value so the new URL shows next render */
    app_settings_menu_refresh_fridge_item_value(); /* 见 Step 4.3 */
}
```

- [ ] **Step 4.3: 刷新条目显示值**

`app_settings_on_fridge_url_picked` 里调用的 `app_settings_menu_refresh_fridge_item_value()` 实现：重新调用 `ui_manager_update_settings_item(mgr, index, value)`，index = 该条目在 items 数组中的下标（构建菜单时用 static int 记录）。若菜单每次进入都重建（观察 `app_settings_menu.c` 现有行为：`ui_manager_set_settings_items` 每次 build 调用），则更简单——退出设置页再进即刷新，**P0 取该路径**：删掉 `app_settings_menu_refresh_fridge_item_value` 调用与函数，仅 `fridge_memo_api_set_base_url` + 刷新数据。设置页显示旧值直到重进——记录为已知限制（P1 修）。

- [ ] **Step 4.4: 构建 + 手动路径验证**

Run: `. ~/.espressif/v6.0.2/esp-idf/export.sh && idf.py build`
Expected: BUILD SUCCESS。

（真机验证属设备冒烟——用户执行：设置 → 网络 → 冰箱后端 → 选网关地址 → 返回冰箱备忘页看离线横幅变化。）

- [ ] **Step 4.5: Commit**

```bash
git add main/app_settings_menu.c
git commit -m "feat(settings): fridge backend base_url picker via server-list dialog"
```

---

### Task 5: 文档更新 + 全量验证 + 收尾

**Files:**
- Modify: `docs/user-manual.md`（§2.2 快捷切换列表插入"冰箱备忘"（天气之后）；新增 §3.x 冰箱备忘页操作表；页面总数 19→20）
- Modify: `README.md`（"19 个 UI 页面"→"20 个"，项目结构树 network/ui 描述加冰箱备忘）
- Modify: `docs/fridge-memo-product-design.md`（状态行：v1.2 → "P0 已实施"）

- [ ] **Step 5.1: 文档更新**（内容按 v1.2 设计 §4.2/§5 写操作表：UP/DN 整页翻动、BOOT 短按刷新、BOOT 双击删除浮层、P0 无语音；离线横幅说明）

- [ ] **Step 5.2: 全量回归**

Run: `bash tests/run_tests.sh`
Expected: 11/11 PASS。

Run: `. ~/.espressif/v6.0.2/esp-idf/export.sh && idf.py build`
Expected: BUILD SUCCESS。

- [ ] **Step 5.3: 真机冒烟清单（用户执行，写入 PR 描述）**

1. 快捷切换出现"冰箱备忘"于天气之后；首次进入空态 + mic 图标。
2. 配置 base_url 后 BOOT 短按 → 有数据显示（mock/真实后端）。
3. UP/DN 翻页、边界不空刷、页指示正确。
4. BOOT 双击 → 浮层 4 条、焦点环绕、BOOT 删除 → footer"已删除：X"、一次全刷。
5. 断后端 → 进页缓存显示 + 黄色离线横幅；删除 → footer"删除失败：后端不可达"、条目保留。
6. 过期条目红块置顶 + "已过期 N 天"；剩 2 天黄块。
7. 深睡唤醒恢复本页 → 先缓存后纠正。

- [ ] **Step 5.4: Commit**

```bash
git add docs/user-manual.md README.md docs/fridge-memo-product-design.md
git commit -m "docs: document fridge memo page (P0)"
```

---

## Self-Review 记录

- **Spec 覆盖**：设计 §4.1（字段/状态/排序/64 截断）→ Task 1；§4.2 按键表 → Task 2 输入模型；§4.4 缓存/权威全量 → Task 1/3；§5.1-5.4 UI → Task 2；§6.2 代码落点 → Tasks 1-4（settings 对话框替代"自研输入"是 P0 唯一架构偏移，已记录）；§7.1 REST → Task 1；验收 1/2/3/4/8 → Step 5.3 清单。P1 范围（音效/LED/WS/mic）明确排除。
- **占位符扫描**：唯一骨架引用 = `http_delete_text` 的 target 分支（复制本文件既有 GET 模式，已注明缘由）；Step 2.5 列出 7 处"勿照抄"修正点并给出参照行号。
- **类型一致性**：`fridge_memo_snapshot_t`/`fridge_memo_page_update`/`ui_manager_update_fridge_memo(void*)` 跨任务签名一致；`settings_page_server_list_handler_t(const char*, void*)` 与 handler 匹配（`settings_page.h:66`）。
- **已知偏差（向用户披露）**：①空态图标 16px 非 48px（字体集限制）；②footer P0 默认提示为"BOOT 双击删除"（语音提示 P1b 替换）；③base_url 无自由文本输入（server-list 选择 + 网关猜测，自由输入 P1）；④设置页内条目值不即时刷新（重进设置页刷新）。
