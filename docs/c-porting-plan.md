# ZecTrix EPD Panel — C++ 到纯 C 完整移植计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将原 C++ 项目（~49,000 行代码，271 个文件，不含字体）的所有功能完整移植到当前的纯 C ESP-IDF 工程。

**Architecture:** 原项目使用 C++ 面向对象架构（虚函数、智能指针、std::function 回调、STL 容器）。移植采用 C 的结构体 + 函数指针表模式，将每个 C++ 类映射为 `(struct + init/deinit + 方法函数)` 的等价 C 接口。UI 页面渲染器统一通过 `page_renderer_t` 虚表分发。图形/字体引擎直接操作 2bpp 帧缓冲区，无 LVGL 运行时依赖。

**Tech Stack:** ESP-IDF v6.0.2, ESP32-S3, SSD2683 4-color EPD (400×300), LVGL v9 字体格式（仅数据文件）, FreeRTOS, esp_codec_dev, cJSON, Opus 编解码器, Wi-Fi STA, BLE GATT

**原项目代码位置:** `/home/wert/GitHub/youn-ink-fourcolor-firmware/firmware/main/`

---

## 移植策略概览

### C++ 到 C 的映射模式

| C++ 特性 | C 替代方案 |
|---------|-----------|
| `class` + 成员函数 | `typedef struct` + `prefix_method_name(struct*, ...)` |
| `virtual` 方法表 | `struct vtable { 函数指针 }` |
| `std::unique_ptr<T>` | 手动 `malloc/free` + `init/deinit` |
| `std::function` 回调 | `void (*callback)(void* user_data)` |
| `std::vector<T>` | 固定容量数组 + `count` 字段 |
| `std::string` | `char[]` 固定缓冲 + `snprintf` |
| `std::mutex` | `SemaphoreHandle_t` |
| `namespace` | 前缀命名 (`rawdraw_`, `ui_`, `app_`) |
| `template` | 泛型 C 宏或类型特化 |

### 阶段划分

| 阶段 | 内容 | 预估文件数 | 难度 |
|------|------|-----------|------|
| Phase 1 | 图形引擎核心 + UI 框架基础 | ~15 | 中 |
| Phase 2 | UI 组件库（Widget Components） | ~14 | 中 |
| Phase 3 | UI 页面渲染器（前 8 页） | ~16 | 中-高 |
| Phase 4 | UI 页面渲染器（后 8 页） | ~14 | 高 |
| Phase 5 | 硬件外设 + 网络模块 | ~12 | 中 |
| Phase 6 | 音频管线 + AI 流式对话 | ~10 | 高 |
| Phase 7 | 集成 + 应用主框架 + 测试 | ~6 | 中 |

### 文件结构设计

```
main/
├── CMakeLists.txt
├── main.c                      # app_main 入口
├── config.h                    # 引脚定义、常量
├── app.h / app.c               # Application 单例 (Phase 7)
├── rawdraw/
│   ├── rawdraw.h / .c          # 2bpp 绘制原语 (已有,需扩充)
│   ├── rawdraw_ext.h / .c      # 扩展绘制：线条、圆形、进度条、测量 (Phase 1)
│   ├── framebuffer.h / .c      # 帧缓冲区管理 + dirty rect (Phase 1)
│   ├── style.h                 # 样式常量 (Phase 1)
│   ├── theme.h / .c            # 4色主题系统 (Phase 1)
│   ├── font_engine.h           # 字体引擎 (已有)
│   ├── layout.h / .c           # 布局工具函数 (Phase 1)
│   ├── clock.h / .c            # 时钟组件 (Phase 1)
│   └── components/
│       ├── widget.h            # Widget 虚表基类 (Phase 2)
│       ├── button.h / .c       (Phase 2)
│       ├── panel.h / .c        (Phase 2)
│       ├── card.h / .c         (Phase 2)
│       ├── list_item.h / .c    (Phase 2)
│       ├── progress_bar.h / .c (Phase 2)
│       ├── scrollview.h / .c   (Phase 2)
│       ├── slider.h / .c       (Phase 2)
│       ├── toggle.h / .c       (Phase 2)
│       ├── status_bar.h / .c   (Phase 2)
│       ├── footer_bar.h / .c   (Phase 2)
│       ├── modal.h / .c        (Phase 2)
│       ├── bubble.h / .c       (Phase 2)
│       ├── weather_card.h / .c (Phase 2)
│       └── voice_wakeup.h / .c (Phase 2)
├── ui/
│   ├── page_renderer.h         # PageRenderer 虚表 (Phase 1)
│   ├── ui_manager.h / .c       # RawDrawUiManager (Phase 4)
│   ├── epd_refresh.h / .c      # 刷新策略 (Phase 1)
│   └── renderers/
│       ├── chat_renderer.h / .c        (Phase 4)
│       ├── settings_renderer.h / .c    (Phase 4)
│       ├── weather_renderer.h / .c     (Phase 3)
│       ├── weather_detail_renderer.h / .c (Phase 3)
│       ├── news_renderer.h / .c        (Phase 4)
│       ├── ebook_renderer.h / .c       (Phase 3)
│       ├── wifi_renderer.h / .c        (Phase 3)
│       ├── photo_gallery.h / .c        (Phase 4)
│       ├── photo_detail_renderer.h / .c (Phase 4)
│       ├── calendar_renderer.h / .c    (Phase 3)
│       ├── almanac_renderer.h / .c     (Phase 4)
│       ├── lifebar_renderer.h / .c     (Phase 4)
│       ├── log_renderer.h / .c         (Phase 4)
│       ├── yearprogress_renderer.h / .c (Phase 4)
│       ├── font_debug_renderer.h / .c  (Phase 4)
│       ├── font_metrics_renderer.h / .c (Phase 4)
│       ├── ap_transfer_renderer.h / .c (Phase 4)
│       └── ap_transfer_server.h / .c   (Phase 4)
├── audio/
│   ├── audio_service.h / .c    # 音频管线 (Phase 6)
│   ├── audio_codec.h / .c      # 编解码抽象 (Phase 6)
│   ├── audio_player.h / .c     # 已有
│   └── wake_word.h / .c        # 唤醒词 (Phase 6)
├── common/
│   ├── sleep_manager.h / .c    # 已有,需对齐
│   ├── storage_manager.h / .c  # SPIFFS/LittleFS (Phase 5)
│   ├── weather_api.h / .c      # HTTP 天气API (Phase 5)
│   ├── holiday_fetcher.h / .c  # 节日API (Phase 5)
│   ├── photo_downloader.h / .c # 照片下载 (Phase 5)
│   ├── photo_storage.h / .c    # 照片存储 (Phase 5)
│   ├── nvs_state.h / .c        # NVS状态持久化 (Phase 5)
│   ├── bluetooth_manager.h / .c # BLE (Phase 5)
│   ├── ble_gatt_service.h / .c # BLE GATT (Phase 5)
│   └── ble_image_receiver.h / .c # BLE图片接收 (Phase 5)
├── streaming/
│   ├── stream_pipeline.h / .c  # AI对话管线 (Phase 6)
│   ├── text_chunker.h / .c     # 文本分块 (Phase 6)
│   └── tts_streamer.h / .c     # TTS流 (Phase 6)
├── protocols/
│   └── protocol.h / .c         # WSS协议 (Phase 6)
├── board.h / board.c           # 已有
├── charge_status.h / .c        # 已有
├── custom_lcd_display.h / .c   # 已有,需扩充
├── rtc_pcf8563.h / .c          # 已有
├── zectrix_nfc.h / .c          # 已有
├── settings.h / .c             # 已有,需扩充
├── wifi_manager.h / .c         # 已有
└── system_info.h / .c          # 系统信息 (Phase 5)

tests/
├── test_host.c                 # 已有
├── test_rawdraw.c              # 图形测试 (Phase 1)
├── test_framebuffer.c          # 帧缓冲测试 (Phase 1)
├── test_layout.c               # 布局测试 (Phase 1)
└── test_text_measure.c         # 文本测量测试 (Phase 1)
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
- `ui/renderers/rawdraw/page_renderer.h` → `ui/page_renderer.h`
- `ui/epd_refresh.cc` → `ui/epd_refresh.c`

### Task 1.1: 扩充 rawdraw 绘制原语

**Files:**
- Create: `main/rawdraw/rawdraw_ext.h`
- Create: `main/rawdraw/rawdraw_ext.c`
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
- Create: `main/rawdraw/framebuffer.h`
- Create: `main/rawdraw/framebuffer.c`
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
    uint32_t next_kick_ms;
} framebuffer_t;

void framebuffer_init(framebuffer_t* fb, uint8_t* buffer, int width, int height, SemaphoreHandle_t mutex);
void framebuffer_deinit(framebuffer_t* fb);
void framebuffer_clear(framebuffer_t* fb, rawdraw_color_t color);
void framebuffer_invalidate_rect(framebuffer_t* fb, int x, int y, int w, int h);
void framebuffer_invalidate_all(framebuffer_t* fb);
rawdraw_rect_t framebuffer_get_dirty(framebuffer_t* fb);
bool framebuffer_has_dirty(framebuffer_t* fb);
void framebuffer_clear_dirty(framebuffer_t* fb);
void framebuffer_request_refresh(framebuffer_t* fb, bool urgent);
void framebuffer_lock(framebuffer_t* fb);
void framebuffer_unlock(framebuffer_t* fb);
```

- [ ] **Step 1:** 创建 `framebuffer.h` 定义上述结构体和 API
- [ ] **Step 2:** 创建 `framebuffer.c` 移植 `framebuffer.cc` 逻辑
- [ ] **Step 3:** 编写 `tests/test_framebuffer.c` — 测试 dirty rect 合并、清除、refresh 回调
- [ ] **Step 4:** 编译运行验证
- [ ] **Step 5:** Commit

### Task 1.3: 样式常量 + 布局工具

**Files:**
- Create: `main/rawdraw/style.h`
- Create: `main/rawdraw/layout.h`
- Create: `main/rawdraw/layout.c`
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
- Create: `main/rawdraw/theme.h`
- Create: `main/rawdraw/theme.c`

**参考:** `rawdraw/theme.h` (3.4KB) + `rawdraw/theme.cc` (16KB)

- [ ] **Step 1:** 创建 `theme.h` — 将 C++ `enum class ThemeId` → C `enum`，`PaintStyle` struct，`ThemeDefinition` struct，`ThemeManager` → C 函数集
- [ ] **Step 2:** 创建 `theme.c` — 移植 6 个主题定义数据表和查询逻辑
- [ ] **Step 3:** 移植 `DrawStyledRect`, `DrawStyledRoundRect`, `DrawStyledText` 等 styled 绘制函数
- [ ] **Step 4:** Commit

### Task 1.5: 页面渲染器虚表 + EPD 刷新策略

**Files:**
- Create: `main/ui/page_renderer.h`
- Create: `main/ui/epd_refresh.h`
- Create: `main/ui/epd_refresh.c`

**参考:** `ui/renderers/rawdraw/page_renderer.h` + `ui/epd_refresh.cc` (5.6KB) + `ui/epd_refresh.h` (4.3KB)

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
struct page_renderer {
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
    // 通用状态
    int width;
    int height;
    bool needs_full_refresh_flag;
    void* user_data; // 各渲染器的私有数据
};
```

- [ ] **Step 1:** 创建 `page_renderer.h` 虚表定义
- [ ] **Step 2:** 创建 `epd_refresh.h` + `epd_refresh.c` — 移植刷新策略（full/partial 判定、帧差异分析已有）
- [ ] **Step 3:** Commit

### Task 1.6: 时钟组件

**Files:**
- Create: `main/rawdraw/clock.h`
- Create: `main/rawdraw/clock.c`

**参考:** `rawdraw/clock.cc` (3.3KB) + `rawdraw/clock.h`

- [ ] **Step 1:** 移植 Clock 组件 — 显示 HH:MM，支持多字体和颜色
- [ ] **Step 2:** Commit

---

## Phase 2: UI 组件库（Widget Components）

**目标:** 移植 rawdraw/components/ 下所有 UI 组件。每个组件从 C++ class 移植为 C struct + 方法函数集。

**参考源文件:** `rawdraw/components/` 目录下 14 个组件，共 ~90KB

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

## Phase 3: UI 页面渲染器（第一批 — 8 页）

**目标:** 移植前 8 个页面渲染器。每个渲染器实现 `page_renderer_t` 虚表。

### Task 3.1: WeatherRenderer（天气页面）
**参考:** `renderers/rawdraw/weather_renderer.cc` (15.4KB)
- [ ] 移植天气页面（当前天气卡片 + 7 日预报列表）
- [ ] Commit

### Task 3.2: WeatherDetailRenderer（天气详情页面）
**参考:** `renderers/rawdraw/weather_detail_renderer.cc` (9.4KB)
- [ ] 移植天气详情（24 小时温度曲线、风向、湿度、紫外线）
- [ ] Commit

### Task 3.3: EbookRenderer（电子书阅读器）
**参考:** `renderers/rawdraw/ebook_renderer.cc` (14.0KB)
- [ ] 移植电子书（分页渲染、进度条、上下翻页）
- [ ] Commit

### Task 3.4: WifiRenderer（Wi-Fi 配置页面）
**参考:** `renderers/rawdraw/wifi_renderer.cc` (17.5KB)
- [ ] 移植 Wi-Fi 页面（SSID 列表、连接状态、AP 配置二维码）
- [ ] Commit

### Task 3.5: CalendarRenderer（日历页面）
**参考:** `renderers/rawdraw/calendar_renderer.cc` (6.0KB)
- [ ] 移植日历页面（嵌入 Calendar 组件、月份导航、今日高亮）
- [ ] Commit

### Task 3.6: PhotoGalleryRenderer（照片相册）
**参考:** `renderers/rawdraw/photo_gallery.cc` (27.0KB)
- [ ] 移植照片相册（4色抖动渲染、幻灯片、列表视图）
- [ ] Commit

### Task 3.7: PhotoDetailRenderer（照片详情）
**参考:** `renderers/rawdraw/photo_detail_renderer.cc` (9.3KB)
- [ ] 移植照片详情页（全屏查看、缩放、删除）
- [ ] Commit

### Task 3.8: ChatRenderer（AI 对话页面）
**参考:** `renderers/rawdraw/chat_renderer.cc` (23.6KB)
- [ ] 移植对话页面（消息列表、流式文本追加、滚动、状态气泡）
- [ ] Commit

---

## Phase 4: UI 页面渲染器（第二批 — 9 页）+ UI 管理器

### Task 4.1: SettingsRenderer（设置面板）
**参考:** `renderers/rawdraw/settings_renderer.cc` (83.7KB) — **最大文件**
- [ ] 移植设置面板（侧边栏分类、列表项、开关、滑块、模态对话框、主题选择、关于页面）
- [ ] 注意：此文件极大，可能需要拆分为 `settings_renderer.c` + `settings_dialogs.c` + `settings_about.c`
- [ ] Commit

### Task 4.2: NewsRenderer（新闻页面）
**参考:** `renderers/rawdraw/news_renderer.cc` (14.8KB)
- [ ] 移植新闻页面（标题列表 + 详情展开）
- [ ] Commit

### Task 4.3: LifeBarRenderer（寿命倒数页面）
**参考:** `renderers/rawdraw/lifebar_renderer.cc` (11.9KB)
- [ ] 移植寿命倒数（周数网格、进度条、统计数据）
- [ ] Commit

### Task 4.4: AlmanacRenderer（老黄历页面）
**参考:** `renderers/rawdraw/almanac_renderer.cc` (10.1KB)
- [ ] 移植老黄历（宜忌、农历、节气）
- [ ] Commit

### Task 4.5: LogRenderer（日志查看器）
**参考:** `renderers/rawdraw/log_renderer.cc` (9.4KB)
- [ ] 移植日志页面（ESP_LOG 缓冲区、级别过滤、滚动）
- [ ] Commit

### Task 4.6: YearProgressRenderer（年度进度页面）
**参考:** `renderers/rawdraw/yearprogress_renderer.cc` (14.1KB)
- [ ] 移植年度进度（全年日历热力图、百分比统计）
- [ ] Commit

### Task 4.7: FontDebugRenderer + FontMetricsRenderer
**参考:** `font_debug_renderer.cc` (3.8KB) + `font_metrics_renderer.cc` (3.8KB)
- [ ] 移植字体调试页面（字符网格、度量信息）
- [ ] Commit

### Task 4.8: ApTransferRenderer + ApTransferServer
**参考:** `ap_transfer_renderer.cc` (10.4KB) + `ap_transfer_server.cc` (47.4KB)
- [ ] 移植 AP 传图功能（HTTP 服务器、照片上传、Wi-Fi AP 模式切换）
- [ ] 注意：HTTP 服务器移植复杂，需使用 `esp_http_server`
- [ ] Commit

### Task 4.9: RawDrawUiManager（UI 管理器）
**参考:** `ui/rawdraw_ui_manager.cc` (69.6KB) + `rawdraw_ui_manager.h` (17.5KB)
- [ ] 移植 UI 管理器核心：
  - 页面注册表（17 个页面的渲染器实例）
  - 页面切换（清屏 + 重新渲染）
  - 按键事件路由到当前页面
  - 状态栏 + 页面内容渲染调度
  - Quick Switch 快速切换覆盖层
  - 时钟刷新定时器
  - 幻灯片定时器
  - 帧缓冲刷新回调
- [ ] Commit

---

## Phase 5: 硬件外设 + 网络模块

**目标:** 移植所有网络通信和存储模块。已有模块（board, rtc_pcf8563, zectrix_nfc, audio_player, settings, wifi_manager, charge_status, sleep_manager）需对齐接口。

### Task 5.1: StorageManager（文件系统管理）
**参考:** `common/storage_manager.cc` (4.6KB)
- [ ] 移植 SPIFFS/LittleFS 挂载、文件读写封装
- [ ] Commit

### Task 5.2: NVS State（状态持久化）
**参考:** `common/nvs_state.cc` (7.1KB)
- [ ] 移植 NVS 键值持久化（主题选择、音量、亮度、Wi-Fi 配置等）
- [ ] Commit

### Task 5.3: WeatherApi（天气 API 客户端）
**参考:** `common/weather_api.cc` (13.1KB)
- [ ] 移植 HTTP 天气请求、JSON 解析、数据缓存
- [ ] Commit

### Task 5.4: HolidayFetcher（节日获取）
**参考:** `common/holiday_fetcher.cc` (9.7KB)
- [ ] 移植节假日 API 请求和缓存
- [ ] Commit

### Task 5.5: PhotoDownloader + PhotoStorage
**参考:** `common/photo_downloader.cc` (12KB) + `common/photo_storage.cc` (17.1KB)
- [ ] 移植照片下载（HTTP）、4色量化、SPIFFS 存储
- [ ] Commit

### Task 5.6: BluetoothManager + BleGattService + BleImageReceiver
**参考:** `common/bluetooth_manager.cc` (6.1KB) + `ble_gatt_service.cc` (13.7KB) + `ble_image_receiver.cc` (3.1KB)
- [ ] 移植 BLE 初始化、GATT 服务注册、图片接收
- [ ] Commit

### Task 5.7: SystemInfo
**参考:** `system_info.cc` (5.0KB)
- [ ] 移植系统信息获取（MAC 地址、芯片型号、SDK 版本、可用内存）
- [ ] Commit

### Task 5.8: Kconfig 配置选项
**参考:** `Kconfig.projbuild` (1.4KB)
- [ ] 移植菜单配置选项（Wi-Fi SSID/密码、服务器地址、天气 API Key 等）
- [ ] Commit

---

## Phase 6: 音频管线 + AI 流式对话

**目标:** 移植音频服务和 AI 对话流式管线。这是最复杂的阶段。

**参考:**
- `audio/audio_service.cc` (29.4KB) — 音频核心管线
- `audio/audio_service.h` (5.6KB)
- `streaming/stream_pipeline.cc` (4.7KB) — AI 对话管线
- `streaming/tts_streamer.cc` (5.6KB) — TTS 流式播放
- `streaming/text_chunker.cc` (5.5KB) — 文本分块
- `protocols/protocol.h` (4.4KB) — WebSocket 协议

### Task 6.1: Protocol（WSS 协议层）
**参考:** `protocols/protocol.h`
- [ ] 移植 WebSocket 协议（消息收发、心跳、Opus 数据包封装）
- [ ] C++ `Protocol` 抽象基类 → C 函数指针回调接口
- [ ] Commit

### Task 6.2: AudioCodec（编解码器抽象）
**参考:** `audio/audio_codec.cc/h`
- [ ] 移植编解码器接口抽象（I2S 配置、ES8311 初始化 — 已有部分在 `audio_player.c`）
- [ ] Commit

### Task 6.3: WakeWord（唤醒词检测）
**参考:** `audio/wake_word.h` + `esp-sr` 组件集成
- [ ] 移植唤醒词检测（ESP-SR 模型加载、VAD、关键词识别）
- [ ] Commit

### Task 6.4: AudioService（音频核心管线）
**参考:** `audio/audio_service.cc` (29.4KB) — **核心难点**
- [ ] 移植音频管线：
  - MIC → 处理器 → Opus 编码 → 发送队列
  - 接收队列 → Opus 解码 → 扬声器
  - 3 个 FreeRTOS 任务管理
  - 事件组状态机
- [ ] C++ `std::deque<std::unique_ptr<AudioStreamPacket>>` → C 固定容量环形缓冲区
- [ ] Commit

### Task 6.5: StreamPipeline + TTS Streamer + TextChunker
**参考:** `streaming/stream_pipeline.cc` + `tts_streamer.cc` + `text_chunker.cc`
- [ ] 移植 AI 对话流：
  - 接收 WSS 文本流 → 分块 → 追加到 ChatRenderer
  - 接收 Opus 音频 → 解码 → 播放
  - TTS 流式播放管理
- [ ] Commit

---

## Phase 7: 集成 + 应用主框架

**目标:** 将所有模块集成到应用主框架，替换当前 3 页 demo 为完整的 17 页 UI 系统。

### Task 7.1: Application 单例
**参考:** `application.cc` (24.9KB) + `application.h`
- [ ] 移植 Application 核心：
  - 设备状态机（`DeviceState`）
  - 子系统初始化顺序
  - 按键事件路由（UP/DOWN/BOOT click/long-press/double-click）
  - 睡眠定时器
  - Wi-Fi 配置模式
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

### Task 7.4: 扩充 CMakeLists.txt
- [ ] 更新 `main/CMakeLists.txt` 注册所有新源文件
- [ ] 更新 `main/idf_component.yml` 添加新依赖（esp_http_server, esp-sr 等）
- [ ] Commit

### Task 7.5: 端到端测试
- [ ] 编译固件，确认 bin 大小在分区限制内
- [ ] 烧录到设备，验证：
  - 17 个页面都能正常切换
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

## 风险和注意事项

### 1. Flash 空间
原 C++ 固件 bin 大小约 1.75MB，分区为 4MB（16MB Flash 的 0x3F0000 = ~4MB）。纯 C 应该更小，但加入 esp-sr 模型可能增大。注意监控 `check_sizes`。

### 2. 内存限制
ESP32-S3 有 512KB SRAM + PSRAM。C++ 版使用 PSRAM 存放帧缓冲和照片。移植时需保留 `MALLOC_CAP_SPIRAM` 分配策略。`std::vector` → 固定数组时需合理估算容量。

### 3. 线程安全
C++ 版大量使用 `std::mutex`。C 版统一使用 FreeRTOS `SemaphoreHandle_t`。注意 C 没有 RAII，需手动 lock/unlock。

### 4. SettingsRenderer 拆分
`settings_renderer.cc` 83.7KB 是最大单文件。建议拆分为 3-4 个 C 文件：核心渲染、对话框、关于页、主题选择。

### 5. AP Transfer Server
`ap_transfer_server.cc` 47.4KB 包含完整 HTTP 服务器。需移植为 `esp_http_server` 组件的 C handler 注册模式。

### 6. esp-sr 依赖
唤醒词和 VAD 依赖 `espressif/esp-sr` 组件，需要额外的模型文件和 PSRAM 配置。在 Phase 6 中处理。

### 7. 渐进式验证
每个 Phase 结束后都应编译通过并能在设备上运行。不要积累太多未测试的改动。
