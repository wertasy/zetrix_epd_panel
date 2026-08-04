# ZecTrix EPD Panel — C++ 到纯 C 完整移植计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将原 C++ 项目（~49,000 行代码，271 个文件，不含字体）的所有功能完整移植到当前的纯 C ESP-IDF 工程。

**Architecture:** 原项目使用 C++ 面向对象架构（虚函数、智能指针、std::function 回调、STL 容器）。移植采用 C 的结构体 + 函数指针表模式，将每个 C++ 类映射为 `(struct + init/deinit + 方法函数)` 的等价 C 接口。UI 页面渲染器统一通过 `page_renderer_t` 虚表分发。图形/字体引擎直接操作 2bpp 帧缓冲区，无 LVGL 运行时依赖。

**Tech Stack:** ESP-IDF v6.0.2, ESP32-S3, SSD2683 4-color EPD (400×300), LVGL v9 字体格式（仅数据文件）, FreeRTOS, esp_codec_dev, cJSON, Opus 编解码器, Wi-Fi STA, BLE GATT

**原项目代码位置:** `/home/wert/GitHub/youn-ink-fourcolor-firmware/firmware/main/`

---

## 移植策略概览

### C++ 到 C 的映射模式

| C++ 特性               | C 替代方案                                                |
| -------------------- | ----------------------------------------------------- |
| `class` + 成员函数       | `typedef struct` + `prefix_method_name(struct*, ...)` |
| `virtual` 方法表        | `const struct ops*` (Linux 内核只读虚表指针，避免每个实例内置大量函数指针)   |
| `std::unique_ptr<T>` | 静态/初始化预分配，或首部嵌套基类实现强类型安全强制转型 (避免动态 malloc 内存碎片)       |
| `std::function` 回调   | `void (*callback)(void* user_data)`                   |
| `std::vector<T>`     | 固定容量数组，或多层复杂结构时采用 Arena 分配器 (例如 Chat Message)         |
| `std::string`        | `char[]` 固定缓冲，或 Linear Arena / 环形字符串缓冲区               |
| `std::mutex`         | `SemaphoreHandle_t`                                   |
| `namespace`          | 前缀命名 (`rawdraw_`, `ui_`, `app_`)                      |
| `template`           | 泛型 C 宏或类型特化                                           |

### 阶段划分

| 阶段      | 内容                        | 预估文件数 | 难度  |
| ------- | ------------------------- | ----- | --- |
| Phase 1 | 图形引擎核心 + UI 框架基础          | ~15   | 中   |
| Phase 2 | UI 组件库（Widget Components） | ~14   | 中   |
| Phase 3 | 硬件外设 + 网络模块               | ~12   | 中   |
| Phase 4 | 音频管线 + AI 流式对话            | ~10   | 高   |
| Phase 5 | UI 页面渲染器（第一批 — 9 页）     | ~16   | 中-高 |
| Phase 6 | UI 页面渲染器（第二批 — 10 页）+ UI 管理器| ~14   | 高   |
| Phase 7 | 集成 + 应用主框架 + 测试           | ~6    | 中   |

### 文件结构设计

```
zetrix_epd_panel/
├── CMakeLists.txt (Project CMake)
├── sdkconfig
├── partitions.csv
├── main/
│   ├── CMakeLists.txt
│   ├── main.c                      # 系统引导入口 (System Bootloader)
│   └── app.h / app.c               # Application 核心调度器 (Phase 7)
│
├── components/                     # 采用 ESP-IDF 组件化解耦，彻底防范模块交叉耦合
│   ├── bsp/                        # Board Support Package (硬件抽象层 HAL，内部静态化封装 g_xxx)
│   │   ├── CMakeLists.txt
│   │   ├── include/
│   │   │   ├── board.h
│   │   │   ├── custom_lcd_display.h
│   │   │   ├── rtc_pcf8563.h
│   │   │   ├── zectrix_nfc.h
│   │   │   ├── charge_status.h
│   │   │   ├── audio_player.h
│   │   │   ├── settings.h
│   │   │   ├── storage_manager.h   # 本地存储 (Task 3.1)
│   │   │   ├── nvs_state.h         # NVS 状态 (Task 3.2)
│   │   │   ├── system_info.h       # 系统信息 (Task 3.7)
│   │   │   └── config.h
│   │   ├── board.c
│   │   ├── custom_lcd_display.c
│   │   ├── rtc_pcf8563.c
│   │   ├── zectrix_nfc.c
│   │   ├── charge_status.c
│   │   ├── audio_player.c
│   │   ├── settings.c
│   │   ├── storage_manager.c
│   │   ├── nvs_state.c
│   │   └── system_info.c
│   │
│   ├── rawdraw/                    # 2bpp 图形绘制与组件库 (无外部硬件依赖，100% 可在 Host 编译)
│   │   ├── CMakeLists.txt
│   │   ├── include/
│   │   │   ├── rawdraw.h
│   │   │   ├── rawdraw_ext.h
│   │   │   ├── framebuffer.h
│   │   │   ├── theme.h
│   │   │   ├── style.h
│   │   │   └── font_engine.h
│   │   ├── rawdraw.c
│   │   ├── rawdraw_ext.c
│   │   ├── framebuffer.c
│   │   ├── theme.c
│   │   └── widgets/                # UI 基础组件 (Phase 2)
│   │       ├── widget.h
│   │       ├── button.h / .c
│   │       ├── panel.h / .c
│   │       ├── card.h / .c
│   │       ├── list_item.h / .c
│   │       ├── progress_bar.h / .c
│   │       ├── scrollview.h / .c
│   │       ├── slider.h / .c
│   │       ├── toggle.h / .c
│   │       ├── status_bar.h / .c
│   │       ├── footer_bar.h / .c
│   │       ├── modal.h / .c
│   │       ├── bubble.h / .c
│   │       ├── weather_card.h / .c
│   │       └── voice_wakeup.h / .c
│   │
│   ├── ui/                         # 业务 UI 页面与渲染器管理器
│   │   ├── CMakeLists.txt
│   │   ├── include/
│   │   │   ├── page_renderer.h
│   │   │   └── ui_manager.h
│   │   ├── ui_manager.c
│   │   └── pages/                  # 19 个具体的页面渲染逻辑
│   │       ├── chat_renderer.h / .c
│   │       ├── settings_renderer.h / .c
│   │       ├── weather_renderer.h / .c
│   │       ├── car_move_renderer.h / .c    # 扫码挪车页面 (Task 5.9)
│   │       ├── coding_plan_renderer.h / .c # Coding Plan 用量显示页面 (Task 6.10)
│   │       └── ...
│   │
│   ├── audio/                      # 音频核心服务 (整合 Opus 编解码与 WakeWord)
│   │   ├── CMakeLists.txt
│   │   ├── include/
│   │   │   ├── audio_service.h
│   │   │   ├── audio_codec.h
│   │   │   └── wake_word.h
│   │   ├── audio_service.c
│   │   ├── audio_codec.c
│   │   └── wake_word.c
│   │
│   └── network/                    # 网络与通信子系统
│       ├── CMakeLists.txt
│       ├── include/
│       │   ├── wifi_manager.h
│       │   ├── bluetooth_manager.h
│       │   ├── ble_gatt_service.h
│       │   ├── ble_image_receiver.h
│       │   ├── weather_api.h       # 天气客户端 (Task 3.3)
│       │   ├── holiday_fetcher.h   # 节日数据 (Task 3.4)
│       │   ├── photo_downloader.h  # 照片下载 (Task 3.5)
│       │   └── photo_storage.h     # 照片存储 (Task 3.5)
│       ├── wifi_manager.c
│       ├── bluetooth_manager.c
│       ├── ble_gatt_service.c
│       ├── ble_image_receiver.c
│       ├── weather_api.c
│       ├── holiday_fetcher.c
│       ├── photo_downloader.c
│       └── photo_storage.c
│
└── tests/                          # 单元测试与 Host 模拟预览
    ├── CMakeLists.txt
    ├── test_host.c
    ├── test_rawdraw.c
    └── test_rawdraw_sim.c          # 在 PC 端以 SDL2 / minifb 模拟屏幕预览界面
```

---

## Phase 1: 图形引擎核心 + UI 框架基础

**目标:** 扩充 rawdraw 绘制原语到完整功能，建立帧缓冲管理器、主题系统、样式常量、页面渲染器虚表接口。

**参考源文件:**
- `rawdraw/rawdraw.cc` (19KB, 601行) → `rawdraw/rawdraw.c` + `rawdraw/rawdraw_ext.c`
- `rawdraw/framebuffer.cc` (3.6KB) → `rawdraw/framebuffer.c`
- `rawdraw/theme.cc` (16KB) → `rawdraw/theme.c`
- `rawdraw/style.h` (10KB) → `rawdraw/style.h`
- `rawdraw/layout_utils.h` → `rawdraw/layout.h` + `rawdraw/layout.c`
- `rawdraw/clock.cc` (3.3KB) → `rawdraw/clock.c`
- `ui/pages/rawdraw/page_renderer.h` → `ui/page_renderer.h`
- `ui/epd_refresh.cc` → `ui/epd_refresh.c`

### Task 1.1: 扩充 rawdraw 绘制原语

**Files:**
- Create: `components/rawdraw/include/rawdraw_ext.h`
- Create: `components/rawdraw/rawdraw_ext.c`
- Modify: `tests/test_rawdraw.c`

**参考:** `rawdraw/rawdraw.cc` 中的所有函数，对比当前 `main/rawdraw.c` 已有的函数（set_pixel, draw_rect, draw_dither_rect, draw_round_rect, draw_text），补齐缺失的：

需要新增的函数（C++ 原名 → C 名）:
- `DrawRectBorder` → `rawdraw_draw_rect_border`
- `DrawRoundRectBorder` → `rawdraw_draw_round_rect_border`
- `DrawHLine` → `rawdraw_draw_hline`
- `DrawVLine` → `rawdraw_draw_vline`
- `DrawLine` → `rawdraw_draw_line` (Bresenham)
- `DrawCircle` → `rawdraw_draw_circle`
- `DrawCircleBorder` → `rawdraw_draw_circle_border`
- `DrawProgress` → `rawdraw_draw_progress`
- `DrawProgressWithLabel` → `rawdraw_draw_progress_with_label`
- `MeasureTextWidth` → `rawdraw_measure_text_width`
- `MeasureTextHeight` → `rawdraw_measure_text_height`
- `MeasureTextBounds` → `rawdraw_measure_text_bounds`
- `FillRect` → `rawdraw_fill_rect`
- `InvertRegion` → `rawdraw_invert_region`
- `CopyRegion` → `rawdraw_copy_region`
- `Clear` → `rawdraw_clear`
- `DrawStripeRect` → `rawdraw_draw_stripe_rect`
- `get_pixel` → `rawdraw_get_pixel`

- [ ] **Step 1:** 阅读 `rawdraw/rawdraw.cc` 完整源码（所有绘制函数实现）
- [ ] **Step 2:** 创建 `rawdraw_ext.h`，声明所有缺失函数的 C 接口
- [ ] **Step 3:** 创建 `rawdraw_ext.c`，逐函数移植（注意：C++ `namespace rawdraw { }` → C 无命名空间，函数加 `rawdraw_` 前缀）
- [ ] **Step 4:** 编写 `tests/test_rawdraw.c` 测试程序，验证每个绘制函数
- [ ] **Step 5:** `gcc -o test_rawdraw tests/test_rawdraw.c main/rawdraw.c main/rawdraw_ext.c && ./test_rawdraw`
- [ ] **Step 6:** Commit

### Task 1.2: 帧缓冲区管理器 + Dirty Rect 追踪

**Files:**
- Create: `components/rawdraw/include/framebuffer.h`
- Create: `components/rawdraw/framebuffer.c`
- Test: `tests/test_framebuffer.c`

**参考:** `rawdraw/framebuffer.cc` (3.6KB) + `rawdraw/framebuffer.h` (7.1KB)

C++ `Framebuffer` 类需要映射为：
```c
typedef struct {
    uint8_t* buffer;
    int width;
    int height;
    SemaphoreHandle_t mutex;
    rawdraw_rect_t dirty;
    bool pending;
    framebuffer_refresh_cb_t refresh_cb;
    void* refresh_user_data;
    uint32_t last_refresh_ms; // 记录上次刷新时间戳用于防抖
    uint32_t next_kick_ms;    // EPD 刷新的防抖/合并延迟 (典型值 300ms-500ms)
} framebuffer_t;

void framebuffer_init(framebuffer_t* fb, uint8_t* buffer, int width, int height, SemaphoreHandle_t mutex);
void framebuffer_deinit(framebuffer_t* fb);
void framebuffer_clear(framebuffer_t* fb, rawdraw_color_t color);
void framebuffer_invalidate_rect(framebuffer_t* fb, int x, int y, int w, int h);
void framebuffer_invalidate_all(framebuffer_t* fb);
rawdraw_rect_t framebuffer_get_dirty(framebuffer_t* fb);
bool framebuffer_has_dirty(framebuffer_t* fb);
void framebuffer_clear_dirty(framebuffer_t* fb);
// 核心接口：添加 debounce 机制以合并多区域或流式更新产生的脏矩形
void framebuffer_request_refresh(framebuffer_t* fb, bool urgent);
void framebuffer_lock(framebuffer_t* fb);
void framebuffer_unlock(framebuffer_t* fb);
```

- [ ] **Step 1:** 创建 `framebuffer.h` 定义上述结构体和 API
- [ ] **Step 2:** 创建 `framebuffer.c` 移植 `framebuffer.cc` 逻辑。在 `framebuffer_request_refresh` 中内置防抖 (debounce) 与合并 (coalescing) 机制（典型防抖时间 300ms–500ms），合并短时间内的脏矩形刷新请求，以减少墨水屏物理更新次数并防止 AI 吐字或 UI 快速变更时屏幕频繁闪烁。同时，统一使用 FreeRTOS `SemaphoreHandle_t` 锁，由于 C 语言没有 RAII 机制，必须确保在所有函数出口分支中手动 `unlock`；对于帧缓冲与照片数据等大块内存，必须使用 `MALLOC_CAP_SPIRAM` 分配策略存放在 PSRAM 中以规避 SRAM 碎片。
- [ ] **Step 3:** 编译运行验证
- [ ] **Step 4:** Commit

### Task 1.3: 样式常量 + 布局工具

**Files:**
- Create: `components/rawdraw/include/style.h`
- Create: `components/rawdraw/include/layout.h`
- Create: `components/rawdraw/layout.c`
- Test: `tests/test_layout.c`

**参考:**
- `rawdraw/style.h` — 所有 `constexpr` 常量，直接用 `#define` 或 `enum` 转换
- `rawdraw/layout_utils.h` — `TopYFromBaseline`, `CenterTextTopY`, `MeasureTextInkBounds`, `InkCenteredTextTopY`

- [ ] **Step 1:** 创建 `style.h`，将所有 `constexpr int kSpacingXXS` 等 → `#define STYLE_SPACING_XXS 2`
- [ ] **Step 2:** 创建 `layout.h` + `layout.c`，移植布局计算函数
- [ ] **Step 3:** 编写测试验证文本居中计算
- [ ] **Step 4:** Commit

### Task 1.4: 主题系统

**Files:**
- Create: `components/rawdraw/include/theme.h`
- Create: `components/rawdraw/theme.c`

**参考:** `rawdraw/theme.h` (3.4KB) + `rawdraw/theme.cc` (16KB)

- [ ] **Step 1:** 创建 `theme.h` — 将 C++ `enum class ThemeId` → C `enum`，`PaintStyle` struct，`ThemeDefinition` struct，`ThemeManager` → C 函数集
- [ ] **Step 2:** 创建 `theme.c` — 移植 6 个主题定义数据表和查询逻辑
- [ ] **Step 3:** 移植 `DrawStyledRect`, `DrawStyledRoundRect`, `DrawStyledText` 等 styled 绘制函数
- [ ] **Step 4:** Commit

### Task 1.5: 页面渲染器虚表 + EPD 刷新策略

**Files:**
- Create: `components/ui/include/page_renderer.h`
- Create: `components/bsp/include/epd_refresh.h`
- Create: `components/bsp/epd_refresh.c`

**参考:** `ui/pages/rawdraw/page_renderer.h` + `ui/epd_refresh.cc` (5.6KB) + `ui/epd_refresh.h` (4.3KB)

```c
// page_renderer.h
typedef enum {
    BTN_UP_CLICK, BTN_DOWN_CLICK,
    BTN_UP_DOUBLE_CLICK, BTN_DOWN_DOUBLE_CLICK,
    BTN_UP_LONG_PRESS, BTN_DOWN_LONG_PRESS,
    BTN_BOOT_CLICK, BTN_BOOT_DOUBLE_CLICK, BTN_BOOT_LONG_PRESS,
} button_event_type_t;

typedef struct {
    button_event_type_t type;
} button_event_t;

typedef struct page_renderer page_renderer_t;

struct page_renderer_ops {
    void (*init)(page_renderer_t* self, int width, int height);
    void (*render)(page_renderer_t* self, uint8_t* fb, int width, int height);
    bool (*handle_input)(page_renderer_t* self, const button_event_t* event);
    rawdraw_rect_t (*get_dirty_rect)(const page_renderer_t* self);
    bool (*needs_full_refresh)(const page_renderer_t* self);
    void (*mark_full_refresh)(page_renderer_t* self);
    void (*clear_full_refresh_flag)(page_renderer_t* self);
    bool (*append_text)(page_renderer_t* self, const char* chunk);
    void (*begin_stream)(page_renderer_t* self);
    void (*end_stream)(page_renderer_t* self);
};

struct page_renderer {
    const struct page_renderer_ops* ops; // Linux 内核级只读方法表指针 (存放在 flash 中)
    int width;
    int height;
    bool needs_full_refresh_flag;
};

- [ ] **Step 1:** 创建 `page_renderer.h` 虚表定义 (使用上述 `const struct page_renderer_ops*` 方案，并通过结构体首成员嵌套基类实现具体渲染器类型的零成本安全向上转型)。
- [ ] **Step 2:** 创建 `epd_refresh.h` + `epd_refresh.c` — 移植刷新策略。基于帧缓冲脏矩形和防抖合并策略判定刷新时机，同时内置针对红色和黄色素的残影清理机制（Ghosting Cleanup），通过统计局部刷新次数，在达到阈值（如 10 次）或用户切换大页面时，自动触发一次全局清屏全刷新。
- [ ] **Step 3:** Commit

### Task 1.6: 时钟组件

**Files:**
- Create: `components/rawdraw/include/clock.h`
- Create: `components/rawdraw/clock.c`

**参考:** `rawdraw/clock.cc` (3.3KB) + `rawdraw/clock.h`

- [ ] **Step 1:** 移植 Clock 组件 — 显示 HH:MM，支持多字体和颜色
- [ ] **Step 2:** Commit

---

## Phase 2: UI 组件库（Widget Components）

**目标:** 移植 rawdraw/widgets/ 下所有 UI 组件。每个组件从 C++ class 移植为 C struct + 方法函数集。

**参考源文件:** `rawdraw/widgets/` 目录下 14 个组件，共 ~90KB

### Widget 基类约定

所有 widget 遵循统一模式：
```c
// widget_xxx.h
typedef struct {
    int x, y, w, h;          // 位置和尺寸
    // ... 组件特有属性
} widget_xxx_t;

void widget_xxx_init(widget_xxx_t* w, int x, int y, int w_, int h_);
void widget_xxx_render(widget_xxx_t* w, uint8_t* fb, int fb_width, int fb_height);
bool widget_xxx_handle_input(widget_xxx_t* w, const button_event_t* event);
```

### Task 2.1: Button 组件

**参考:** `components/button.cc` (3.2KB) + `button.h` (4.2KB)

- [ ] 移植按钮组件（普通/选中/禁用状态、图标+文本、圆角）
- [ ] Commit

### Task 2.2: Panel 组件

**参考:** `components/panel.cc` (3.9KB) + `panel.h` (3.7KB)

- [ ] 移植面板容器（标题栏、内容区域、边框）
- [ ] Commit

### Task 2.3: Card 组件

**参考:** `components/card.cc` (5.0KB) + `card.h` (4.6KB)

- [ ] 移植卡片容器（阴影效果、标题、内容）
- [ ] Commit

### Task 2.4: ListItem 组件

**参考:** `components/list_item.cc` (5.6KB) + `list_item.h` (4.7KB)

- [ ] 移植列表项（图标+标签+值+箭头）
- [ ] Commit

### Task 2.5: ProgressBar 组件

**参考:** `components/progress_bar.cc` (8.2KB) + `progress_bar.h` (6.5KB)

- [ ] 移植进度条（水平/圆形、标签、百分比）
- [ ] Commit

### Task 2.6: ScrollView 组件

**参考:** `components/scrollview.cc` (3.2KB) + `scrollview.h` (4.3KB)

- [ ] 移植滚动视图（视口、滚动条、偏移量管理）
- [ ] Commit

### Task 2.7: Slider 组件

**参考:** `components/slider.cc` (7.4KB) + `slider.h` (4.8KB)

- [ ] 移植滑块（轨道、菱形拇指、最小/最大/值标签）
- [ ] Commit

### Task 2.8: Toggle 组件

**参考:** `components/toggle.cc` (4.6KB) + `toggle.h` (4.6KB)

- [ ] 移植开关切换（轨道、圆形拇指、标签）
- [ ] Commit

### Task 2.9: StatusBar 组件

**参考:** `components/status_bar.cc` (3.3KB) + `status_bar.h` (3.8KB)

- [ ] 移植状态栏（页面标题、Wi-Fi/电池/蓝牙图标、日期时间）
- [ ] Commit

### Task 2.10: FooterBar 组件

**参考:** `components/footer_bar.cc` (3.1KB) + `footer_bar.h` (927B)

- [ ] 移植底部提示栏（左/中/右文本）
- [ ] Commit

### Task 2.11: Modal 组件

**参考:** `components/modal.cc` (4.9KB) + `modal.h` (1.1KB)

- [ ] 移植模态对话框（半透明遮罩、标题栏、内容、底部按钮）
- [ ] Commit

### Task 2.12: Bubble 组件

**参考:** `components/bubble.cc` (8.6KB) + `bubble.h` (6.2KB)

- [ ] 移植聊天气泡（左/右对齐、文本换行、时间戳、圆角）
- [ ] Commit

### Task 2.13: WeatherCard 组件

**参考:** `components/weather_card.cc` (8.5KB) + `weather_card.h` (3.7KB)

- [ ] 移植天气卡片（温度、天气描述、体感温度、更新时间）
- [ ] Commit

### Task 2.14: VoiceWakeup 组件

**参考:** `components/voice_wakeup.cc` (5.2KB) + `voice_wakeup.h` (4.6KB)

- [ ] 移植语音唤醒覆盖层（录音动画、状态文本）
- [ ] Commit

### Task 2.15: Calendar 组件

**参考:** `components/calendar.cc` (28.6KB) + `calendar.h` (6.8KB)

- [ ] 移植日历组件（月视图、星期行、农历、节气、节假日、滚动导航）— 最复杂的组件
- [ ] Commit

---

## Phase 3: 硬件外设 + 网络模块

**目标:** 移植所有网络通信和存储模块。已有模块（board, rtc_pcf8563, zectrix_nfc, audio_player, settings, wifi_manager, charge_status, sleep_manager）需对齐接口。

### Task 3.1: StorageManager（文件系统管理）

**参考:** `common/storage_manager.cc` (4.6KB)

- [ ] **引入 LittleFS 存储管理**：统一采用 LittleFS 作为 Flash 文件系统以取代 SPIFFS。由于 LittleFS 读写均衡算法更佳，具有物理断电保护机制，并天然支持真正目录树结构，极利于相册大文件的管理。实现 LittleFS 的挂载与文件读写封装。
- [ ] Commit

### Task 3.2: NVS State（状态持久化）

**参考:** `common/nvs_state.cc` (7.1KB)

- [ ] 移植 NVS 键值持久化，并在内存中构建 `system_settings_t` 结构体以实现配置缓存（Write-Through Cache 模式），避免运行期频繁擦写物理 Flash 阻塞高吞吐或实时循环。配置读取直接命中 RAM，仅在值改变时通过防抖机制写回 NVS，避免高频擦写。
- [ ] Commit

### Task 3.3: WeatherApi（天气 API 客户端）

**参考:** `common/weather_api.cc` (13.1KB)

- [ ] 移植 HTTP 天气请求、JSON 解析、数据缓存
- [ ] Commit

### Task 3.4: HolidayFetcher（节日获取）

**参考:** `common/holiday_fetcher.cc` (9.7KB)

- [ ] 移植节假日 API 请求和缓存
- [ ] Commit

### Task 3.5: PhotoDownloader + PhotoStorage

**参考:** `common/photo_downloader.cc` (12KB) + `common/photo_storage.cc` (17.1KB)

- [ ] 移植照片下载（HTTP）、4色量化、LittleFS 存储
- [ ] Commit

### Task 3.6: BluetoothManager + BleGattService + BleImageReceiver

**参考:** `common/bluetooth_manager.cc` (6.1KB) + `ble_gatt_service.cc` (13.7KB) + `ble_image_receiver.cc` (3.1KB)

- [ ] 移植 BLE 初始化、GATT 服务注册、图片接收。
- [ ] 碰一碰传图支持：在设备启动时，将本地 BLE MAC 地址及配对信息格式化为 NDEF 文本，写入 GT23SC6699 NFC 标签中，支持手机触碰后自动连接蓝牙并通过 `ble_image_receiver` 进行“Touch & Go”后台传图。
- [ ] Commit

### Task 3.7: SystemInfo

**参考:** `system_info.cc` (5.0KB)

- [ ] 移植系统信息获取（MAC 地址、芯片型号、SDK 版本、可用内存）
- [ ] Commit

### Task 3.8: Kconfig 配置选项

**参考:** `Kconfig.projbuild` (1.4KB)

- [ ] 移植菜单配置选项（Wi-Fi SSID/密码、服务器地址、天气 API Key 等）
- [ ] Commit

### Task 3.9: 硬件抽象层 (HAL/BSP) 重构与功耗优化

**Files:**
- Modify: `components/bsp/wifi_manager.c`
- Modify: `components/bsp/audio_player.c`
- Modify: `components/bsp/charge_status.c`
- Modify: `components/bsp/board.c`
- Modify: `components/bsp/custom_lcd_display.c`

- [ ] 重构 `wifi_manager.c`：**规避系统事件循环阻塞**，移除事件回调中阻塞的 `vTaskDelay`，改由非阻塞 `esp_timer_t` 延迟触发 `esp_wifi_connect()`，或在独立的后台监控任务中调度重连逻辑；**启用 Wi-Fi Modem Sleep**，连接建立后配置 `esp_wifi_set_ps(WIFI_PS_MIN_MODEM)`，允许芯片在 DTIM 广播信标之间关闭 RF 模块，将闲置电流降至 10mA 以降低空闲功耗。
- [ ] 重构 `audio_player.c`：**浮点运算与 I2S 音频合成优化**，将正弦波生成的双精度 `sin()` 替换为单精度 `sinf()`（强制单精度硬件 FPU 加速）或静态正弦查找表（LUT）；将音效播放改造为非阻塞异步调用，防止阻塞主调任务。
- [ ] 重构 `charge_status.c`：读取 `VBAT_PWR_PIN` (GPIO17) 的电压，在内部加入非线性锂电池放电电量映射表（LUT），实现精确的剩余百分比转换。
- [ ] 重构 `board.c`：**充电去抖状态机精度修正**，将 `charge_status_tick` 采样调用移出长睡眠（高达 500ms - 2.8s）的 LED 任务，挂载到固定的 100ms `esp_timer_t` 非阻塞回调中（或将充电芯片检测引脚配置为中断触发），保障去抖时序精度（去抖常数如 `STABLE_HIGH_MS` = 400ms）。
- [ ] 重构 `custom_lcd_display.c`：**EPD Busy 管脚中断唤醒**，将 Busy 管脚配置为边沿中断触发；将 `read_busy` 中的 50ms 轮询检测替换为 FreeRTOS 信号量阻塞与中断唤醒机制，允许 CPU 在屏幕物理刷新期间（数秒内）进入 Light Sleep 节能，刷新完毕后在 ISR 中唤醒任务。

---

## Phase 4: 音频管线 + AI 流式对话

**目标:** 移植音频服务和 AI 对话流式管线。这是最复杂的阶段。

**参考:**

- `audio/audio_service.cc` (29.4KB) — 音频核心管线
- `audio/audio_service.h` (5.6KB)
- `streaming/stream_pipeline.cc` (4.7KB) — AI 对话管线
- `streaming/tts_streamer.cc` (5.6KB) — TTS 流式播放
- `streaming/text_chunker.cc` (5.5KB) — 文本分块
- `protocols/protocol.h` (4.4KB) — WebSocket 协议

### Task 4.1: Protocol（WSS 协议层）

**参考:** `protocols/protocol.h`

- [ ] 移植 WebSocket 协议（消息收发、心跳、Opus 数据包封装）
- [ ] C++ `Protocol` 抽象基类 → C 函数指针回调接口
- [ ] Commit

### Task 4.2: AudioCodec（编解码器抽象）

**参考:** `audio/audio_codec.cc/h`

- [ ] 移植编解码器接口抽象（I2S 配置、ES8311 初始化 — 已有部分在 `audio_player.c`）
- [ ] Commit

### Task 4.3: WakeWord（唤醒词检测）

**参考:** `audio/wake_word.h` + `esp-sr` 组件集成

- [ ] 移植唤醒词检测（ESP-SR 模型加载、VAD、关键词识别）
- [ ] Commit

### Task 4.4: AudioService（音频核心管线）

**参考:** `audio/audio_service.cc` (29.4KB) — **核心难点**

- [ ] 移植音频管线：
  - MIC → 处理器 → Opus 编码 → 发送队列
  - 接收队列 → Opus 解码 → 扬声器
  - 3 个 FreeRTOS 任务管理
  - 事件组状态机
- [ ] **音频任务间流包同步**：放弃自定义环形队列加条件变量的轮询做法，完全使用 FreeRTOS 原生队列 (`QueueHandle_t`) 或消息缓冲区 (`MessageBufferHandle_t`) 进行 Opus 数据包跨线程传递，利用内核挂起机制实现高响应、零自旋锁开销，以此代替 C++ 中的 `std::deque<std::unique_ptr<AudioStreamPacket>>`。
- [ ] Commit

### Task 4.5: StreamPipeline + TTS Streamer + TextChunker

**参考:** `streaming/stream_pipeline.cc` + `tts_streamer.cc` + `text_chunker.cc`

- [ ] 移植 AI 对话流：
  - 接收 WSS 文本流 → 分块 → 追加到 ChatRenderer。引入 Linear Arena 字符串内存池（预分配固定大小，例如 32KB），以 O(1) 的开销进行顺序拼接拷贝，清除聊天时游标归零，彻底避免堆内存碎片。
  - **刷新抑制机制**：在 WSS 文本流式追加期间，屏幕**禁止**逐字刷新（防止墨水屏频繁闪烁耗电）。仅当接收到整句结束（遇到标点符号）或流式输出结束时，才触发一次 EPD 局部刷新。
  - 接收 Opus 音频 → 解码 → 播放
  - **网络拥塞与丢包容错 (Jitter Buffer & PLC)**：在 Opus 接收端实现 **Jitter Buffer** 缓存（如 2-3 帧），并在播放队列发生 Underflow 时自动注入静音包 (PLC 机制)，避免系统因等待数据挂起或产生爆音（TTS 流式播放管理与播放拥塞控制）。
- [ ] Commit

---

## Phase 5: UI 页面渲染器（第一批 — 9 页）

**目标:** 移植前 9 个页面渲染器。每个渲染器实现 `page_renderer_t` 虚表。

### Task 5.1: WeatherRenderer（天气页面）

**参考:** `pages/rawdraw/weather_renderer.cc` (15.4KB)

- [ ] 移植天气页面（当前天气卡片 + 7 日预报列表）
- [ ] Commit

### Task 5.2: WeatherDetailRenderer（天气详情页面）

**参考:** `pages/rawdraw/weather_detail_renderer.cc` (9.4KB)

- [ ] 移植天气详情（24 小时温度曲线、风向、湿度、紫外线）
- [ ] Commit

### Task 5.3: EbookRenderer（电子书阅读器）

**参考:** `pages/rawdraw/ebook_renderer.cc` (14.0KB)

- [ ] 移植电子书（分页渲染、进度条、上下翻页）
- [ ] Commit

### Task 5.4: WifiRenderer（Wi-Fi 配置页面）

**参考:** `pages/rawdraw/wifi_renderer.cc` (17.5KB)

- [ ] 移植 Wi-Fi 页面（SSID 列表、连接状态、AP 配置二维码）
- [ ] Commit

### Task 5.5: CalendarRenderer（日历页面）

**参考:** `pages/rawdraw/calendar_renderer.cc` (6.0KB)

- [ ] 移植日历页面（嵌入 Calendar 组件、月份导航、今日高亮）
- [ ] Commit

### Task 5.6: PhotoGalleryRenderer（照片相册）

**参考:** `pages/rawdraw/photo_gallery.cc` (27.0KB)

- [ ] 移植照片相册（4色抖动渲染、幻灯片、列表视图）
- [ ] Commit

### Task 5.7: PhotoDetailRenderer（照片详情）

**参考:** `pages/rawdraw/photo_detail_renderer.cc` (9.3KB)

- [ ] 移植照片详情页（全屏查看、缩放、删除）
- [ ] Commit

### Task 5.8: ChatRenderer（AI 对话页面）

**参考:** `pages/rawdraw/chat_renderer.cc` (23.6KB)

- [ ] 移植对话页面（消息列表、流式文本追加、滚动、状态气泡）
- [ ] Commit

### Task 5.9: CarMoveRenderer（扫码挪车页面）

**参考:** 本地全新实现 (BSP 模块提供 QR 码显示原语)

- [ ] 移植并实现扫码挪车页面：
  - 绘制“微信扫码，呼叫车主”核心文案（使用中文字体库）。
  - 将预设的车主电话号码（通过 NVS Settings 配置读取，提供默认值）编码为 `tel:138xxxxxxxx` 或统一中间代理 URL，并调用二维码生成库（ESP-IDF 中的 `qrcode` 库）在屏幕正中渲染二维码。
  - 按键交互：短按 BOOT 键触发返回或刷新二维码。
- [ ] Commit

---

## Phase 6: UI 页面渲染器（第二批 — 10 页）+ UI 管理器

### Task 6.1: SettingsRenderer（设置面板）

**参考:** `pages/rawdraw/settings_renderer.cc` (83.7KB) — **最大文件**

- [ ] 移植设置面板（侧边栏分类、列表项、开关、滑块、模态对话框、主题选择、关于页面）
- [ ] **复杂单文件拆分与菜单剥离**：采用声明式菜单配置项数据表，将通用渲染引擎与具体的逻辑菜单项定义剥离。将其拆分为 `settings_renderer.c`（核心渲染）、`settings_dialogs.c`（对话框）、`settings_about.c`（关于页）和 `settings_themes.c`（主题数据） 4 个 C 文件。
- [ ] Commit

### Task 6.2: NewsRenderer（新闻页面）

**参考:** `pages/rawdraw/news_renderer.cc` (14.8KB)

- [ ] 移植新闻页面（标题列表 + 详情展开）
- [ ] Commit

### Task 6.3: LifeBarRenderer（寿命倒数页面）

**参考:** `pages/rawdraw/lifebar_renderer.cc` (11.9KB)

- [ ] 移植寿命倒数（周数网格、进度条、统计数据）
- [ ] Commit

### Task 6.4: AlmanacRenderer（老黄历页面）

**参考:** `pages/rawdraw/almanac_renderer.cc` (10.1KB)

- [ ] 移植老黄历（宜忌、农历、节气）
- [ ] Commit

### Task 6.5: LogRenderer（日志查看器）

**参考:** `pages/rawdraw/log_renderer.cc` (9.4KB)

- [ ] 移植日志页面（ESP_LOG 缓冲区、级别过滤、滚动）
- [ ] Commit

### Task 6.6: YearProgressRenderer（年度进度页面）

**参考:** `pages/rawdraw/yearprogress_renderer.cc` (14.1KB)

- [ ] 移植年度进度（全年日历热力图、百分比统计）
- [ ] Commit

### Task 6.7: FontDebugRenderer + FontMetricsRenderer

**参考:** `font_debug_renderer.cc` (3.8KB) + `font_metrics_renderer.cc` (3.8KB)

- [ ] 移植字体调试页面（字符网格、度量信息）
- [ ] Commit

### Task 6.8: ApTransferRenderer + ApTransferServer

**参考:** `ap_transfer_renderer.cc` (10.4KB) + `ap_transfer_server.cc` (47.4KB)

- [ ] 移植 AP 传图功能（HTTP 服务器、照片上传、Wi-Fi AP 模式切换）
- [ ] **HTTP 服务路由注册**：利用 `esp_http_server` 组件，将完整 HTTP 传图服务移植为 C handler 路由注册模式。
- [ ] Commit

### Task 6.9: RawDrawUiManager（UI 管理器）

**参考:** `ui/rawdraw_ui_manager.cc` (69.6KB) + `rawdraw_ui_manager.h` (17.5KB)

- [ ] 移植 UI 管理器核心：
  - 页面注册表（19 个页面的渲染器实例，新增扫码挪车与 Coding Plan 页面）。**页面预分配规避内存碎片**：将所有 19 个页面渲染器结构体在初始化阶段静态声明或一次性分配完毕，在运行期页面切换时只修改指向活跃渲染器的指针，严禁在运行时动态分配与销毁页面结构体。
  - 页面切换（清屏 + 重新渲染）
  - 按键事件路由到当前页面
  - 状态栏 + 页面内容渲染调度
  - Quick Switch 快速切换覆盖层
  - 时钟刷新定时器
  - 幻灯片定时器
  - 帧缓冲刷新回调
- [ ] Commit

### Task 6.10: CodingPlanRenderer（Coding Plan 用量显示页面）

**参考:** 本地全新实现 (依赖 storage 模块缓存网络图片)

- [ ] 移植并实现 Coding Plan 用量显示页面：
  - 调用 `ProgressBar` 组件显示每 5 小时使用额度（百分比）和每周使用额度（百分比）。
  - 显示文字：重置时间、近 7 天 token 消耗总量、各个模型消耗量明细。
  - **折线图渲染**：后台任务通过 HTTP 请求从服务端 API 拉取提前渲染好（4色抖动处理）的用量折线图 2bpp 原始位图，缓存至 LittleFS 存储中，通过 `rawdraw_copy_region` 将该用量折线图直接贴图渲染到页面下半部分。
- [ ] Commit

---

## Phase 7: 集成 + 应用主框架

**目标:** 将所有模块集成到应用主框架，替换当前 3 页 demo 为完整的 19 页 UI 系统。

### Task 7.1: Application 单例

**参考:** `application.cc` (24.9KB) + `application.h`

- [ ] 移植 Application 核心与系统功耗状态机：
  - **功耗及睡眠状态机**：设计统一的 `Active` $\rightarrow$ `Idle` $\rightarrow$ `Light Sleep` / `Deep Sleep` 功耗状态机。睡眠前将 Wi-Fi、BLE 和音频解码器置于低功耗待机模式。
  - **硬电源切断 (Power Gating)**：在进入 Light/Deep Sleep 前，除了发出 LCD 和 Codec 驱动的软件 Sleep 指令，必须通过拉低引脚 `EPD_PWR_PIN` (GPIO6) 和 `Audio_PWR_PIN` (GPIO42) 从硬件上切断墨水屏驱动板及音频功放芯片的 VCC 供电线，以防止微小的漏电损耗。
  - 子系统初始化顺序与配网门禁管理（为 AP 传图 HTTP 服务增加“USB充电状态下开启”及“电池供电下2分钟超时自动关闭”保护门禁）
  - 按键事件路由（UP/DOWN/BOOT click/long-press/double-click）
  - **RTC 定时同步看板模式 (Duty-Cycle Mode)**：利用 PCF8563 RTC 定时闹钟中断（每小时唤醒），唤醒设备同步天气/日历日程后刷新 EPD 并重回 Deep Sleep，以将待机时长增加至半年以上。
  - **低电量关机警示屏**：当电量低于 3% 时，利用 `custom_lcd_display` 强制在 EPD 上全屏渲染“电量耗尽，请充电”提示卡片，防止墨水屏留存失效的时钟和过时看板画面，最后关闭硬件 VBAT 电源轨。
  - **NFC 碰一碰唤醒**：配置 GT23SC6699 的 Field-Detect GPIO7 为 EXT0 外部唤醒源，支持用户用手机触碰设备时将 ESP32-S3 从 Deep Sleep 唤醒并执行业务配网。
- [ ] Commit

### Task 7.2: 集成 UI 管理器到应用

- [ ] 将 `ui_manager` 接入 Application
- [ ] 替换 `main.c` 的 `draw_page()` 为 UI 管理器的 `RenderAll`
- [ ] 配置按键回调路由到 UI 管理器
- [ ] Commit

### Task 7.3: 扩充 EPD 驱动

**参考:** `boards/zectrix-s3-epaper-4.2/custom_lcd_display.cc` (完整版)

- [ ] 对齐当前 `custom_lcd_display.c` 与 C++ 版功能
- [ ] 补充缺失的 EPD 命令序列
- [ ] Commit

### Task 7.4: 重构 CMakeLists.txt 与组件注册

- [ ] 建立组件化构建树，为 `bsp`, `rawdraw`, `ui`, `audio`, `network` 各自编写 `CMakeLists.txt`
- [ ] 配置依赖关系：在各组件的 `CMakeLists.txt` 中精准声明 `REQUIRES` (如 `audio` 依赖 `bsp` 与 `esp_codec_dev`，`ui` 依赖 `rawdraw`)
- [ ] 更新 `components/audio/idf_component.yml` 添加 `espressif/esp-sr` 依赖
- [ ] **Opus Resampler 定点数编译**：在编译 Opus 编解码组件时，确保在 `CMakeLists.txt` 中定义 `FIXED_POINT` 宏，采用定点数算法代替浮点算法进行重采样，避免硬件浮点单元竞争并降低延迟。
- [ ] Commit

### Task 7.5: 端到端测试

- [ ] 编译固件，确认 bin 大小在分区限制内
- [ ] 烧录到设备，验证：
  - 19 个页面都能正常切换
  - 状态栏正确显示（Wi-Fi/电池/时间）
  - 按键响应（短按/长按/双击）
  - Wi-Fi 连接和断开
  - 天气数据获取
  - 音频播放
  - 睡眠/唤醒
- [ ] Commit

### Task 7.6: 主机端测试扩充

- [ ] 扩充 `tests/` 下的主机端测试覆盖新增图形和布局函数
- [ ] Commit

---

## 风险和注意事项（跨任务通用）

### 1. Flash 空间

原 C++ 固件 bin 大小约 1.75MB，分区为 4MB（16MB Flash 的 0x3F0000 = ~4MB）。纯 C 应该更小，但加入 esp-sr 模型可能增大。注意监控 `check_sizes`。

### 2. 内存限制与碎片规避

ESP32-S3 有 512KB SRAM + PSRAM。

- 帧缓存与照片数据等大块内存分配必须保留 `MALLOC_CAP_SPIRAM` 分配策略存放在 PSRAM 中，避免占用宝贵的 SRAM。
- **规避内存碎片 (Heap Fragmentation)**：运行期频繁更新的数据应避免高频动态 `malloc`/`realloc`。尽可能在初始化阶段采用预分配策略（如 Linear Arena/环形缓冲区、页面结构体静态预分配等）以保证 O(1) 复杂度且零内存碎片。

### 3. 线程安全与 RTOS 任务同步

C++ 版大量使用 `std::mutex` 与 `std::condition_variable`。

- C 移植版统一使用 FreeRTOS `SemaphoreHandle_t` 锁。注意 C 语言没有 RAII 机制，必须确保在所有函数出口分支中手动 `unlock`。

### 4. SSD2683 四色墨水屏物理限制与防抖

- **刷新防抖 (Refresh Debouncing)**：墨水屏刷新延迟高且耗电。物理刷新中必须内置合并(coalesce)机制（典型防抖时间 300ms–500ms），防止频繁闪烁。
- **残影清理策略 (Ghosting Cleanup)**：四色墨水屏极易残留红色和黄色素。必须统计局部刷新次数，在刷新达到阈值（如 10 次）或用户切换大页面时自动触发一次全局清屏全刷新。

### 5. 渐进式验证

每个 Phase 结束后都应编译通过并能在设备上运行。不要积累太多未测试的改动。
