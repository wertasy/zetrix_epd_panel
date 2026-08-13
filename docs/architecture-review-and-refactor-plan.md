# 架构评审与重构计划

> 评审范围：`components/`（bsp / rawdraw / network / audio / ui / 78__xiaozhi-fonts）、`main/`
> 目标平台：ESP32-S3，ESP-IDF 6.0.2
> 第三方依赖：lvgl 9.3、espressif/button、esp_codec_dev、esp_websocket_client、cjson、qrcode、littlefs
> 代码总量：约 35,680 行（C/H）

---

## 1. 模块划分与代码分布

### 1.1 组件清单与体量

| 组件 | 行数 | 职责 |
|------|------|------|
| `components/ui` | 14,216 | 页面渲染（19 页）+ ui_manager + page_registry + ap_transfer_server |
| `components/rawdraw` | 8,888 | 图形原语 + 主题 + 布局 + 15 个 widget |
| `components/network` | 5,235 | 天气/编程计划/节假日 API + 图片下载/存储 + 蓝牙(BLE) |
| `components/bsp` | 4,579 | 板级驱动（EPD/RTC/NFC/I2S 音频）+ WiFi + NVS + 设置 + 睡眠 + 充电 |
| `components/audio` | 908 | WebSocket 协议 + 文本流管道 |
| `components/78__xiaozhi-fonts` | （资源） | CBIN 矢量字体 + 天气图标 + emoji PNG |
| `main` | 1,854 | 应用编排（application.c 1412 行 + main.c 382 行） |

### 1.2 最大文件（Top 10）

| 文件 | 行数 | 混杂职责数 |
|------|------|-----------|
| `ui/ui_manager.c` | 1493 | 8（页面路由/状态栏/电池图标/七段时钟/快速切换/语音浮层/HTTP 服务器/设置持久化） |
| `main/application.c` | 1412 | 8（WiFi 状态机/SNTP/RTC 闹钟深睡/设置菜单/数据编组/事件队列/状态栏/低电量渲染） |
| `ui/pages/ap_transfer_server.c` | 1288 | 5（WiFi AP/HTTP 路由/HTML/图片解码/NVS） |
| `ui/pages/settings_page.c` | 1251 | 3（列表渲染/设备信息/主题子页） |
| `rawdraw/widgets/calendar.c` | 892 | 2（公历+农历渲染，但耦合节假日数据源） |
| `network/weather_api.c` | 892 | 1 |
| `network/photo_storage.c` | 829 | 1 |
| `ui/pages/photo_gallery_page.c` | 743 | 2 |
| `bsp/custom_lcd_display.c` | 716 | 1（EPD 驱动，命名误导为 LCD） |
| `ui/pages/chat_page.c` | 667 | 1 |

---

## 2. 组件依赖关系（实测）

### 2.1 当前依赖图

```
         ┌─────────────────────────────┐
         │   main (application.c)      │
         │   编排层                      │
         └──┬────┬────┬────┬────┬───────┘
            │    │    │    │    │
        ┌───▼┐ ┌▼──┐ ┌▼──┐ ┌▼──┐ ┌▼───┐
        │ ui │ │net│ │raw│ │aud│ │bsp │
        └─┬─┬┘ └─┬─┘ └─┬─┘ └─┬─┘ └────┘
          │ │    │     │     │
          │ └─ui→net(ap_transfer_server + 页面.h类型)
          └─ui→raw(渲染)                │
                 │     │     │
           raw→net(calendar widget, 反向)│
                 │     │     │
                 │     │  audio→ui(stream_pipeline, 反向)
                 │     │     │  audio→bsp(protocol 用 settings/system_info)
                 │     │     │  audio→raw(幽灵: 仅 CMake 声明, 代码零引用)
                 │     │     │
                 │  net→bsp  │
                 │           │
                 └ bsp→raw(类型,反向: epd_refresh.h 用 rawdraw_rect_t)

  反向依赖(破坏分层): raw→net, audio→ui, bsp→raw
  幽灵依赖(声明未用): audio→raw
  职责错位: ui 内嵌并宿主 ap_transfer_server(HTTP 服务器)
```

### 2.2 实测的跨层依赖清单

| 方向 | 类型 | 证据 | 严重度 |
|------|------|------|--------|
| `rawdraw/widgets/calendar.c` → `network/holiday_fetcher` | 实现 | `calendar.c:693-699` 直接调用 `holiday_fetcher_is_holiday()` 等 4 个查询函数 | 高 |
| `bsp/epd_refresh.h` → `rawdraw` | 类型 | 回调签名用 `rawdraw_rect_t`（`epd_refresh.h:6-7,148,173`） | 低 |
| `ui/ui_manager.c` → `network/ap_transfer_server` | 实现 | 内嵌 `ap_transfer_server_t` 实例，承担 HTTP 服务器宿主（`ui_manager.c:93,747-752`） | 高 |
| `ui/ui_manager.c` → `bsp/wifi_manager` | 实现 | 直接 `wifi_manager_get_ip()`（`ui_manager.c:948`） | 中 |
| `ui/ui_manager.c` → `bsp/custom_lcd_display` | 实现 | 注册刷新空闲回调 `set_on_refresh_idle()`（`ui_manager.c:753`） | 中 |
| `ui/ui_manager.c` → `bsp/nvs_state` | 实现 | 直接读写应用偏好（主题键 `RAWDRAW_THEME_NVS_KEY`） | 中 |
| `ui/pages/weather_page.c` → `network/weather_api` | 实现 | 页面直接调 `weather_api_fetch_now()`（`weather_page.c:298`） | 中 |
| `ui/pages/coding_plan_page.c` → `network/coding_plan_api` | 实现 | 页面直接调 `coding_plan_api_fetch_async()`（`coding_plan_page.c:319`） | 中 |
| `ui/pages/calendar_page.c, car_move_page.c` → `bsp/nvs_state` | 实现 | 页面直接读写持久化 | 中 |
| `ui/pages/ebook_page.c` → `bsp/settings` | 实现 | 页面直接操作 NVS | 中 |
| `audio/stream_pipeline.c` → `ui/ui_manager` | 实现 | 持有 `ui_manager_t*` 并直接 `ui_manager_append_chat_text()`（`stream_pipeline.c:16-18`）；audio CMakeLists 声明 `REQUIRES ui` | 高 |
| `audio/CMakeLists.txt` → `rawdraw` | 幽灵 | CMakeLists 声明 `REQUIRES rawdraw`，但 audio 代码（protocol/text_chunker/stream_pipeline）零引用 rawdraw 任何符号 | 低 |
| 多个页面 `.h` → `network` 类型 | 类型 | `weather_page.h`/`coding_plan_page.h`/`photo_gallery_page.h` 直接 `#include` 网络层数据结构 | 中 |

### 2.3 期望的分层（理想态）

```
自下而上单向流动：
  bsp(HAL) → rawdraw(图形) → network(数据服务) → ui(展示) → main(编排)
跨层通信一律经事件/回调，不直接持有下层实例
```

当前图中存在 3 处**反向依赖**（rawdraw→network、bsp→rawdraw、audio→ui）、1 处**幽灵依赖**（audio→rawdraw，仅 CMake 声明未使用）和 1 处**严重的职责错位**（UI 宿主 HTTP 服务器），破坏了分层。

---

## 3. 内聚/耦合评分

| 组件 | 内聚度 | 耦合度 | 评价 |
|------|:----:|:----:|------|
| `network` 各 API client | 良 | 中 | 各 client 相互独立；但 holiday_fetcher 混合了"网络获取"与"纯查询"两职责 |
| `rawdraw` 图形原语 | 优 | 中 | 原语本身内聚好；但 calendar widget 耦合数据源 |
| `rawdraw/widgets`（除 calendar） | 良 | 优 | widget 独立性好 |
| `ui/pages`（多数） | 中 | 中 | 页面自洽，但直接调网络 API / NVS 是硬伤 |
| `ui/ui_manager` | **差** | **差** | 8 职责混合；宿主 HTTP 服务器 |
| `audio` | 中 | 差 | 逻辑清晰，但反向依赖 UI |
| `bsp` | 差 | 中 | 杂物间（6 子域）；nvs_state 是应用关注点错置；单向类型耦合到 rawdraw（bsp→rawdraw） |
| `main/application.c` | **差** | 中 | 上帝对象 |

**总体结论**：底部（图形原语、network clients、widget）质量尚可；中上部（ui_manager / application / bsp）存在系统性分层倒置与职责膨胀。当前"高内聚低耦合"达标度约 **55%**。

---

## 4. 做得好的部分（保留）

1. **`page_registry` 自注册模式**：页面通过 `PAGE_REGISTER` 宏在链接器段注册，`ui/CMakeLists.txt` 用 `WHOLE_ARCHIVE` 保证构造器不被优化掉。新增页面无需修改中心化注册表，扩展性好。

2. **`rawdraw` 图形原语与 widget 分离**：原语层（`rawdraw.c`/`framebuffer.c`）不感知具体业务，widget 层（button/card/list_item 等 15 个）各自独立。除 calendar 外，widget 不跨层依赖。

3. **`network` 各 API client 相互独立**：weather_api / coding_plan_api / holiday_fetcher / photo_downloader 互不引用，可单独替换。

4. **`nvs_state` 的 write-through 缓存设计**：RAM 缓存为读源、setter 立即持久化，避免 N+1 Flash 擦除，崩溃不丢数据。设计专业。

5. **host 测试友好**：多个模块（holiday_fetcher、page_registry、bsp 部分）用 `#ifdef ESP_PLATFORM / #else` 提供 host shim，支持脱离硬件的单元测试（tests/ 下 20 个测试文件）。

6. **字体资源独立组件**（`78__xiaozhi-fonts`）：CBIN 矢量字体、天气图标、emoji PNG 集中管理，被 ui/rawdraw/main 引用，内聚良好。

---

## 5. 问题诊断（按严重度排序）

### P-高1 UI 层宿主 HTTP 服务器（职责错位）

**证据**：`ap_transfer_server.c`（1288 行，含 WiFi AP + esp_http_server + HTML + 图片解码）被放在 `components/ui/pages/` 下；`ui_manager.c:93` 在结构体内嵌其实例，`ui_manager.c:747-752` 在 init 中启动。

**影响**：UI 管理器同时是 HTTP 服务器宿主、WiFi 状态消费者、传输协议处理者。这是本仓库最严重的内聚问题——"页面渲染协调器"却拥有网络服务。后果：无法对 UI 做不依赖网络栈的测试；HTTP 服务变更要改 UI 文件；职责认知负担极高。

### P-高2 UI 页面直接穿透到数据/传输层（双向耦合 + 内聚破坏）

**证据**：
- `weather_page.c:298` 渲染层直接调 `weather_api_fetch_now()`
- `coding_plan_page.c:319` 直接调 `coding_plan_api_fetch_async()`
- `rawdraw/widgets/calendar.c:693-699` 底层 widget 直接调 `holiday_fetcher_is_holiday()` 等
- 多个页面 `.h` 直接 `#include` 网络层类型（`weather_api.h`、`coding_plan_api.h`、`photo_storage.h`）

**影响**：视图层同时承担展示 + 数据编排，违反"视图不应触发数据获取"。无法对页面做纯渲染测试；数据格式变更同时冲击网络层和 UI 层；刷新逻辑分散在页面而非统一编排器。calendar widget 的依赖更严重——底层图形组件依赖业务数据源，使图形库无法脱离业务复用。

### P-高3 流式管道反向依赖 UI（分层倒置）

**证据**：`stream_pipeline.c:16-18` 持有 `ui_manager_t*` 并调用 `ui_manager_append_chat_text(sp->ui, chunk)`；`audio/CMakeLists.txt:3` 声明 `REQUIRES ui`。

**影响**：数据/传输层（audio）依赖展示层（ui）。正确方向应反过来：UI 订阅文本块事件。当前实现导致 audio 组件无法独立测试或复用，且把 UI 线程模型硬编码进管道。

### P-中1 bsp ↔ rawdraw 类型耦合（分层倒置）

**证据**：`bsp/include/epd_refresh.h:6-7` `#include "rawdraw.h"` 与 `"rawdraw_ext.h"`，回调签名 `epd_refresh_cb_t` 用 `rawdraw_rect_t`；`bsp/CMakeLists.txt:6` 声明 `REQUIRES rawdraw`。

**影响**：这是**单向类型耦合**（bsp→rawdraw：`epd_refresh.h` 回调签名借用 `rawdraw_rect_t`），不是实现耦合——比最初判断的轻，且方向单一（rawdraw 并不反向依赖 bsp）。但仍造成概念性倒置：HAL 本应是最底层，却引用了图形层的类型。后果：替换显示驱动需先理解图形层类型；无法在不拉入图形栈的情况下构建/测试 HAL。建议长期把 `rawdraw_rect_t` 这类几何类型下沉到独立的 `display_types.h`（见 Phase 4.1）。

### P-中2 UI→bsp 多点穿透

**证据**：
- `ui_manager.c` → `wifi_manager.h`（直接查 IP `wifi_manager_get_ip()`）、`custom_lcd_display.h`（注册刷新空闲回调 `set_on_refresh_idle`）、`nvs_state.h`（读写主题键 `RAWDRAW_THEME_NVS_KEY`）
- `calendar_page.c`、`car_move_page.c` → `nvs_state.h`（页面直接操作持久化）
- `ebook_page.c` → `settings.h`（页面直接操作 NVS）

**影响**：UI 直接感知硬件/WiFi/NVS 细节，而非通过编排层注入状态。状态来源分散，难以 mock 与测试。

### P-中3 nvs_state 应用关注点错置于 bsp

**证据**：`bsp/nvs_state.c`（521 行）管理的是**应用级偏好**——天气城市、日历显示偏好、UI 导航状态、BLE 标志、刷新计数器等，而非板级硬件状态。

**影响**：应用逻辑下沉到 HAL 层，违反"bsp 只管硬件"。应用偏好变更需触碰 bsp 组件；bsp 编译边界被应用逻辑污染。

> 注：`bsp/settings.c`（115 行）是纯 NVS key-value 包装工具，放在 bsp 合理。

### P-中4 四个上帝对象

见 §1.2 表格。`ui_manager.c`、`application.c`、`ap_transfer_server.c`、`settings_page.c` 各自承担 3~8 个关注点，单文件改动牵动过多逻辑，认知负荷高，多人协作易冲突。

### P-低1 bsp 是杂物间（低内聚）

`bsp` 一个组件塞进 6 个子领域：显示驱动、存储、连接性、外设、电源、应用配置。任一子域修改都重编译整个 bsp。

### P-低2 命名误导

- `custom_lcd_display.c`（24KB）实际是 EPD（电子纸）驱动，却叫 LCD。
- `ap_transfer_server` 是网络/传输基础设施，却放在 `ui/pages/` 目录。

---

## 6. 重构计划

### 6.1 总体目标

将"高内聚低耦合"达标度从 ~55% 提升到 85%+；消除全部反向依赖；拆解 4 个上帝对象；建立"编排层订阅/发布"的跨层通信范式。

### 6.2 阶段划分

```
Phase 0 (低风险, 几乎零业务影响)
  └─ 修正依赖声明 + 命名

Phase 1 (高收益, 中等风险)
  └─ 引入事件总线 + 拆 HTTP 服务器归位

Phase 2 (中等收益, 中等风险)
  └─ 拆解上帝对象

Phase 3 (中等收益, 较大改动)
  └─ bsp 子域拆分 + 关注点归位

Phase 4 (持续性)
  └─ 类型层下沉 + 接口抽象
```

每个阶段独立可交付、可验证、可回滚。

---

### Phase 0 — 低风险修正（先行）

**0.1 拆分 holiday_fetcher 双重职责**
- 现状：`holiday_fetcher` 同时含"网络获取（init/fetch/parse_json）"和"纯查询（is_holiday/get_name，基于内存 cache）"。
- 动作：把纯查询函数与 `holiday_cache_t` 类型抽到 `network/include/holiday_query.h`（无网络依赖）；`holiday_fetcher.c` 保留获取逻辑并 include 它。
- 注：calendar widget 的解耦（Phase 1.3）有两种实现路径——(a) 注入 `const holiday_cache_t*` 让 widget 自带查表逻辑；或 (b) 注入函数指针表 `holiday_provider_t{is_holiday,get_name,is_makeup,get_label}`，widget 完全不知 cache 结构。路径 (b) 解耦更彻底（widget 不依赖 holiday_query.h 类型），推荐。
- 收益：为 Phase 1.3（解 calendar widget 耦合）铺路；查询可在 host 测试。

**0.2 重命名 custom_lcd_display → epd_driver**
- 动作：`bsp/custom_lcd_display.{c,h}` → `bsp/epd_driver.{c,h}`；全局替换 include；更新 CMakeLists。
- 收益：消除 LCD 误导。
- 风险：低（纯重命名 + include 路径更新）。

---

### Phase 1 — 消除分层倒置（核心）

**1.1 斩断 audio → UI 反向依赖（引入事件总线雏形）**
- 现状：`stream_pipeline.h:43` 在**头文件层** `#include "ui_manager.h"`，结构体字段 `ui_manager_t *ui`（`stream_pipeline.h:48`）；`.c:16-18` 直接调 `ui_manager_append_chat_text()`；`audio/CMakeLists.txt:3` 声明 `REQUIRES ui rawdraw`。
- 方案：定义 `text_chunk_cb_t` 回调接口；`stream_pipeline_t` 结构体字段改为 `text_chunk_cb_t cb; void *cb_ctx;`，**删除** `ui_manager_t *ui` 字段与 `#include "ui_manager.h"`；由 `application.c` 在 init 时注入 `(ui_manager_append_chat_text, ui_mgr)`。
- 改动文件：`audio/include/stream_pipeline.h`（改结构体 + 移 include）、`audio/stream_pipeline.c`（改 init 签名 + 回调调用）、`audio/CMakeLists.txt`（移除 `REQUIRES ui` 与 `REQUIRES rawdraw`——后者为幽灵依赖）、`main/application.c`（注入回调）。
- 验收：audio 组件不再 `REQUIRES ui`/`rawdraw`；chat 文本流功能不变；`test_network.c` 等仍通过。
- 风险：低-中；依赖注入反转本身简单，但 `stream_pipeline_init` 签名变化需同步所有调用点（当前仅 `application.c:984`）。

**1.2 移走 ap_transfer_server，UI 不再宿主 HTTP 服务器**
- 方案：
  - 将 `components/ui/pages/ap_transfer_server.{c,h}` 移至 `components/network/ap_transfer_server.{c,h}`（或新建 `components/services/`）。
  - `ui_manager` 改为持有不透明句柄 `transfer_service_t*`（前向声明），只保留 `start/stop/is_running` 转发，**不内嵌**实例、不 include 其头。
  - 生命周期管理（create/destroy）上移到 `application.c`；`ui_manager` 通过函数指针或 `application.c` 注入的接口操作。
  - `ui_manager.c` 移除 `#include "wifi_manager.h"`；IP 获取由 `application.c` 在调用 `ui_manager_start_lan_http_server` 前注入。
- 改动文件：移动 2 个文件；`network/CMakeLists.txt`（加 SRCS）；`ui/ui_manager.c`（瘦身 ~60 行）；`ui/CMakeLists.txt`（移 SRCS）；`main/application.c`（接管生命周期）。
- 验收：`ui_manager` 不再 include `ap_transfer_server.h`/`wifi_manager.h`；HTTP 传输功能在 AP 与 LAN 模式下均正常；图片上传后 UI 刷新正常。
- 风险：中；需仔细处理回调链（image_received/settings_changed/photos_changed/show_photo）的注入路径。

**1.3 解 calendar widget 对数据源的耦合**
- 现状：`calendar.c:693` 直接调 `holiday_fetcher_is_holiday()`。
- 方案（推荐路径 b）：定义 `holiday_provider_t` 函数指针表 `{bool(*is_holiday)(int,int,int); const char*(*get_name)(int,int,int); bool(*is_makeup)(int,int,int); const char*(*get_label)(int,int,int);}`；calendar widget 的绘制函数增加 `const holiday_provider_t *hol` 入参；调用方（`calendar_page.c`）在渲染前用 `holiday_fetcher_*` 填充该表并注入。widget 不再 include 任何 holiday 头。
- 改动文件：`rawdraw/widgets/calendar.{c,h}`（加 provider 入参，移 include）、`ui/pages/calendar_page.c`（构造 provider 并注入）、`rawdraw/CMakeLists.txt`（移除 `REQUIRES network`）。
- 验收：`rawdraw/widgets/calendar.c` 不再 include `holiday_fetcher.h`；`rawdraw` 组件不再 `REQUIRES network`；日历节假日/补班显示不变（host 测试 + 实机 smoke）。
- 风险：低；provider 接口是标准做法，且 calendar.c 已只在 `:693-699` 一处调用 holiday 函数，改动面小。

**1.4 解 UI 页面对网络 API 的直接调用**
- 现状：weather_page/coding_plan_page 直接调 fetch。
- 方案：引入轻量"刷新意图"机制——页面在 input 处理中调用 `ui_manager_request_refresh(UI_PAGE_WEATHER)`（已有 `ui_manager_request_active_page_refresh`）；`application.c` 的数据同步编排器订阅刷新请求，触发对应 API fetch。
- 改动文件：`weather_page.c`、`coding_plan_page.c`（改为请求刷新）；`application.c`（编排器响应）。
- 验收：页面不直接调 `weather_api_*`/`coding_plan_api_*`；刷新行为不变。
- 风险：低-中；需在编排器建立 page→数据源 的映射。

---

### Phase 2 — 拆解上帝对象

**2.1 拆分 application.c（1412 行 → 5 模块）**

| 新模块 | 来源行 | 职责 |
|--------|--------|------|
| `main/app_state.c` | ~200 | 设备状态机 + 事件队列 + run 循环分发 |
| `main/app_sync.c` | ~250 | 天气/编程计划/节假日数据同步编排（ensure/refresh/callback） |
| `main/app_sleep.c` | ~250 | RTC 闘钟 + 深睡 + 外设下电 + 唤醒判断 |
| `main/app_settings_menu.c` | ~200 | 设置项构建 + settings_menu_cb 分发 |
| `main/app_sntp.c` | ~50 | SNTP 初始化 + 回调 |
| `main/application.c`（瘦身后） | ~400 | 单例聚合 + init/run + 公共 API 转发 |

- 验收：单文件不超过 500 行；每个模块职责单一；行为完全等价（用现有 smoke test 验证）。
- 风险：中；纯结构重组，需保证静态状态（`s_app`）的访问协调（建议保留单一 `s_app` 但按模块拆函数）。

**2.2 拆分 ui_manager.c（1493 行）**
- 下沉到 widget：七段数字时钟绘制（`draw_mini_time_*`）、电池图标（`draw_battery_icon`）→ `rawdraw/widgets/`（status_bar widget 已存在，扩展它）。
- ui_manager 回归"页面路由 + 生命周期 + 刷新调度"单一职责。
- 目标：ui_manager.c < 700 行。
- 风险：中；需保证下沉后的回调/布局参数兼容。

**2.3 settings_page.c 主题子页外移**
- `settings_themes.c`（129 行，已独立文件）保持；确认 settings_page 仅调用其接口，不内联主题逻辑。

---

### Phase 3 — bsp 子域拆分 + 关注点归位

**3.1 按子域拆分 bsp**

| 新组件 | 含文件 |
|--------|--------|
| `bsp_display` | epd_driver（原 custom_lcd_display）、epd_refresh |
| `bsp_storage` | storage_manager、settings（NVS 包装） |
| `bsp_connectivity` | wifi_manager、zectrix_nfc |
| `bsp_peripherals` | rtc_pcf8563、audio_player、charge_status |
| `bsp_power` | sleep_manager |
| `bsp_board` | board、config、system_info |

- 收益：子域独立编译/演进；修改显示驱动不再重编译 WiFi。
- 风险：中-高；影响所有 REQUIRES bsp 的组件，需批量更新 CMakeLists。建议分步迁移（先拆 bsp_display，验证后再拆其余）。

**3.2 nvs_state 归位**
- `nvs_state.c`（应用偏好）从 bsp 移到 `main/`（或新建 `components/app_state/`）；bsp 只保留 `settings.c`（通用 NVS 工具）。
- 收益：应用关注点不再污染 HAL。
- 风险：低-中；需更新所有 include nvs_state.h 的 UI 文件（但这些文件本身在 Phase 1 后已不再直接调 NVS）。

---

### Phase 4 — 类型层下沉 + 接口抽象（持续性）

**4.1 抽出 display_interface（解 bsp↔rawdraw 类型耦合）**
- 新建 `components/display_hal/include/display_types.h`，仅含 `rawdraw_rect_t`（或更通用的 `display_rect_t`）+ `framebuffer` 抽象接口（`get_framebuffer/commit_rect/register_refresh_cb`）。
- `rawdraw` 与 `bsp_display` 共同依赖此薄接口，不再互相直接依赖。
- 收益：bsp_display 不再 `REQUIRES rawdraw`；可独立测试 HAL。
- 风险：中；需梳理 framebuffer 生命周期的所有权。

**4.2 数据 DTO 下沉到独立类型层**
- 现状：`weather_page.h` 等 include `weather_api.h` 取 `weather_data_t`。
- 方案：把 `weather_data_t`/`coding_plan_data_t`/`holiday_cache_t` 等 DTO 移到 `components/data_types/`（或各 network client 的纯类型头），UI 与 network 共同依赖该薄接口，UI 不直接依赖 network 实现。
- 收益：UI 只依赖数据契约，不依赖网络实现；可独立 mock 数据做 UI 测试。

**4.3（可选）统一事件总线**
- 若 Phase 1 的"回调注入"模式在多处铺开后显得重复，可引入一个极简的 `event_bus`（发布/订阅），统一所有跨层数据流（天气更新/文本块/图片就绪/设置变更）。
- 收益：跨层通信范式统一；新增数据流成本低。
- 风险：中；避免过度设计——若回调注入已够清晰则不强求总线。

---

## 7. 重构后目标依赖图

```
                ┌──────────────────────────┐
                │  main (application)      │
                │  编排层：状态机/同步/睡眠 │
                └──┬─────┬─────┬───────────┘
                   │     │     │  (注入回调/句柄)
            ┌──────▼─┐ ┌─▼─┐ ┌─▼────────┐
            │  ui    │ │net│ │  audio    │
            │ pages  │ │   │ │ pipeline  │
            │+mgr    │ │   │ │ (无UI依赖)│
            └──┬─────┘ └─┬─┘ └──┬────────┘
               │  ui→raw    │ net→bsp_display
               │  ui→types  │
          ┌────▼────┐  ┌────▼─────────┐
          │ rawdraw │  │ data_types   │  ← 薄类型层,无实现
          │ 原语+widget│ │ (DTO)        │
          └────┬────┘  └──────────────┘
               │ rawdraw→display_hal(类型)
          ┌────▼────────────┐
          │ display_hal     │  ← 抽象接口
          │ (display_types) │
          └────┬────────────┘
               │
   ┌───────────┴───────────────────────────┐
   │  bsp 子组件 (display/storage/         │
   │  connectivity/peripherals/power/board)│
   └───────────────────────────────────────┘

  跨层通信：事件总线 / 回调注入（单向）
  无反向依赖；bsp 不依赖 rawdraw；audio 不依赖 ui
```

---

## 8. 优先级与收益矩阵

| 阶段 | 任务 | 风险 | 收益 | 建议顺序 |
|------|------|:----:|:----:|:--------:|
| 0.1 | 拆 holiday_fetcher 查询/获取 | 低 | 中 | 1 |
| 0.2 | 重命名 epd_driver | 低 | 低 | 2 |
| 1.1 | audio 反转依赖（注入回调） | 低 | 高 | 3 |
| 1.3 | calendar widget 入参化 | 低 | 中 | 4 |
| 1.2 | 移走 ap_transfer_server | 中 | 高 | 5 |
| 1.4 | 页面改请求刷新 | 低-中 | 中 | 6 |
| 2.1 | 拆 application.c | 中 | 高 | 7 |
| 2.2 | 拆 ui_manager.c | 中 | 高 | 8 |
| 3.1 | bsp 子域拆分 | 中-高 | 中 | 9（分步） |
| 3.2 | nvs_state 归位 | 低-中 | 中 | 10 |
| 4.1 | display_interface 抽象 | 中 | 中 | 11 |
| 4.2 | DTO 类型层下沉 | 中 | 中 | 12 |

建议执行顺序：**0 → 1 → 2 → 3 → 4**。Phase 0+1 完成后即消除全部反向依赖与最严重的职责错位，达标度可提升至 ~75%。Phase 2 后达 ~85%。

---

## 9. 验证策略

每个阶段交付前必须：

1. **编译验证**：`idf.py build` 通过（ESP32-S3 target）。
2. **host 测试**：`tests/run_tests.sh` 全绿，重点验证改动涉及的模块。
3. **行为等价**：改动不改变用户可见行为，用 `test_ui_pages_smoke.c` + 手动 smoke（页面切换/天气刷新/图片传输/深睡唤醒）验证。
4. **依赖声明核对**：受影响组件的 `CMakeLists.txt` 的 `REQUIRES` 与实际 include 一致（无幽灵依赖、无遗漏）。

---

## 10. 附录：本次评审的修正记录

本报告经两轮复核，修正了以下项：

| 项 | 论断 | 实测修正 |
|----|---------|---------|
| rawdraw→network | 首轮"幽灵依赖，零使用" | **错误**。`calendar.c:693-699` 真实调用 holiday_fetcher 查询函数。改为"widget 直接耦合数据查询服务" |
| bsp→rawdraw | 首轮"无法单独替换显示驱动" | **偏重**。实为 `rawdraw_rect_t` 类型耦合（`epd_refresh.h` 回调签名），非实现耦合。降级为"单向类型耦合"，方向 bsp→rawdraw |
| rawdraw→bsp（framebuffer） | 二轮 §2.2 曾列为"类型耦合" | **臆造/错误**。framebuffer.c/h 与 rawdraw 全组件对 bsp 头文件零引用，CMakeLists 也不 REQUIRES bsp。该依赖行已删除 |
| audio→rawdraw | 两轮均未提及 | **遗漏**。`audio/CMakeLists.txt:3` 声明 `REQUIRES rawdraw`，但 audio 代码零引用。属幽灵依赖，已补入 §2.2 与 Phase 1.1 |
| UI→bsp 耦合范围 | 首轮仅提 wifi_manager | **遗漏**。补充 nvs_state/custom_lcd_display/settings 多点穿透；custom_lcd 用途实为 `set_on_refresh_idle` 回调注册（非"持有句柄"） |
| nvs_state 归属 | 首轮未分析 | **遗漏**。补充：应用级偏好错置于 bsp |
| holiday_fetcher | 首轮未分析 | **遗漏**。补充：双重职责（获取+查询）的拆分机会；并明确 widget 解耦推荐用 provider 函数指针表（路径 b） |
| 78__xiaozhi-fonts | 首轮完全漏掉 | **遗漏**。补充：字体/图标资源组件，内聚良好，保留 |
| WHOLE_ARCHIVE | 首轮未提 | **遗漏**。补充：自注册模式的必要手段，属优点 |
| 测试文件数 | 首轮/二轮写"21 个" | **错误**。实测 `tests/test_*.c` 共 20 个。已修正 |
| stream_pipeline 耦合层级 | 二轮只提 .c 持有 ui_manager_t* | **不精确**。实为头文件层耦合（`stream_pipeline.h:43` include + `:48` 结构体字段），比 .c 层更重。Phase 1.1 已明确需改头文件 |
