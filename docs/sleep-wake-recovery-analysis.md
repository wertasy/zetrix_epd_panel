# 深度睡眠唤醒恢复路径分析

> 评审问题：当前深睡眠后恢复时，系统走的路径和正常上电是一样的吗？是否需要在 pages 框架上做些改动来建立正确的睡眠恢复机制？
> 日期：2026-08-11

---

## 结论速览

**是的，深睡眠唤醒与正常上电走完全相同的路径**——两者都从 `app_main()` 开始完整初始化。这在 ESP32 深度睡眠模型下是必然的（深度睡眠 wipes RAM，`esp_deep_sleep_start()` 后唯一出路就是复位重入 `app_main`），**pages 框架本身不需要结构性改动**。

但当前实现存在一个 **致命的前置缺陷** 和三个恢复语义缺陷：

- **致命**：`nvs_state` 模块在生产代码中从未被初始化（`nvs_state_init()` 无调用方），导致所有 `nvs_state_get/set_*` 调用静默失败——不仅是 `last_page`，连主题持久化（`ui_manager.c:699/1191`）也是空操作。
- 导航状态：`current_page` 硬编码为 `UI_PAGE_CODING_PLAN`，唤醒后回到首页。
- 页面内状态：日历导航月份、电子书阅读位置等在深睡后丢失。
- 网络数据缓存全部丢失后没有懒加载机制。

这些不需要改 pages 框架的 vtable 结构，而是在 `application_init` 和各页面的 `init()` 中补充恢复逻辑。

---

## 一、路径对比：冷启动 vs 深睡唤醒

### ESP32 深度睡眠的硬件事实

`esp_deep_sleep_start()` 关闭 CPU 和大部分外设，仅保留 RTC 和 ULP 协处理器运行。RAM 内容全部丢失。唤醒时芯片执行 **完整复位**——`app_main()` 从头开始，与上电冷启动无法区分，只能通过 `esp_sleep_get_wakeup_cause()` 区分唤醒源。

因此"两条路径是否相同"的答案是：**必然相同**，这是芯片行为决定的，不是设计选择。

### 实际代码路径追踪

```
┌─────────────────────────────────────────────────────────────────┐
│                    app_main() [main.c:193]                       │
│  （冷启动和深睡唤醒唯一入口，无任何分支）                           │
│                                                                  │
│  1. nvs_flash_init()          ← NVS Flash 初始化                 │
│  2. charge_status_init()                                        │
│  3. board_init()              ← I2C/SPI/外设全量初始化             │
│  4. board_power_{vbat,audio,epd}_on()  ← 电源轨拉高               │
│  5. pcf8563_init() + 时间同步                                   │
│  6. nfc_init(), bluetooth_manager_init(), audio_player_init()   │
│  7. wifi_manager_init()                                         │
│  │                                                              │
│  │  ┌───────────────────────────────────────────────────┐       │
│  │  │ esp_sleep_get_wakeup_cause() 检查 [main.c:273]     │       │
│  │  │                                                     │       │
│  │  │ EXT1 + RTC_INT + slideshow>0:                       │       │
│  │  │   → is_rtc_slideshow_wakeup = true                  │       │
│  │  │   → 跳过 WiFi 连接 [main.c:290]                     │       │
│  │  │                                                     │       │
│  │  │ 其他所有情况:                                        │       │
│  │  │   → 正常 WiFi 连接                                  │       │
│  │  └───────────────────────────────────────────────────┘       │
│  │                                                              │
│  8. custom_lcd_display_init()  ← EPD 重新初始化                  │
│  9. 按钮重新注册                                                │
│ 10. application_init()        ← 见下方详细分析                    │
│ 11. render_ui_and_refresh(true) ← 全屏刷新                      │
│ 12. xTaskCreate(application_main_task)                          │
└─────────────────────────────────────────────────────────────────┘
```

> **注意：** `app_main()` 在步骤 1 调用的是 `nvs_flash_init()`（ESP-IDF 底层 Flash 分区初始化），不是 `nvs_state_init()`（模块自己的句柄打开）。这是后文致命缺陷的关键区分点。

**唯一的分叉点**在 `main.c:271-288`：RTC 幻灯片唤醒时跳过 WiFi 连接以省电。除此之外，初始化序列完全一致。

### `application_init()` 中的唤醒区分逻辑

```c
// application.c:821-911
void application_init(void)
{
    memset(&s_app, 0, sizeof(s_app));      // ← application 状态清零
    // ...
    ui_manager_init(s_app.ui_mgr, NULL, NULL);  // ← UI 全量初始化

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    bool is_rtc_wakeup = (cause == ESP_SLEEP_WAKEUP_EXT1)
                      && (pin_mask & (1ULL << RTC_INT_GPIO));

    if (is_rtc_wakeup && slideshow_interval > 0) {
        // 幻灯片唤醒：直接跳到相册全屏，恢复 NVS 中的照片索引
        ui_manager_switch_page(s_app.ui_mgr, UI_PAGE_GALLERY);
        // ...恢复 saved_idx（走 settings_open，而非 nvs_state）...
        photo_gallery_select_next(gallery, true);
    }

    if (!(is_rtc_wakeup && slideshow_interval > 0)) {
        if (esp_reset_reason() == ESP_RST_DEEPSLEEP) {
            // 深睡唤醒：闪一下 LED，请求刷新当前页
            board_flash_activity_led();
            ui_manager_request_active_page_refresh(s_app.ui_mgr);
        }
    }
}
```

**当前唤醒处理仅覆盖了两种场景**：RTC 幻灯片唤醒（有专门逻辑）和普通深睡唤醒（仅闪 LED + 刷新）。但 BOOT 按钮唤醒（用户主动唤醒）走的是完全相同的路径，且恢复逻辑不足。

---

## 二、致命前置缺陷：`nvs_state` 模块从未初始化

### 问题

`nvs_state` 模块使用一个**共享 NVS 句柄** `s_handle`（`nvs_state.c:31`）：

```c
static nvs_handle_t s_handle = 0;  // ← 初始化为 0（无效）
```

所有底层 `kv_get_i32` / `kv_set_i32` 调用（`nvs_state.c:70-81`）都使用这个句柄：

```c
static bool kv_get_i32(const char *key, int32_t *out)
{
    return nvs_get_i32(s_handle, key, out) == ESP_OK;  // s_handle == 0 → 失败
}
```

`s_handle` **仅在** `nvs_state_init()` 中通过 `nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_handle)` 赋值（`nvs_state.c:451`）。

**全局搜索确认：`nvs_state_init()` 在生产代码中无任何调用方**——仅出现在模块自身的定义处和 `tests/test_nvs_state.c` 中。

### 后果链

```
nvs_state_init() 从未被调用
  → s_handle 永远为 0（无效句柄）
  → kv_get_i32/kv_set_i32 全部失败（返回 ESP_ERR_NVS_INVALID_HANDLE）
  → nvs_state_get_i32/set_i32 静默返回 false
  → 所有依赖 nvs_state 的持久化均为空操作
```

**受影响的现有代码（不仅是 `last_page`）：**

| 调用点 | 功能 | 当前状态 |
|---|---|---|
| `ui_manager.c:699` `nvs_state_get_string(RAWDRAW_THEME_NVS_KEY, ...)` | 主题恢复 | ❌ 空操作，每次启动回到默认 "industrial" |
| `ui_manager.c:1191` `nvs_state_set_string(RAWDRAW_THEME_NVS_KEY, ...)` | 主题保存 | ❌ 空操作 |
| `nvs_state.c:250` `kv_get_i32(K_UI_PAGE, ...)` | `last_page` 读取 | ❌ 永远失败 |
| `nvs_state.c:274` `kv_set_i32(K_UI_PAGE, ...)` | `last_page` 写入 | ❌ 永远失败 |

### 项目中的两套 NVS 访问路径

项目中存在两条完全独立的 NVS 访问路径：

| 路径 | 模块 | 句柄管理 | 生产可用 | 示例调用 |
|---|---|---|---|---|
| `settings_open/get/set` | `settings.c` | 每次调用 open→use→close | ✅ **可用** | `main.c:243`, `application.c:644` |
| `nvs_state_get/set_*` | `nvs_state.c` | 共享 `s_handle`，需 `nvs_state_init()` | ❌ **不可用** | `ui_manager.c:699` |

项目唯一正确工作的持久化恢复——相册幻灯片位置——走的是 `settings.c` 路径（`application.c:644`）：

```c
settings_handle_t gnvs = settings_open(APP_GALLERY_NS, true);
if (gnvs) {
    settings_set_int(gnvs, "current_idx", idx);
    settings_close(gnvs);
}
```

---

## 三、`ui_manager_init` 的全量重建问题

### 问题：`current_page` 硬编码为 `UI_PAGE_CODING_PLAN`

```c
// ui_manager.c:687
mgr->current_page = UI_PAGE_CODING_PLAN;  // ← 每次启动都从这里开始
```

用户睡前在看日历页，深睡唤醒后永远回到 Coding Plan 页——这不是"恢复"，是"重置"。

### 页面状态丢失的真实机制

`struct ui_manager`（`ui_manager.c:67-107`）**不包含**任何页面结构体成员——它只有 manager 自身的字段（current_page、status_bar、timer handles 等）。页面实例是各自文件中的**独立静态全局变量**，分配在 PSRAM（`EXT_RAM_BSS_ATTR`）：

```c
// chat_page.c
static EXT_RAM_BSS_ATTR chat_page_t s_chat_instance;
// calendar_page.c
static EXT_RAM_BSS_ATTR calendar_page_t s_calendar_instance;
```

状态丢失的真实链条是：
1. 深度睡眠清除全部 RAM（含 PSRAM）→ 页面静态实例被清零
2. 唤醒后 GCC constructor 重新注册页面（`page_registry_add`）
3. `ui_manager_init`（`ui_manager.c:682`）执行 `memset(mgr, 0, sizeof(*mgr))` 清零 manager 自身
4. `ui_manager_init`（`ui_manager.c:703-705`）对每个页面调用 `init_renderer` → 重置为默认值

`memset` 清零的是 manager 结构体（current_page、status_bar 等），不直接清零页面结构体。页面状态丢失发生在步骤 1（硬件清零）和步骤 4（init 重置）。

### 页面 init 的无条件重置

各页面的 `init()` 实现策略不一致：

| 页面 | init 行为 | 状态保护 |
|---|---|---|
| `settings_page` (`:325`) | `if (r->item_count == 0)` 才重置 selected_index | ✅ 运行期有效，深睡无效 |
| `photo_gallery` (`:458`) | `first_init = (photo_count == 0 && mode == 0)` | ✅ 运行期有效，深睡无效 |
| `calendar_page` (`:217-222`) | `r->year = today_year; selected_date = {0}` | ❌ **丢失导航月份** |
| `ebook_page` (`:67-77`) | `reader_mode = false; reader_content = ""; current_page = 0` | ❌ **丢失阅读位置** |
| `chat_page` | `message_count = 0` | ❌ **丢失聊天记录** |

由于深睡后 RAM 被清空，这些 `init()` 函数看到的都是零初始化的内存，`first_init` 判断恒为 true——所以"保护"逻辑在深睡唤醒场景下无效，只是保护了运行期页面切换时的状态。

---

## 四、具体的恢复缺陷

### D1. 导航位置丢失

**现象：** 用户在任何页面睡着 → 唤醒后回到 Coding Plan 首页。

**根因：**
- `current_page` 硬编码为 `UI_PAGE_CODING_PLAN`（`ui_manager.c:687`）
- `ui_manager_switch_page` 切换页面时不持久化当前页码
- `ui_manager_init` 初始化时不恢复上次页码
- `enter_scheduled_sleep` 睡前不保存当前页码
- `nvs_state` 模块虽然定义了 `last_page` 字段，但整个模块从未被初始化（见第二节）

**影响：** 用户体验断裂。用户设置好一个页面（如天气详情）后放下设备，30 分钟后唤醒发现回到首页。

### D2. 页面内导航状态丢失

**现象：**
- 日历页：用户翻到 3 个月后 → 唤醒回到当前月
- 电子书：读到第 50 页 → 唤醒回到文件列表
- 聊天页：有 20 条对话 → 唤醒后清空
- 设置页：滚动到第 15 项 → 唤醒回到顶部

**根因：** 页面的 `init()` 无条件重置运行时状态，且没有从持久化存储恢复页面内状态的机制。

**最严重的是电子书阅读位置**——用户读到一半，唤醒后丢失。

### D3. 网络数据缓存全丢

**现象：** 天气预报、新闻列表、编程计划用量——这些运行时从网络获取并缓存在页面结构体中的数据，深睡后全部丢失。唤醒后页面空白，直到网络重新连接并重新拉取。

**根因：** 数据缓存在 RAM 中（如 `weather_page_t` 中的预报数组），无持久化。而 RTC 幻灯片唤醒场景明确跳过 WiFi 连接（`main.c:284-286`），意味着幻灯片唤醒后所有网络数据页面都无法恢复。

---

## 五、修复方案

### Pages 框架（vtable 结构）：不需要改

当前 `page_renderer_ops_t` 已经有 `init` + `enter` 的两阶段生命周期：
- `init`：一次性初始化（尺寸、字体、默认值）
- `enter`：页面获得焦点时调用（请求刷新、保持导航状态）

这个设计 **足以支撑睡眠恢复**——只需要在 `init` 中区分"首次初始化"和"唤醒恢复"即可。不需要新增 vtable 槽位（如 `restore`/`wake`）。

### 前置修复：选择持久化路径

当前有两条可选路径，必须先确定用哪条：

**选择 A（最小改动）：** 在 `main.c:202`（`nvs_flash_init` 之后）添加一行 `nvs_state_init()`，激活整个 `nvs_state` 模块：

```c
ESP_ERROR_CHECK(ret);
nvs_state_init();  // ← 激活 nvs_state 的共享句柄，让所有 get/set 生效
```

一行代码修复系统性静默失败——同时让 `last_page` 恢复和主题持久化全部生效。

**选择 B（更一致）：** 将所有持久化统一到 `settings.c` 路径（与现有相册位置恢复一致），逐步废弃 `nvs_state` 模块。改动范围较大。

**推荐选择 A 作为即时修复**，后续可逐步迁移到 B。

### P0：恢复当前页面（3 种方案，按推荐度排序）

#### 方案 1：RTC_NOINIT_ATTR（推荐——零 Flash 磨损、零延迟）

对于"恢复上次看的页面"这个场景，RTC 内存是更优选择：
- 页面切换是高频操作，用 Flash 会产生不必要的写入磨损
- 唤醒时读取零延迟，不需要等待 NVS 打开
- magic number 验证只需 4 行代码，对 page ID（枚举值）足够可靠

```c
// ui_manager.c — 新增
static RTC_NOINIT_ATTR uint32_t s_rtc_last_page;
static RTC_NOINIT_ATTR uint32_t s_rtc_magic;
#define WAKE_MAGIC 0xDEAD5AA5

// switch_page 时写入
void ui_manager_switch_page(ui_manager_t *mgr, ui_page_id_t page)
{
    // ...existing code...
    mgr->current_page = page;
    s_rtc_last_page = (uint32_t)page;
    s_rtc_magic = WAKE_MAGIC;
}

// init 时恢复
void ui_manager_init(...)
{
    // ...existing code...
    if (s_rtc_magic == WAKE_MAGIC && s_rtc_last_page < UI_PAGE_COUNT) {
        mgr->current_page = (ui_page_id_t)s_rtc_last_page;
    }
    // else 保持默认 UI_PAGE_CODING_PLAN
}
```

冷启动时 magic 不匹配 → 回退到默认页面，安全。OTA 后只要不重排枚举就不会出问题。

#### 方案 2：`settings.c`（跨冷启动/OTA 持久化）

如果需要跨冷启动保留（用户换电池后仍恢复上次页面），用 `settings.c`：

```c
// switch_page 时保存
settings_handle_t h = settings_open("ui_nav", true);
if (h) { settings_set_int(h, "last_page", (int32_t)page); settings_close(h); }

// init 时恢复
settings_handle_t h = settings_open("ui_nav", false);
if (h) {
    int32_t saved = settings_get_int(h, "last_page", (int32_t)UI_PAGE_CODING_PLAN);
    settings_close(h);
    mgr->current_page = (ui_page_id_t)saved;
}
```

#### 方案 3：`nvs_state`（前置选择 A 后可用）

```c
// switch_page 时保存
nvs_state_set_i32("ui_page", (int32_t)page);

// init 时恢复
int32_t saved = 0;
if (nvs_state_get_i32("ui_page", &saved) && saved >= 0 && saved < UI_PAGE_COUNT)
    mgr->current_page = (ui_page_id_t)saved;
```

**注意：** 必须先执行前置修复（添加 `nvs_state_init()` 调用），否则此方案是空操作。

> **所有方案：** RTC 幻灯片唤醒应覆盖此恢复（直接跳到 GALLERY），`application_init` 已有此逻辑（`:860-882`），只需确保在 `ui_manager_init` 之后执行。

### P1：关键页面增加状态恢复

只有用户长期交互的页面需要恢复页面内状态：

| 页面 | 需要恢复的状态 | 存储位置 |
|---|---|---|
| 电子书 | 文件名 + 当前页码 | NVS `ebook_reader_file` + `ebook_reader_page` |
| 日历 | 导航到的年/月 | `nvs_state.calendar.selected_year/month`（前置修复后可用）或 `settings.c` |
| 聊天 | 消息历史 | 不持久化（聊天记录是临时会话，冷启动清空是合理行为） |

日历的恢复（前置修复后 `nvs_state` 可用）：

```c
void calendar_page_init(page_renderer_t *self, int width, int height)
{
    // ...existing code...
    int32_t y = 0, m = 0;
    if (nvs_state_get_i32("cal_year", &y) && nvs_state_get_i32("cal_month", &m)) {
        if (y >= 2020 && y <= 2050 && m >= 1 && m <= 12) {
            r->year = y;    // 恢复导航月份
            r->month = m;
        }
    }
}
```

### P2：网络数据懒加载（不改框架，改数据流）

当前网络数据在 `application_init` 中通过 WiFi 连接回调异步拉取。RTC 幻灯片唤醒跳过 WiFi（`main.c:284`），但这些页面不可达（自动跳到 GALLERY 全屏），所以不是问题。

真正的问题是普通深睡唤醒后 WiFi 重连 + 重新拉取数据期间（可能 5-10 秒），页面显示空白。**解法是在页面 init 中先从 NVS 加载上次的缓存数据**，网络刷新完成后覆盖。天气页面已经部分做到了（`weather_api.c` 有 NVS 缓存），但新闻和编程计划没有。

---

## 六、不需要引入的机制

### 不需要：`suspend`/`resume` vtable 槽位

在 `page_renderer_ops_t` 中新增 `void (*suspend)(page_renderer_t*)` 和 `void (*resume)(page_renderer_t*)` 是过度设计：

1. 深睡前 RAM 被清空，`suspend` 保存的状态无处存放（除非写 NVS，但那和直接在 init 中读 NVS 没区别）
2. 唤醒后 `init` + `enter` 已经覆盖了 `resume` 的职责
3. 增加了 19 个页面都要实现的虚函数，复杂度收益比差

内核的 `suspend`/`resume` 回调适用于 STR（Suspend to RAM）——RAM 内容保留。ESP32 深睡不保留 RAM，模型完全不同。

### 不需要：全局睡眠/唤醒事件广播

不需要设计一个 `APP_EVENT_WAKEUP` 事件让所有页面响应。唤醒后 `app_main` 重新初始化一切，等同于"所有页面都是新创建的"。唯一需要区分的是 RTC 幻灯片唤醒 vs 其他唤醒——这个区分已在 `application_init` 中完成。

---

## 七、总结

### 修复优先级

| 问题 | 严重度 | 根因 | 解法 | 改动范围 |
|---|---|---|---|---|
| `nvs_state` 模块未初始化 | **致命** | `nvs_state_init()` 无调用方 | `main.c` 添加 1 行调用 | `main.c` 1 处 |
| 唤醒后回到首页 | 高 | `current_page` 硬编码 + 无持久化 | RTC_NOINIT_ATTR 或 `settings.c` | `ui_manager.c` 2 处 |
| 主题持久化失效 | 高 | 同前置缺陷 | 同前置修复（`nvs_state_init`） | 0（自动生效） |
| 日历导航月份丢失 | 中 | `init` 无条件重置为当月 | `init` 中恢复（前置修复后） | `calendar_page.c` 1 处 |
| 电子书阅读位置丢失 | 中 | 阅读状态纯 RAM | `settings.c` 保存文件名+页码 | `ebook_page.c` + `application.c` |
| 网络数据缓存丢失 | 低 | 数据在 RAM，深睡清零 | 页面 init 先加载 NVS 缓存 | 各页面 init |

### 核心判断

1. **pages 框架的 vtable 结构不需要改动。** `init` + `enter` 的两阶段生命周期已经足够。
2. **前置缺陷（`nvs_state_init` 未调用）是最高优先修复项**——一行代码修复系统性的静默失败，同时让主题持久化和 `last_page` 持久化基础设施生效。
3. **持久化方案选择**：对页面 ID 等高频写入的瞬态状态，优先用 RTC_NOINIT_ATTR（零 Flash 磨损）；对需要跨冷启动/OTA 持久化的状态，用 `settings.c`。
4. 改动总量约 30-50 行代码（前置修复 1 行 + P0 约 10 行 + P1 约 20 行）。
