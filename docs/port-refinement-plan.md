# ZecTrix EPD Panel — 移植完善计划（基于全项目 Review）

## Context

`docs/c-porting-plan.md` 的 Phase 1–3、5–7 任务已基本完成。三轮逐文件 Review（图形/控件、19 个页面、网络/BSP/HAL/存储）证实移植**高度忠实于原 C++ 项目**：图形引擎与控件库 20/21 完整（唯一未移植的 `layers.cc` 在原 C++ 项目中是死代码，无任何调用方）；网络与存储模块全部完整且多处强于 C++；页面渲染器逐一 1:1 对齐。

**真正的缺口集中在集成层，不在渲染/驱动/存储层。** 本计划修复这些已确认的集成缺口，并执行用户指定的结构调整：① 板级模块从 `main/` 迁移合并到已有的 `components/bsp/`；② Phase 4 仅做骨架（Protocol/WebSocket + StreamPipeline + TextChunker 文本流→Chat 页面），音频/Opus/唤醒词整体延后。音频/AI 流式对话是当前唯一缺失的大块功能（`application.h` 注释明确 "intentionally parked"）。

## 确认的问题清单（按优先级）

| # | 问题 | 证据 | 严重度 |
|---|------|------|--------|
| G0 | **按钮失效（已烧录验证）**：UP/DOWN/BOOT 单击在第一次操作后全部失效，无法翻页 | `ui_manager.c:861` 在 navigation click 被 renderer 处理后置 `input_refresh_locked = true`，但**全文件无任何清除点**（grep 确认仅 `:672` init 时 `= false`）。C++ 原版 `rawdraw_ui_manager.cc:366-369` 在 `lcd_->SetOnRefreshIdle` 回调中清除该锁；C 版有 `set_on_refresh_idle`（`custom_lcd_display.h:89`）但 **ui_manager 从未注册过它**（grep 确认全项目 `main/`+`components/ui/` 无 `set_on_refresh_idle` 调用）。结果：首次 click 后锁恒为 true，后续所有 navigation click 被 `:822-825` 直接丢弃。 | 致命 |
| G0b | **页面切换入口缺失**：UP/DOWN 双击未注册 → quick_switch 无法打开 | `main.c:254-260` 仅给 BOOT 注册了 `BUTTON_DOUBLE_CLICK`；UP/DOWN 无双击注册。但 `ui_manager.c:841` 用 `BTN_UP_DOUBLE_CLICK` 开关 quick_switch（页面切换唯一入口）。此外长按后误触发单击（C++ `:361-364` 有 `s_up_suppress_click` 抑制，C 版无）。 | 致命 |
| G1 | 相册幻灯片完全失效：定时器从未创建/启动 | `ui_manager.c:143` 有 `gallery_slideshow_timer` 字段，但全局无 `esp_timer_create`/`esp_timer_start_once`/arm 回调；`gallery_slideshow_pending` 永不为 true。C++ `rawdraw_ui_manager.cc:410-421,1344-1377` 有完整 `ArmGallerySlideshowTimer`/`OnGallerySlideshowTimer`/`AdvanceGallerySlideshow` | 高 |
| G2 | AP 传图 4 个回调全部注册为 NULL | `ui_manager.c:702-705` 四个 `set_*_callback(..., NULL, mgr)`；WiFi 传图后不刷新相册、不自动选中新照片、`/photo/show` 端点恒返回 not_found、网页改幻灯片间隔不到达 manager。C++ `rawdraw_ui_manager.cc:297-320` 全部接线 | 高 |
| G3 | 睡眠前电源轨未切断 | `board.c:165-205` 定义 `board_power_*_off()` 全套，但 `application.c:431-514` 的 `enter_scheduled_sleep`/`application_enter_manual_sleep` 从不调用任何 `*_off`，仅 `esp_wifi_disconnect/stop` 后直接 `esp_deep_sleep_start`。漏电损耗 | 高 |
| G4 | 低电量（<3%）无关机警示屏 | `application.c` 无 `charge_status_get_battery_percent()` 的阈值检查与强制渲染逻辑（plan Task 7.1 要求） | 中 |
| G5 | PCF8563 RTC 闹钟唤醒未接入睡眠状态机 | `rtc_pcf8563.c:81-162` 闹钟+倒计时寄存器操作完整移植，但 `application.c` 睡眠路径仅用 `esp_sleep_enable_ext0_wakeup(BOOT_BUTTON)`。plan Task 7.1 "RTC 定时同步看板模式"未实现。**注**：C++ `application.cc:511` 同样未接入（生产也仅 BOOT 唤醒），故非回归，但属计划明确要求 | 中 |
| G6 | Phase 4 音频/AI 流式对话整体缺失 | 无 `components/audio/`；`application.h` 注释 "Phase 4 audio pipeline is parked"；`main/idf_component.yml` 无 `esp-sr`/`esp-opus-encoder` 依赖 | 大（已决定延后音频，仅做骨架） |
| G7 | 板级模块结构偏离：硬件驱动+板级服务全在 `main/` 而非组件目录 | `main/CMakeLists.txt` 把 `board/custom_lcd_display/rtc_pcf8563/zectrix_nfc/audio_player/wifi_manager/charge_status/settings/sleep_manager` 全编进 main 组件，而 `components/bsp/` 只有 4 个文件 | 中 |

**Review 中确认为「非问题」的项（不在本计划处理）**：
- `photo_storage` 4 色量化/抖动：C++ 与 C 均不做，服务端预量化；渲染层 `photo_gallery_page.c:48-66` 已支持 1bpp+2bpp(4 色) 两种格式。
- `photo_downloader` 客户端 resize：C++ 也没有。
- `custom_lcd_display` 1bpp 分支：SSD2683 硬件为 4 色专用，1bpp 分支在 C++ 中也仅作 `IsFourColorPanel()` 回退，删减无影响。
- `rtc_pcf8563` ISR 回调/`factory_test_service`：原项目仅工厂测试自检用，未移植该服务，非回归。
- `wifi_manager` FAST_RC 快速重连缓存：性能优化，非功能缺口，列为可选增强（见 Assumptions）。

---

## Approach

### Phase A — 板级模块迁移合并到 `components/bsp/`（G7）

将 `main/` 下的 9 个板级模块（硬件驱动 + 板级服务）迁入**已有的** `components/bsp/` 组件，与其中现有的 4 个文件（`storage_manager`/`nvs_state`/`system_info`/`epd_refresh`）合并为一个 13 文件的 BSP 组件。**纯结构搬迁 + 依赖声明修正，无行为变更**。

这些模块（board/custom_lcd_display/rtc_pcf8563/zectrix_nfc/audio_player/wifi_manager/charge_status/settings/sleep_manager/config.h）全部绑死在 ZecTrix-S3 板的引脚与芯片拓扑上，是 BSP（Board Support Package）而非可移植的 HAL。用 `bsp/` 符合 ESP-IDF 生态约定（Espressif 官方 `esp-bsp` 及所有官方 BSP 均用此命名）。合并消除人为的 hal/bsp 二分边界。

- **A1**：移动 `main/{board,custom_lcd_display,rtc_pcf8563,zectrix_nfc,audio_player,wifi_manager,charge_status,settings,sleep_manager}.c` 及其 `.h`、`config.h` 到 `components/bsp/`（`.c`）与 `components/bsp/include/`（`.h` + `config.h`）。目标 `components/bsp/include/` 将含：迁入的 9 个 `.h` + `config.h` + 已有的 `storage_manager.h`/`nvs_state.h`/`system_info.h`/`epd_refresh.h`。`application.c`/`application.h`/`main.c` 保留在 `main/`（应用层）。
- **A2**：更新 `components/bsp/CMakeLists.txt`（当前 `:1-3` 只有 4 个 SRCS + `REQUIRES rawdraw littlefs nvs_flash`）：SRCS 追加上述 9 个 `.c`；REQUIRES 合并为 `REQUIRES driver esp_driver_spi esp_driver_i2c esp_codec_dev esp_wifi esp_driver_i2s esp_adc rawdraw`；`PRIV_REQUIRES nvs_flash littlefs`。
- **A3**：更新 `main/CMakeLists.txt`（当前 `:1-4`）：SRCS 从 11 个缩减为 `main.c application.c`；REQUIRES 改为 `bsp rawdraw network ui lvgl nvs_flash`。移除已迁出的源文件与直接 BSP 依赖（`esp_codec_dev`/`esp_wifi`/`esp_driver_i2s`/`esp_adc` 等现在由 bsp 组件声明）。`main/idf_component.yml` 不变。
- **A4**：include 路径**无需修改**：`main.c`/`application.c` 当前用 `#include "board.h"` 等无前缀形式（确认 `application.c:26-32`、`main.c:15-24`）；`components/bsp/` 的 `INCLUDE_DIRS "include"` 使这些头文件在 `REQUIRES bsp` 后仍以无前缀解析。bsp 内部 `.c` 之间的相互 include（如 `board.c` include `charge_status.h`）同理无需改。
- **依赖顺序**：A1→A2→A3→A4。每步后检查 include 一致性。

### Phase B — 集成缺口修复（G0–G5）

#### B0 — 修复按钮失效：注册刷新空闲回调解锁输入（G0，最高优先）

文件：`components/ui/ui_manager.c` + `main/main.c`

**根因**：`ui_manager.c:861` 在 navigation click 被 renderer 成功处理后置 `mgr->input_refresh_locked = true`，意在 EPD 物理刷新期间阻止新输入。但该锁**仅**在 init（`:672`）置 false，运行期无清除点。C++ 原版在 `rawdraw_ui_manager.cc:366-369` 通过 `lcd_->SetOnRefreshIdle` 回调清除；C 版有 `set_on_refresh_idle` API（`custom_lcd_display.c:647`，签名 `void set_on_refresh_idle(void (*cb)(void*), void* user_data)`）但**从未被调用**。

- **B0.1**：在 `ui_manager.c` 新增静态回调 `static void on_display_refresh_idle_cb(void* user_data)`：`ui_manager_t* mgr = (ui_manager_t*)user_data;` 置 `mgr->input_refresh_locked = false;`（移植 C++ `:367`）。
- **B0.2**：在 `ui_manager_init`（`:663-709`）末尾、`init_renderer` 之后，调 `set_on_refresh_idle(on_display_refresh_idle_cb, mgr)`——注册回调，使 EPD 刷新完成后自动解锁。**注意**：`set_on_refresh_idle` 是 `custom_lcd_display` 的全局函数（`custom_lcd_display.h:89`），`ui_manager.c` 需 `#include "custom_lcd_display.h"`。
- **B0.3**：**`main.c` 的回调注册时机**——当前 `main.c:268-271` 在 `application_init` 之后调 `ui_manager_set_refresh_callback(mgr, ui_refresh_cb, NULL)`。`set_on_refresh_idle` 在 `ui_manager_init`（B0.2，由 `application_init` 调用）内部注册，早于 `main.c` 的 refresh callback 设置——顺序正确（`on_refresh_idle` 是 EPD 刷新任务调用的，与 refresh callback 独立，无先后依赖）。

**边界**：`on_display_refresh_idle_cb` 可能在 EPD 刷新任务上下文中被调用（`custom_lcd_display.c:491,529`），只做单个 `bool` 赋值（原子性足够，单字节写），不需额外锁。若 `mgr` 为 NULL（理论上不会，因 init 注册时传入了 mgr）则提前 return。

**验证**：烧录后连续按 UP/DOWN 单击 5+ 次，每次都应触发页面内交互（如照片切换/列表滚动）；按 UP 双击应打开 quick_switch 覆盖层并在其中用 UP/DOWN 导航——不再出现"第一次有效、之后全部静默"。日志应周期性出现 `Display refresh idle; input unlocked`（移植 C++ `:368` 的日志）。


#### B0b — 修复按钮事件注册缺失（G0b，与 B0 同优先）

文件：`main/main.c:254-260`

**根因**：C 版 `main.c:254-260` 仅注册了 3 类事件：`BUTTON_SINGLE_CLICK`（UP/DOWN/BOOT）、`BUTTON_LONG_PRESS_START`（UP/DOWN/BOOT）、`BUTTON_DOUBLE_CLICK`（仅 BOOT）。对照 C++ `zectrix-s3-epaper-4.2.cc:349-465` 的完整按钮状态机，缺失两类关键事件：

1. **UP/DOWN 双击未注册**：`ui_manager_handle_input:841` 检查 `BTN_UP_DOUBLE_CLICK` 来开关 quick_switch 覆盖层（页面切换的唯一入口），但 `main.c:260` 仅给 `confirm_btn`（BOOT）注册了 `BUTTON_DOUBLE_CLICK`——UP/DOWN 的双击事件永远不会产生，**quick_switch 无法打开，无法切换页面**。
2. **UP/DOWN `OnPressDown`/`OnPressUp` 缺失**：C++ `:351-359,372-385,388-396,409-422` 用 `OnPressDown`/`OnPressUp` 跟踪两键同时按下（`s_up_held`/`s_down_held`）实现 UP+DOWN 长按组合进入 WiFi 配置模式（`:426-434,436-444` `EnterWifiConfigComboOnce`）。C 版无等价注册，**UP+DOWN 组合键不工作**。
3. **长按抑制单击缺失**：C++ `:374-377,411-414` 在 `OnPressUp` 中检测长按后置 `s_up_suppress_click=true`，使随后的 `OnClick` 被丢弃（`:361-364`）；C 版 `BUTTON_LONG_PRESS_START` 后 `BUTTON_SINGLE_CLICK` 仍会触发，导致长按后再误触发单击。

- **B0b.1**：在 `main.c:254-260` 新增 `iot_button_register_cb(up_btn, BUTTON_DOUBLE_CLICK, NULL, button_up_double_click_cb, NULL)` 及对应回调 `button_up_double_click_cb` → `application_on_up_double_click()`（需在 `application.h/.c` 新增该函数，内部调 `ui_manager_handle_input(mgr, &(ui_button_event_t){BTN_UP_DOUBLE_CLICK})`）。
- **B0b.2**：**长按抑制单击**：在 `main.c` 的 `button_up_long_press_cb`/`button_down_long_press_cb` 中置 static flag `s_suppress_next_up_click`/`s_suppress_next_down_click = true`；在 `button_up_click_cb`/`button_down_click_cb` 开头检查并清除该 flag（为 true 则直接 return 不转发），移植 C++ `:361-364,398-401` 的抑制逻辑。这是最小改动方案（不需移植完整的 PressDown/PressUp 状态机）。
- **B0b.3**（可选，后续）：UP+DOWN 组合键进 WiFi 配置——需要 `OnPressDown`/`OnPressUp` 级跟踪（`BUTTON_PRESS_DOWN`/`BUTTON_PRESS_UP` 事件）。当前 WiFi 配置可通过 settings 菜单进入，故**标记为可选**；若做，需注册 `BUTTON_PRESS_DOWN`/`BUTTON_PRESS_UP` 事件并移植 `s_up_held`/`s_down_held` 原子标志 + 组合判定逻辑。

**验证**：烧录后按 UP 双击 → quick_switch 覆盖层打开（之前无反应）；长按 UP 后松开 → 不触发额外单击导航（之前长按后会误触发一次单击）。


#### B1 — 修复相册幻灯片（G1）

文件：`components/ui/ui_manager.c`

- **B1.1**：在 `ui_manager.c` 新增静态函数 `arm_gallery_slideshow_timer(ui_manager_t* mgr)`，移植 C++ `rawdraw_ui_manager.cc:1344-1354`：若 `mgr->gallery_slideshow_timer==NULL` 先 `esp_timer_create`——**关键**：`esp_timer_create_args_t.arg` 必须设为 `mgr`（仿 `application.c:483-485` 已有 timer 模式，但那里 `arg=NULL`；此处必须传 `mgr`，因 `on_gallery_slideshow_timer` 需通过 arg 取回实例——`ui_manager` 无模块级单例）；`esp_timer_stop(mgr->gallery_slideshow_timer)`；若 `mgr->gallery_slideshow_interval_minutes<=0` 返回；否则 `esp_timer_start_once(mgr->gallery_slideshow_timer, (uint64_t)interval*60*1000000)`。
- **B1.2**：新增 `static void on_gallery_slideshow_timer(void* arg)`：移植 C++ `rawdraw_ui_manager.cc:1356-1361`，`ui_manager_t* mgr = (ui_manager_t*)arg;`（arg 由 B1.1 的 create 设为 mgr）；置 `mgr->gallery_slideshow_pending = true`。不在此回调内直接刷新屏幕（刷新在 pump 中合并触发，避免 ISR/task 上下文直接调 render）。
- **B1.3**：新增 `static bool advance_gallery_slideshow(ui_manager_t* mgr)`，移植 C++ `rawdraw_ui_manager.cc:1363-1377`：先 `arm_gallery_slideshow_timer`（重新 arm）；若当前页非 `UI_PAGE_GALLERY` 直接返回 false；调用 `photo_gallery_select_next((page_renderer_t*)&mgr->gallery, true)`（签名已确认 `photo_gallery_page.h:67`，第二参 `wrap` 传 true 循环到首张）；`ui_manager_request_active_page_refresh(mgr)`。
- **B1.4**：修改 `ui_manager_set_gallery_slideshow_interval_minutes`（`ui_manager.c:1085-1088`）：在设值后调用 `arm_gallery_slideshow_timer(mgr)` 并置 `gallery_slideshow_pending=false`（移植 C++ `:1334-1336`）。
- **B1.5**：修改 `ui_manager_pump_clock_refresh`（`ui_manager.c:1103-1117`）：当前代码先 `page_pending = ...; mgr->active_page_refresh_pending = false`，再 `if(page_pending||transient||slideshow_pending) refresh_cb(...)`。改为：在 `slideshow_pending` 为 true 时，先调 `advance_gallery_slideshow(mgr)`（其内部已 `request_active_page_refresh`，即置 `active_page_refresh_pending=true`）；然后照常执行现有的 `page_pending = mgr->active_page_refresh_pending; ... if(page_pending||...) refresh_cb(...)`。即 `advance` 在合并 `page_pending` 之前执行，让 `advance` 触发的 refresh 与其它 pending 合并为一次 `refresh_cb` 调用，对齐 C++ `:1463-1466`。
- **B1.6**：在 `ui_manager_init` 创建 timer（移植 C++ `:410-420`），或在 `arm_gallery_slideshow_timer` 内懒创建（C++ 是 init 时创建；懒创建更简单，选懒创建）。

**边界**：`interval<=0` 不创建/停止 timer；非 Gallery 页 advance 只重新 arm 不切图；timer 已存在则 stop 后 start_once。

#### B2 — 接线 AP 传图回调（G2）

文件：`components/ui/ui_manager.c`（`ui_manager_init` 中 `:702-705`）

移植 C++ `rawdraw_ui_manager.cc:297-320` 的四个 lambda 为静态函数，替换 NULL：

- **B2.1** `image_received_cb(const char* photo_id, void* ctx)`：调 `photo_gallery_refresh_photo_list((page_renderer_t*)&mgr->gallery)`；取 `int count = photo_gallery_get_photo_count((page_renderer_t*)&mgr->gallery)`；若 `count>0` 调 `photo_gallery_set_selected_by_id((page_renderer_t*)&mgr->gallery, photo_id)`——**始终用回调传入的 photo_id**（不选"最后一帧"）：`photo_id` 是 AP server 分配并回传的，必定指向刚上传的照片；`set_selected_by_id` 找不到时返回 false，此时不报错（列表已刷新，默认选中首张）。`ui_manager_request_active_page_refresh(mgr)`。对齐 C++ `:297-305`。
- **B2.2** `settings_changed_cb(int minutes, void* ctx)`：调 `ui_manager_set_gallery_slideshow_interval_minutes(mgr, minutes)`；更新设置项索引 3（`APP_SETTINGS_SLIDESHOW_INDEX`）的显示值为 `关闭` 或 `Nmin`；`ui_manager_request_active_page_refresh(mgr)`。对齐 C++ `:306-312`。
- **B2.3** `photos_changed_cb(void* ctx)`：调 `photo_gallery_refresh_photo_list`。对齐 C++ `:313-317`。
- **B2.4** `show_photo_cb(const char* photo_id, void* ctx) → bool`：直接 `return ui_manager_show_photo_by_id(mgr, photo_id)`（已有函数 `:1094-1097`）。对齐 C++ `:318-320`。
- **B2.5**：将 `ui_manager_init`（`:702-705`）四个 NULL 替换为上述四个静态函数指针（ctx 传 `mgr`，已有）。

**签名均已确认**（`photo_gallery_page.h:61-71`）：`photo_gallery_refresh_photo_list(page_renderer_t*)`、`photo_gallery_get_photo_count(const page_renderer_t*) → int`、`photo_gallery_set_selected_by_id(page_renderer_t*, const char* id) → bool`、`photo_gallery_select_next(page_renderer_t*, bool wrap) → bool`。回调 typedef 在 `ap_transfer_server.h:47-51` 已定义。

#### B3 — 睡眠前切断电源轨（G3）

文件：`main/application.c`（`enter_scheduled_sleep :431-449` 与 `application_enter_manual_sleep :499-514`）

- **B3.1**：新增静态辅助 `static void power_down_peripherals_for_sleep(void)`：调用 `board_power_epd_off()`、`board_power_audio_off()`、`board_power_amp_off()`（三个 GPIO 已在 `board.c` 实现，含 `gpio_hold_en` 保持睡眠期间低电平）。**不调 `board_power_vbat_off()`**（VBAT 切断会断 RTC/唤醒源；plan 仅要求 EPD+Audio 功放）。`esp_wifi_disconnect`/`stop` 保持现状。
- **B3.2**：在 `enter_scheduled_sleep`（`:443-448`，`esp_deep_sleep_start()` 之前）插入 `power_down_peripherals_for_sleep()`。
- **B3.3**：在 `application_enter_manual_sleep`（`:508-513`，`esp_deep_sleep_start()` 之前）插入 `power_down_peripherals_for_sleep()`。
- **唤醒后恢复**：`app_main`/`board_init` 已在启动路径 `board_power_epd_on`/`board_power_audio_on`（`board.c` init）；深度睡眠唤醒即重启，电源轨自动恢复，无需额外 on 代码。

**边界**：`gpio_hold_en` 确保睡眠期间引脚保持；唤醒即复位，init 路径重新拉高。

#### B4 — 低电量关机警示屏（G4）

文件：`main/application.c`

- **B4.1**：新增静态函数 `static void render_low_battery_warning(void)`：取 `get_framebuffer()`（`custom_lcd_display.h:92`）共享帧缓冲，用 `rawdraw_clear(fb, 400, 300, RAWDRAW_COLOR_WHITE)` 清屏；用 `rawdraw_draw_text(fb,400,300, 居中x, 居中y, "电量耗尽\n请充电", &SourceHanSansSC_Medium_slim, RAWDRAW_COLOR_BLACK)` 全屏渲染（字体符号 `SourceHanSansSC_Medium_slim`/`SourceHanSansSC_Regular_slim` 来自 `78__xiaozhi-fonts` 组件，全项目统一用此对——见各页 `kXxxTitleFont`/`kXxxFont` 模式如 `car_move_page.c:38-39`；`fb` 的宽高用 `STYLE_SCREEN_WIDTH/HEIGHT`）。触发全刷新：`request_urgent_full_refresh()`（`custom_lcd_display.h:87`）；阻塞等待刷新完成——`while (is_refresh_pending()) vTaskDelay(pdMS_TO_TICKS(100));` 确保画面在断电前已物理刷新完毕。
- **B4.2 充电状态访问修正**：`s_charge_status` 实例是 `main.c:32` 的 file-static，`application.c` 无法访问；`charge_status_get_battery_percent()` 是已有 static 全局访问器（`charge_status.c:220`），但读取 `.charging` 字段需实例，无 static 访问器。**先新增静态访问器**：在 `charge_status.c` 加 `bool charge_status_is_charging(void)`——访问 `g_board.charge_status`（`board.c:10` 的全局 `board_context_t g_board`，其 `charge_status` 指针由 `board_init` 设置 `board.c:85`），调 `charge_status_get(g_board.charge_status).charging`。在 `charge_status.h` 声明。**然后在 `application_run`（`:572-577`）**：每次迭代开头 `int pct = charge_status_get_battery_percent();` 若 `pct > 0 && pct <= 3 && !charge_status_is_charging()` → 调 `render_low_battery_warning()` → `board_power_vbat_off()` → `esp_deep_sleep_start()`。阈值 3% 与 plan 一致。放在 run 而非 init，避免阻塞启动。`charge_status_is_charging` 需 `#include "board.h"` 取 `g_board`（extern 于 board.h）。
- **B4.1 include 需求**：`application.c` 当前不 include rawdraw/fonts（已确认无 `#include "rawdraw"`）。加 `#include "rawdraw_ext.h"`、`#include "style.h"`、`#include <lv_font.h>`（或 `78__xiaozhi-fonts` 暴露的字体声明头）。`SourceHanSansSC_*_slim` 符号由 `78__xiaozhi-fonts` 组件提供——`main` 组件 `CMakeLists.txt` 的 `REQUIRES` 需加 `78__xiaozhi-fonts`（当前 main REQUIRES 无此组件，确认 `main/CMakeLists.txt:3`）。

#### B5 — RTC 定时唤醒看板模式（G5，可选）

**用户决定延后音频**：RTC 看板模式依赖天气/日历同步，且为省电增强非功能正确性，**列为可选**。若做：

- 在 `enter_scheduled_sleep` 用 `pcf8563_start_countdown_timer(interval_minutes*60)` + `esp_sleep_enable_ext0_wakeup(NFC_FIELD_DETECT_GPIO? 或 RTC_INT_GPIO, level)` 替代/补充 BOOT ext0。驱动已具备（`rtc_pcf8563.c:127-162`）。
- 默认在本计划中**不实现**（Assumptions 记录），除非用户明确要求。

### Phase C — Phase 4 骨架（G6，仅文本流）

实现 WebSocket 协议层 + StreamPipeline + TextChunker，将 WSS 文本流接入已就绪的 `chat_page`（`begin_stream`/`append_text`/`end_stream` 已在 `ui_manager.c:948-981` 接线）。**音频/Opus/唤醒词延后**。

#### C1 — 新建 `components/audio/` 组件骨架

```
components/audio/
├── CMakeLists.txt   (SRCS: protocol.c stream_pipeline.c text_chunker.c)
├── include/
│   ├── protocol.h
│   ├── stream_pipeline.h
│   └── text_chunker.h
├── protocol.c
├── stream_pipeline.c
└── text_chunker.c
```

- **C1.1 `protocol.h`**：移植 C++ `protocols/protocol.h` 的 C 接口：
  ```c
  typedef struct {
      int sample_rate; int frame_duration; uint32_t timestamp;
      const uint8_t* payload; size_t payload_len;
  } audio_stream_packet_t;  /* payload 为外部拥有，不拷贝 */

  typedef enum { PROTO_LISTENING_AUTO, PROTO_LISTENING_MANUAL, PROTO_LISTENING_REALTIME } listening_mode_t;
  typedef enum { PROTO_ABORT_NONE, PROTO_ABORT_WAKE_WORD } abort_reason_t;

  typedef struct protocol protocol_t;
  /* 回调集（对应 C++ std::function 成员）*/
  typedef void (*proto_incoming_json_cb)(const cJSON* root, void* ctx);
  typedef void (*proto_incoming_audio_cb)(const audio_stream_packet_t* pkt, void* ctx);  /* 骨架阶段不实现音频，仅声明 */
  typedef void (*proto_text_cb)(const char* text, void* ctx);
  typedef void (*proto_state_cb)(void* ctx);  /* connected/disconnected/channel_opened/closed/idle_timeout */
  typedef void (*proto_error_cb)(const char* msg, void* ctx);

  struct protocol {
      int server_sample_rate; int server_frame_duration;
      char session_id[40];
      /* 回调表 */
      proto_incoming_json_cb on_incoming_json; void* json_ctx;
      proto_text_cb on_incoming_text; void* text_ctx;
      proto_state_cb on_connected; void* connected_ctx;
      proto_state_cb on_disconnected; void* disconnected_ctx;
      proto_state_cb on_idle_timeout; void* idle_ctx;
      proto_error_cb on_network_error; void* err_ctx;
  };
  void protocol_init(protocol_t* p);
  /* 公开操作（对应 C++ Protocol 虚函数）*/
  bool protocol_start(protocol_t* p);
  bool protocol_open_audio_channel(protocol_t* p);  /* 骨架：开 WSS 文本通道即可 */
  void protocol_close_audio_channel(protocol_t* p);
  bool protocol_send_text(protocol_t* p, const char* text);
  void protocol_send_start_listening(protocol_t* p, listening_mode_t m);
  void protocol_send_stop_listening(protocol_t* p);
  void protocol_send_abort_speaking(protocol_t* p, abort_reason_t r);
  void protocol_refresh_idle_timer(protocol_t* p);
  bool protocol_is_audio_channel_opened(const protocol_t* p);
  ```
  **二进制头**（C++ `BinaryProtocol2/3`）用 `#pragma pack(1)` 或 `__attribute__((packed))` 结构体照搬；`BuildMessageEnvelope`/`GenerateMsgId`/`GetTimestampMs` 移植为内部静态函数。

- **C1.2 `protocol.c`**：实现 WebSocket 客户端（用 ESP-IDF `esp_websocket_client`）+ 文本/JSON 消息分发。JSON 消息经 `on_incoming_json` 回调（传 cJSON），TTS 文本经 `on_incoming_text`。**音频收发留 `// TODO Phase4-audio` 桩**（`SendAudio`/`on_incoming_audio` 空实现返回 false）。心跳/空闲定时器（C++ `kIdleTimeoutMs=15000`）用 `esp_timer_t`。

- **C1.3 `text_chunker.h/.c`**：移植 C++ `streaming/text_chunker.cc/.h`（5.5KB）。接口：`text_chunker_init`/`text_chunker_feed(chunk)` → 按标点/长度分块回调。逻辑较独立，纯字符串处理。

- **C1.4 `stream_pipeline.h/.c`**：移植 C++ `streaming/stream_pipeline.cc/.h`（4.7KB）。结构体持有 `protocol_t* proto` + `text_chunker_t chunker` + `ui_manager_t* ui`（弱引用，由 C3.1 传入）。签名 `void stream_pipeline_init(stream_pipeline_t* sp, protocol_t* proto, ui_manager_t* ui)`；注册 `proto->on_incoming_text = stream_pipeline_on_text`（内部 ctx=`sp`）。`on_text` 把文本经 chunker 分块，逐块调 `ui_manager_chat_append_text(sp->ui, chunk)`（已存在 `ui_manager.c:948-981`）。**引入 Linear Arena 字符串池**（plan Task 4.5 要求）：`sp` 内嵌 `uint8_t arena_buf[32768]; int arena_off;`，O(1) 顺序拼接（`memcpy` 到 `arena_buf+arena_off`），流结束（`end_stream`）`arena_off=0` 归零（替代 C++ `std::deque<std::unique_ptr<...>>`，避免堆碎片）。刷新抑制：仅整句（chunker 回调标点边界）或 `end_stream` 时调 `ui_manager_request_active_page_refresh(sp->ui)`（防逐字闪屏）。

- **C1.5 `CMakeLists.txt`**：`REQUIRES esp_websocket_client cjson ui rawdraw bsp`（`protocol.c` 调 `settings_open` 读 WSS URL——`settings` 在 bsp 组件；`stream_pipeline.c` 调 `ui_manager_chat_append_text`——需 ui；`cjson` 用于 JSON 解析）。`network` 不需要（audio 不直接用 photo/weather）。**不依赖 esp_codec_dev/esp-sr/esp-opus-encoder**（音频延后）。

#### C2 — 依赖声明

- **C2.1**：`main/idf_component.yml` 新增 `espressif/esp_websocket_client: '*'`（WebSocket）。**不加** `espressif/esp-sr`/`78/esp-opus-encoder`（音频延后）。
- **C2.2**：`main/CMakeLists.txt` 的 `REQUIRES` 加 `audio`。

#### C3 — Application 接入

- **C3.1**：`application.c`/`application.h` 增加 `static protocol_t s_protocol; static stream_pipeline_t s_pipeline;`。`application_init` 调 `stream_pipeline_init(&s_pipeline, &s_protocol, application_get_ui_manager())`；注册 `protocol` 回调（`on_incoming_text` → pipeline）。
- **C3.2**：Wi-Fi 连接后（`application_notify_wifi_if_connected`）调 `protocol_start` / `protocol_open_audio_channel`。
- **C3.3**：更新 `application.h` 顶部注释，删除 "Phase 4 audio pipeline is parked"，改为说明仅文本流已接入、音频延后。
- **配置（WSS URL / 设备 token）**：当前 `Kconfig.projbuild` **无** WebSocket 相关符号（已确认 `:78-102` 仅有 `SERVER_ADDRESS`/`WEATHER_API_KEY`）。C++ 原 protocol 的 WSS URL 从 NVS namespace `"websocket"` key `"url"` 读取（`wifi_station.cc:48-49`）。采用相同策略：`protocol_init` 调 `settings_open("websocket", true)`（已有 `settings.c` 封装）读 `"url"`；若 NVS 无值则回退到新增 Kconfig 默认。**在 `Kconfig.projbuild` 的 "Server & API" menu 新增**：`config XIAOZHI_WSS_URL`（string，default `"wss://api.example.com/v1/chat"`，help 说明对话服务 WebSocket 地址）。设备 ID 由 `system_info_get_device_id`（F4.3）提供，不需配置。无独立 token 字段（若服务需要，后续随鉴权方案再加）。

### Phase D — 可选增强（非阻塞，按需）

- **D1 wifi 快速重连（Review #1）**：移植 C++ `WifiStation` FAST_RC 缓存（BSSID/channel/IP），优化深睡唤醒后重连速度。对频繁睡眠设备有显著省电/体验收益。**默认不做**，列为后续。
- **D2 system_info 补全（Review #2）**：补 `GetDeviceId`/`GetUserAgent`/`GetFlashSize`/`PrintTaskCpuUsage`/`PrintTaskList`/`PrintHeapStats`。当前无调用方，低优先。

### Phase E — 验证与清理

- **E1**：代码自检——逐文件对照 C++ 源确认移植忠实度；所有 `photo_gallery_*`、`custom_lcd_display_*`、`application_run` 签名已在本计划中确认（见 B1.3/B2/B4）。
- **E2**：`idf.py build` 编译确认无错误；`idf.py size` 确认 bin 在 4MB app 分区内。
- **E3**：主机端测试 `tests/`：扩展现有 host 测试覆盖新增 slideshow/arm_timer、text_chunker、stream_pipeline（纯逻辑可 gcc 编译）。
- **E4**：文档——更新 `application.h` 注释；`docs/c-porting-plan.md` 标注 Phase 4 骨架状态与本计划。
- **执行顺序**：Phase A（迁移合并到 bsp）先于 Phase B/C（B/C 引用 bsp 头，需 bsp 组件先扩充就位）。A 完成后，所有 `main/` 下板级文件迁入 `components/bsp/`（A1 列表），`main/CMakeLists.txt` 的 `SRCS` 缩减为 `main.c application.c`、`REQUIRES` 含 `bsp`。B4 的 `#include "custom_lcd_display.h"` 等**无需改前缀**（见 A4：bsp 的 INCLUDE_DIRS 使无前缀解析成立）。Phase F 与 A–E 独立，可并行。


### Phase F — 已迁移代码优化

以下四节（F1–F4）彼此**完全独立**，与 Phase A–E 也无耦合，可任意顺序、任意并行实施。每节内部自成闭环。同一文件若被多节触及，按文件分批即可，无逻辑依赖。**唯一例外**：F3.3 依赖 F1.1 先完成（F3.3 改 `photo_read_pixel` 签名，而该函数由 F1.1 创建）。

#### F1 — 重复代码提取 + 大缓冲 PSRAM 化

**F1.1 提取照片像素解码工具**：`photo_gallery_page.c:32-66` 与 `photo_detail_page.c:30-65` 有 6 个逐字重复函数（后者仅加 `pd_` 前缀）：`bytes_per_row_1bpp/2bpp`、`is_bwry_2bpp_image`、`is_mono_1bpp_image`、`read_photo_pixel_color`。新建 `components/ui/src/photo_blit.h/.c`，将这 6 个函数移入并改为无前缀公开 API：`photo_bytes_per_row_1bpp(int width)→int`、`photo_bytes_per_row_2bpp`、`photo_is_bwry_2bpp(int w,int h,uint32_t size)→bool`、`photo_is_mono_1bpp`、`photo_read_pixel(const uint8_t* data,uint32_t size,int photo_width,bool bwry2bpp,int x,int y)→rawdraw_color_t`。删除两个页面文件中的 static 副本，改为 include 调用。更新 `components/ui/CMakeLists.txt` 的 SRCS 加 `src/photo_blit.c`。

**F1.2 提取 cJSON 辅助工具**：`cj_copy_str`/`cj_get_int` 在 4 个文件逐字重复：`photo_storage.c:123,133`、`photo_downloader.c:72,82`、`holiday_fetcher.c:49,59`、`ble_gatt_service.c:289`。新建 `components/network/include/cjson_util.h` + `cjson_util.c`，暴露 `cjson_copy_str(cJSON*,const char* key,char* dst,size_t)`、`cjson_get_int(cJSON*,const char* key,int def)→int`。删除 4 处 static 副本，include 替换。更新 `components/network/CMakeLists.txt` SRCS 加 `cjson_util.c`。

**F1.3 提取文本换行工具**：`wrap_text` 有 4 个近乎相同的变体：`chat_page.c`、`news_page.c`、`photo_gallery_page.c`、`ebook_page.c:209-258`。在已有的 `components/ui/src/ui_text_util.c`（该文件已存在且有 `ui_text_fit_to_width`）新增 `ui_text_wrap_lines(const lv_font_t* font, const char* text, int max_width, char out[][line_cap], int max_lines, int* out_count)`——逐字测宽断行（用现有 `rawdraw_measure_text_width`），超长硬切。4 个页面改为调用它，删除各自的 `wrap_text`。

**F1.4 提取 rawdraw_rect_area 工具**：`rawdraw_rect_area`（计算 `r.w*r.h`）在 15 个文件中各有一个 static 副本。在 `rawdraw_ext.h/.c` 新增 `rawdraw_rect_area(rawdraw_rect_t r)→int`，删除 15 处 static 副本改为调用。

**F1.5 统一 MIN/MAX/CLAMP 宏**：有 4 种命名变体（`MIN/MAX`、`CARD_MIN/MAX`、`clamp_int`、`imax/imin/iclamp`）散在 `rawdraw.c`、`rawdraw_ext.c`、`bubble.c`、`calendar.c`、`modal.c`、`weather_card.c`、`card.c`、`progress_bar.c`、`scrollview.c`、`slider.c`。rawdraw 已有 `CLAMP/MIN/MAX`（rawdraw.c 内部）。新建 `components/rawdraw/include/rawdraw_util.h`，定义唯一 `RD_MIN/RD_MAX/RD_CLAMP/RD_MIN_MAX` 宏（加 `RD_` 前缀避免与 ESP-IDF 宏冲突），各文件 include 替换，删除局部宏。

**F1.6 大缓冲 PSRAM 化**（plan 明确要求 `MALLOC_CAP_SPIRAM`）：
- `photo_downloader.c:53` `static uint8_t s_photo_buf[15000]` → 改为 `init` 时 `heap_caps_malloc(PHOTO_DL_BUF_SIZE, MALLOC_CAP_SPIRAM)` + PSRAM 不可用回退 `malloc`（仿 `ble_image_receiver.c:48-53` 已有模式），`deinit` 时 `free`。
- `ebook_page.c:285` `malloc(portrait_bytes)`（~30KB）→ 改为 `static uint8_t* s_portrait_buf` 首次渲染时 `heap_caps_malloc` 一次分配、复用（UI 渲染单线程非重入）；或直接改 `heap_caps_malloc(...,MALLOC_CAP_SPIRAM)` + 回退。同理 `photo_gallery_page.c:160,477`、`photo_detail_page.c:80` 的照片 `malloc(info->file_size)` 改 `heap_caps_malloc(...,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)` + 回退。
- `coding_plan_page.c:90` `malloc(CODING_PLAN_CHART_2BPP_BYTES)` 同理。


#### F2 — 正确性修复

**F2.1 RTC I2C write 返回值忽略**：`main/rtc_pcf8563.c` 中 `set_time`(`:53-63`)、`set_alarm`(`:81-90`)、`disable_alarm`(`:92-99`)、`clear_alarm_flag`(`:101-107`)、`enable_interrupt`(`:109-119`)、`start/stop_countdown_timer`、`clear_timer_flag` 调用 `board_i2c_write_reg`（返回 `esp_err_t`）但忽略返回值，函数末尾恒 `return true`。改为累积各 `board_i2c_write_reg` 的返回值，任一 `!= ESP_OK` 则 `return false`。**注意**：`pcf8563_get_time`(`:65-79`) 已正确检查 `board_i2c_read_regs` 返回值（`:68` `!= ESP_OK`），不需改。**调用方安全**：唯一外部调用方 `main.c:171` 已 `if(pcf8563_get_time(...))`，且未调用 set/enable 系列（它们仅被 factory test 用，当前无调用方），故改返回值不会破坏现有调用——是内部正确性修复。`board_i2c_read_reg`(`board.c`) 失败静默返回 0，在 `clear_alarm_flag`/`enable_interrupt` 中读 CTRL2 后回写——失败时读到 0 会导致写回错误的寄存器值，一并在此校验。

**F2.2 BLE 图片接收跨任务竞态**：`ble_image_receiver.c` 的 `s_image_buffer`/`s_expected_size`/`s_received_size`/`s_status` 在 GATT BLE-host 任务中被 `receive_chunk`(`:118-120`，含非原子 `s_received_size += len`) 写入，而 app 任务读取/`reset`(`:134-137`)。无 mutex 保护。修复：加 `static SemaphoreHandle_t s_mutex`（`ble_image_receiver_init` 创建 binary mutex），`receive_chunk`/`get_status`/`get_data`/`reset`/`save_to_storage` 全部 `xSemaphoreTake`/`Give` 包裹。`save_to_storage`(`:177-206`) 读 `s_received_size` 时也需持锁，拷出后再释放锁做 I/O。

**F2.3 holiday_fetcher blob 大小不匹配**：`holiday_fetcher.c` `save_cache` 用 magic `4000`(`:143`)，`load_cache` 用 `blob[4096]`(`:168`)，且有两处不同大小的栈 blob 缓冲(`:133,168`)。统一为 `#define HOLIDAY_BLOB_MAX 4096`，save/load/buffer 三处一致。同时 `init` 中 `load_cache` 被调用两次(`:246-247`)，删一次。

**F2.4 ui_manager sscanf 未校验**：`ui_manager.c:~400` `sscanf(server_date,"%d-%d-%d",&y,&m,&d)` 返回值未检查，畸形日期渲染"0月0日"。改为检查 `==3`，否则回退到 iso 字符串或留空。


#### F3 — 渲染热路径性能

**F3.1 rawdraw_set_pixel 去逐像素重算**：`rawdraw.c` 中 `set_pixel` 每次重算 `bytes_per_row`（`y * bpr + (x*n)>>3`）并做 4 项边界检查。所有已知边界的填充循环（`draw_rect`/`draw_dither_rect`/`draw_round_rect`/`draw_circle` 在 `rawdraw.c`；`draw_round_rect_border`/`circle_border`/`styled_rect`/`styled_round_rect` 在 `rawdraw_ext.c`/`theme.c`）均重复调用它。新增 `rawdraw_set_pixel_unchecked(fb,width,height,x,y,color)`——跳过边界检查、预计算 bpr——内部用静态 inline。循环体改调它。每像素省 1 乘法 + 4 比较。

**F3.2 rawdraw_invert_region / copy_region 改行级**：`rawdraw_ext.c:421,438` 逐像素 `get_pixel`+`set_pixel`（每像素 2× bpr 重算 + 2× 边界检查）。`copy_region` 改为按行对齐 `memcpy`（源 dst 对齐时直接 `memcpy(bpr)`，否则逐字节）；`invert_region` 改为按行 `for` 循环对每字节 `^= 0xFF`（1bpp）/逐 2bit 取反（2bpp）。

**F3.3 照片解码传 stride 而非重算**（**依赖 F1.1 先完成**）：`photo_gallery_page.c:378-381,196` 与 `photo_detail_page.c:195` 的 `read_photo_pixel_color` 每像素重算 `bytes_per_row_*(photo_width)`（~100k 次/帧），而调用方已在 `:367-368` 算过 `photo_byte_width`。将 F1.1 新建的 `photo_read_pixel` 签名改为接收预计算 `int bpr` 参数：`photo_read_pixel(const uint8_t* data, uint32_t size, int bpr, bool bwry2bpp, int x, int y)`，调用方传 bpr 而非 photo_width。若 F1.1 未做则 F3.3 无法独立实施（需先合并工具文件）。

**F3.4 draw_ring_arc 去 atan2f-per-pixel**：`progress_bar.c:52-82` 在 `(2R+1)²` 框内每像素调 `atan2f`。改为：每 `dy` 行预算一次 `dx_max = sqrt(R² - dy²)`（整数半径用勾股数表或 `isqrt`），弧角判定改为查表或整数比较；或复用 `rawdraw_draw_circle_border` 已有的整数 Bresenham 模式，按环半径逐圈描边填充扇区。

**F3.5 ebook 旋转 O(n²) 加 rotate blit**：`ebook_page.c:303-307` 逐像素 `get_pixel`→`set_pixel` 旋转 300×400=120k 次。在 `rawdraw_ext.h/.c` 新增 `rawdraw_blit_rotated_90(const uint8_t* src, int sw, int sh, uint8_t* dst, int dw, int dh, int dst_x, int dst_y)`——按源行批量处理、内联 bit 操作。`ebook_render_reader_portrait` 改调它。注意源是 2bpp，blit 需正确处理 2bit 打包。

**F3.6 settings_themes 双重 theme_get**：`settings_themes.c:110,117` 每次循环迭代调 `rawdraw_theme_get(entry->id)` 两次返回同一指针。在循环体开头提一次 `const rawdraw_theme_t* th = rawdraw_theme_get(entry->id);`，后续索引 `th->tokens` 两次。


#### F4 — 低优先杂项

**F4.1 NVS handle 复用**：`nvs_state.c` 的 `kv_get_str/kv_set_str`(`:46-86`) 每次 open+close NVS handle；`cache_load`/`flush` 各做 ~14/15 次顺序 open。改为 `nvs_state_init` 时 open 一个 `NVS_READWRITE` handle 存为 `static nvs_handle_t s_handle`，各 kv 调用复用，`nvs_state_deinit` 关闭。减少每次写入的 round-trip。

**F4.2 HTTP boilerplate 抽取**：`photo_downloader.c`、`weather_api.c`、`holiday_fetcher.c` 各有一份近乎相同的 `esp_http_client` event-handler + GET/POST 封装。新建 `components/network/include/http_client_util.h/.c`，提供 `http_get_binary(url, uint8_t* buf, size_t max)` 与 `http_get_text(url, char* buf, size_t max)`（封装 client init→event handler→read→cleanup）。三处改调。SRCS 加 `http_client_util.c`。

**F4.3 system_info 补 device_id/user_agent**（Phase C protocol 依赖）：`components/bsp/system_info.c` 补 `system_info_get_device_id(char* out, size_t len)`（实现：`esp_read_mac` 后 `snprintf(out,len,"inkscreen_%02x%02x",mac[4],mac[5])`，移植自 C++ `system_info.cc:46-56`）与 `system_info_get_user_agent(char* out, size_t len)`（`snprintf(out,len,"%s/%s",BOARD_NAME,esp_app_get_description()->version)`，移植自 `:62-66`）。Phase C 的 `protocol_init` 用 `system_info_get_device_id` 填 `protocol_t.device_id`。`GetFlashSize`/诊断方法（`PrintTaskCpuUsage` 等）暂不补（无调用方）。

**F4.4 对话框尺寸常量化**：`dialog_w=292/dialog_h=166`（及 156/210/280/316 等变体）散在 `chat_page.c:253`、`photo_gallery_page.c:399`、`settings_dialogs.c:44,241,656`、`settings_themes.c:57`、`settings_about.c:38`。在 `style.h` 新增 `STYLE_DIALOG_W 292` + 常用高度 `STYLE_DIALOG_H_SM/MD/LG`（156/166/210），各处改用常量。仅统一相同值的，不同高度的保留各自字面量或命名。

**F4.5 widget custom_colors 显式标志**：`list_item.c:196`、`slider.c:254`、`progress_bar.c:309`、`card.c` 通过比较字段 == `WHITE/BLACK` 判断"是否用默认色"，caller 设 bg=WHITE+自定义 fg 会被误判。改为在 `set_colors` 函数中设 `bool custom_colors` 标志（`bubble.c:75/110` 已正确用此模式），render 据此判断。

**F4.6 硬编码屏幕尺寸改用常量**：`photo_gallery_page.c:539`、`photo_detail_page.c:144`、`ebook_page.c:282-283`（`kPortraitW=300/kPortraitH=400`，实为交换的屏幕尺寸）、`coding_plan_page.c:47`、`ble_image_receiver.c:192-193`、`ap_transfer_server.c:44-47` 中的 `400`/`300`/`30000`/`15000` 改用 `style.h` 已有的 `STYLE_SCREEN_WIDTH/HEIGHT` 及推导的 `STYLE_SCREEN_1BPP_BYTES`/`STYLE_SCREEN_2BPP_BYTES`（若 style.h 无则新增 `#define`）。


## Critical files & anchors

| 文件 | 区域 | 原因 |
|------|------|------|
| `components/ui/ui_manager.c` | `:818-868`（handle_input）、`:861`（input_refresh_locked）、`:663-709`（init）、`:702-705`（AP 回调）、`:841`（quick_switch）、`:1085-1117`（slideshow+pump） | G0/G1/G2 核心修改点 |
| `main/main.c` | `:228-260`（按钮创建+事件注册）、`:267-271`（refresh callback） | G0b 按钮事件注册 |
| `main/application.c` | `:431-449`（scheduled sleep）、`:499-514`（manual sleep）、`:520-548`（init）、`:572-577`（run） | G3/G4/C3 修改点 |
| `main/CMakeLists.txt` `:1-4` | 组件注册 | A3 迁移后重写 |
| `components/audio/`（新建） | 全部 | C1 Phase 4 骨架 |
| `main/rtc_pcf8563.c:81-162` | 闹钟/倒计时 API | G5（可选）驱动已就绪 |
| `components/ui/pages/ap_transfer_server.h:46-96` | 回调 typedef | B2 签名依据 |
| `components/ui/src/photo_blit.h/.c`（新建）| F1.1 照片解码 6 函数 | F1/F3.3 去重与性能 |
| `components/network/include/cjson_util.h`（新建）| F1.2 cJSON 辅助 | F1 去重 |
| `components/rawdraw/rawdraw_ext.c` | `set_pixel`/`invert_region`/`copy_region` | F3.1/F3.2/F3.5 热路径 |
| `main/rtc_pcf8563.c:53-162` | 全部 set/enable/timer 函数 | F2.1 返回值修复 |
| `components/network/ble_image_receiver.c:118-206` | 共享状态写入 | F2.2 竞态修复 |
| `components/network/weather_api.c:109-360` | `parse_now_json`(v7)、`http_get`(无 header)、`do_fetch`(v7 URL) | G-W1 v1 迁移核心 |
| `components/network/coding_plan_api.h/.c`（新建）| 全部 | G-C1 用量 API 客户端 |
| `components/ui/pages/coding_plan_page.c:89-167` | `load_chart_strip`/`render_chart` | G-C2 折线图改本地绘制 |

## Verification

- **G0 按钮失效修复**：烧录后连续按 UP/DOWN 单击 5+ 次，每次都应触发页面内交互（照片切换/列表滚动），不再"第一次有效后全部静默"。串口日志应周期性出现 `input unlocked`（B0.1 回调中的日志）。
- **G0b 页面切换**：按 UP 双击 → quick_switch 覆盖层打开（之前无反应）；在覆盖层中用 UP/DOWN 选中目标页、BOOT 确认 → 切换成功。长按 UP 后松开 → 不触发额外单击导航。
- **G2 AP 回调**：WiFi 传图后确认相册自动刷新并选中新照片；调 `GET /photo/show?id=X` 返回该照片而非 not_found；网页改幻灯片间隔后 `ui_manager_get_gallery_slideshow_interval_minutes` 返回新值。
- **F1 去重**：`grep -rn "static int bytes_per_row_1bpp\|static void cj_copy_str" components/` 返回 0（旧副本全删）；`idf.py build` 通过。
- **F1.6 PSRAM**：照片传输后 `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)` 不因图片缓冲而下降（vs 旧版下降 ~30KB）。
- **F2.1 RTC**：断开 RTC I2C 或模拟 `board_i2c_write_reg` 失败，确认 `pcf8563_set_time` 返回 `false`（旧行为恒 `true`）。
- **F2.2 BLE 竞态**：压测并发 BLE 传图 + UI 读照片，无 corrupt/重复（旧行为可能 `s_received_size` 竞态）。
- **F3.1/F3.3 set_pixel**：全屏填充 + 照片渲染的帧时间用 `esp_timer_get_time()` 计量，应显著下降（set_pixel 去逐像素重算 + 照片解码传 bpr）。
- **F3.5 ebook rotate**：进入电子书竖屏阅读模式翻页，旋转帧时间下降（旧版 120k 逐像素调用）。
- **G3 电源轨**：万用表测 GPIO6(EPD_PWR)/GPIO42(Audio_PWR) 在 `esp_deep_sleep_start` 后为 0V；唤醒后恢复高。
- **G4 低电量**：模拟 `charge_status_get_battery_percent` 返回 2 且非充电，确认渲染警示屏后进深睡。
- **Phase C 文本流**：WSS 连上后服务器推 TTS 文本，确认 `chat_page` 逐句追加（非逐字刷新）、整句触发一次 EPD 局部刷新。
- **编译**：`idf.py build` 通过；`idf.py size` app 分区未超。
- **主机测试**：`gcc -o test_chunker tests/test_*.c components/audio/text_chunker.c && ./test_chunker`。
- **G-W0 IP 定位**：首次启动（清空 NVS）WiFi 连接后，串口日志出现 `Detecting location via IP` + `Location: 34.2649,108.954 (新城)`；天气页顶部显示城市名。第二次启动直接从 NVS 读缓存（无 IP 请求日志）。
- **G-Weather v1**：烧录后进入天气页，确认温度/体感/天气描述/湿度（百分比）/风向/风力显示真实数据（不再 v7 的 `code:200` 包装）。串口日志 `Fetching current weather` + HTTP 200。若 v1 forecast/air 端点暂未实现，forecast 区为空或隐藏（不崩溃）。
- **G-Coding 用量**：WiFi 连接后进入用量页，确认 5h/weekly 进度条显示真实百分比、近 7 天 token 总量显示真实数字、per-model 明细列出 GLM-5.2/GLM-4.7。折线图为本地绘制的柱状图（非缓存位图）。BOOT 按钮触发刷新。

### Phase G — 天气 v1 API 迁移 + Coding Plan 用量 API 集成

两节独立：G-Weather 改 `weather_api.c`（v7→v1）+ 页面适配；G-Coding 新建 `coding_plan_api.c` + 页面改本地绘制折线图 + 应用层接线。二者无依赖，可并行。

#### G-W0 — IP 地理定位自动获取经纬度（G-Weather 前置）

当设备未配置固定经纬度（Kconfig/NVS 均无值）时，通过 IP 地理定位 API 自动获取当前位置的经纬度。

**API**：`GET http://ip-api.com/json/?lang=zh-CN&fields=61439`

响应示例（用户已验证真实响应）：
```json
{"status":"success","country":"中国","countryCode":"CN","region":"SN","regionName":"陕西","city":"新城","lat":34.2649,"lon":108.954,"timezone":"Asia/Shanghai","isp":"Chinanet","query":"113.140.11.140"}
```

- **G-W0.1 新增 `weather_api_detect_location(void) → bool` 函数**（在 `weather_api.c`）：
  - HTTP GET `http://ip-api.com/json/?lang=zh-CN&fields=61439`（用 `http_get`，无需自定义 header——此 API 不需要 key）。
  - 解析响应 JSON：检查 `status == "success"`，取 `lat`（浮点）和 `lon`（浮点）。
  - 成功时格式化为 `s_location`：`snprintf(s_location, sizeof(s_location), "%.4f,%.4f", lat, lon)`（4 位小数 ≈ 11 米精度，足以满足天气查询）。
  - 同时缓存 `city` 到 `s_city_name[32]`（如 "新城"），供 weather_page 显示当前城市名。
  - 写入 NVS namespace `"weather"` key `"location"` 和 `"city"`，后续启动直接读缓存避免重复请求。

- **G-W0.2 在 `weather_api_init` 中集成定位优先级链**：
  - 读取顺序：① NVS `"location"` → ② Kconfig `CONFIG_WEATHER_DEFAULT_LOCATION` → ③ 若均为空，标记 `s_need_auto_detect = true`，延迟到 WiFi 连接后自动获取。
  - **延迟获取**：`do_fetch` 开头检查 `!s_location[0] && s_need_auto_detect` → 调 `weather_api_detect_location()`；成功则置 `s_need_auto_detect = false` 并继续正常 fetch；失败则 log warning 并 return（下次定时器触发时重试）。
  - **手动刷新**：用户可通过 settings 菜单（或 weather_page BOOT 长按）触发 `weather_api_redetect_location()` 清空 NVS 缓存重新定位。

- **G-W0.3 数据模型新增**（`weather_api.h`）：
  - `static char s_city_name[32]` + 公开 `const char* weather_api_get_city_name(void)`。
  - `weather_data_t` 加 `char city_name[32]` 字段（从 IP 响应或 Kconfig/NVS 填充）。

- **G-W0.4 weather_page 显示城市名**：
  - `weather_page.c` 顶部当前显示日期/更新时间区域，新增城市名显示：`weather_api_get_city_name()`。
  - 若 `s_city_name[0] == '\0'`（定位未完成），显示空或 "定位中…"。

- **边界**：
  - ip-api.com 免费版：**仅 HTTP**（非 HTTPS），限 45 req/min。设备仅在首次配置或手动刷新时调用，后续从 NVS 缓存读取，频率极低。
  - `fields=61439` 是 bitmask，返回含 status/country/region/city/lat/lon/timezone/isp/query 等全部字段；仅需 `lat`/`lon`/`city`。
  - 若 ip-api.com 不可达（DNS 解析失败/超时），`detect_location` 返回 false，`do_fetch` 跳过本轮 fetch，下次 timer 重试。不影响其他功能。
  - 设备使用 NAT 出口 IP，定位到的是 ISP 出口位置（如示例中 Chinanet 陕西节点），精度通常在城市级别，对天气查询完全够用。
  - 若需 HTTPS/更高精度，可后续替换为付费方案（ip-api.com Pro 版 `https://pro.ip-api.com`）。

#### G-Weather — 和风天气 v7→v1 迁移

用户提供了真实 v1 API（host `mg3aarxm84.re.qweatherapi.com`，经纬度路径，`X-QW-Api-Key` header）。v1 的 JSON 结构与 v7 完全不同。weather_page 渲染依赖的字段：temp、feels_like、weather_text、weather_icon、wind_dir、wind_scale、humidity、air_aqi、air_quality、forecast（3天）。

- **G-W1 重写 `weather_api.c` URL + header + JSON 解析**：
  - **URL 构造**（`do_fetch :313-360`）：改 `s_city_code`（Location ID）为 `s_location`（经纬度字符串如 `"34.16,108.95"`）。URL 模板从 `https://devapi.qweather.com/v7/weather/now?key=%s&location=%s` 改为 `https://%s/weather/v1/current/%s?localTime=false&lang=zh`（host 从配置读，经纬度从配置读，key 不在 URL）。
  - **`http_get` 加 header 支持**（`:277-311`）：当前 `http_get(const char* url)` 不设 header。改为 `http_get_with_headers(const char* url, const char** headers, const char** values, int header_count)`：在 `esp_http_client_init` 后、`perform` 前，对每个 header 调 `esp_http_client_set_header(client, headers[i], values[i])`。和风 v1 需 `X-QW-Api-Key: <key>` + `accept: application/json`。
  - **`parse_now_json`（`:109-150`）适配 v1**：v1 current 响应无 `code`/`now` 包装，直接顶层对象。字段映射：
    - `condition.text` → `weather_text`；`condition.code` → `weather_icon`（v1 的 code 如 "305"）
    - `temperature.value`（浮点）→ `temp`（`snprintf("%.0f")` 取整）+ `temp_int`
    - `feelsLike.value`（浮点）→ `feels_like`
    - `humidity`（0-1 浮点）→ `humidity`（`snprintf("%d", (int)(h*100))` 百分比）
    - `wind.direction.compass`（如 "sw"）→ `wind_dir`；`wind.scale`（整数）→ `wind_scale`
    - 新增 `uvIndex`→ `weather_data_t` 需加字段 `int32_t uv_index`（`weather_page.c:204` 当前硬编码 `"弱"`，改用真实值映射）
    - v1 无 `obsTime`：`update_time` 用 fetch 时的本地时间或留空
  - **`parse_forecast_json`（`:152-203`）适配 v1**：v1 daily 端点 `https://{host}/weather/v1/daily/{lat}/{lon}?days=7&localTime=false&lang=zh`（经用户提供的真实响应确认）。响应顶层 `days[]` 数组（非 v7 的 `daily[]`）。每天字段映射：
    - `temperatureMax.value`（浮点）→ `temp_max`（`int` 取整）
    - `temperatureMin.value`（浮点）→ `temp_min`
    - `daytime.condition.text` → `weather_text`（用白天天气描述）
    - `daytime.condition.code` → `icon_code`（v1 code 如 "103"）
    - label 仍按 i<3 用 `今天`/`明天`/`后天`（`labels[]` 数组已有 `:157-161`）
    - **URL 改 `days=7`**（用户给的参数），但 weather_page forecast 区只显示前 4 个槽位（`WEATHER_MAX_FORECAST=7`、weather_page 渲染 `forecast_items[4]`）；`forecast_count` 记实际天数（≤7）。
    - **成功判定**：无 `"code"` 字段——JSON 可解析 + `days` 数组非空即成功。
  - **`parse_air_json`（`:205-229`）适配 v1**：v1 空气质量端点 `https://{host}/airquality/v1/current/{lat}/{lon}?lang=zh`（经用户提供的真实响应确认）。响应无 `code`/`now` 包装，顶层 `indexes[]` 数组。字段映射：遍历 `indexes[]` 找 `code=="cn-mee"` 条目（中国国标 AQI），取其 `aqi`（整数，如 34）→ `out->air_aqi`；`category`（如 "优"）→ `out->air_quality`。**注意**：v1 用 `airquality/v1/current` 路径（非 `air/v1/now`）。成功判定：JSON 可解析 + `indexes` 数组含 cn-mee 条目即成功。
  - **成功判定**：v1 无 `"code":"200"` 包装——改用 HTTP 200 + JSON 可解析即成功（移除 `:121-127` 的 code 检查）。

- **G-W2 `weather_api.h` 数据模型微调**：
  - `s_city_code[16]` → `s_location[32]`（装经纬度）；`weather_api_init(api_key, location, ...)` 第二参语义从 Location ID 改为经纬度字符串。
  - `weather_data_t` 加 `int32_t uv_index`（默认 -1=未知）；`weather_api_parse_now_json` 填充。

- **G-W3 `weather_page.c` 适配**：
  - `:204` `strcpy(values[3], "弱")` 改为：`uv_index` 映射（`>=8` "强"、`>=3` "中"、`>0` "弱"、`<=0` "--"）。
  - AQI 区（`:159-173`）：`air_aqi` 来自 v1 `indexes[].aqi`（cn-mee）、`air_quality` 来自 `category`（如 "优"），正常显示真实数据。若 fetch 失败（air_aqi 仍为 -1），显示 "--"。
  - forecast 区（`:215-247`）：若 `forecast_count == 0` 缩减或隐藏。
- **边界**：v1 返回 `humidity` 是 0-1 浮点（如 0.95），必须 ×100 转百分比；`temperature.value`/`temperatureMax.value`/`temperatureMin.value` 是浮点，`snprintf("%.0f")` 取整。v1 air 的 `indexes[].aqi` 是整数。三个 v1 端点（current `/weather/v1/current`、daily `/weather/v1/daily`、air `/airquality/v1/current`）均经用户真实响应确认，字段映射无不确定项。
- **G-W4 Kconfig + NVS 配置**：
  - `Kconfig.projbuild` 的 `WEATHER_API_KEY` 保留（default 改为 `[REDACTED]`）。
  - `WEATHER_DEFAULT_CITY` 改名 `WEATHER_DEFAULT_LOCATION`，default 改为 `"34.16,108.95"`。
  - 新增 `config QWEATHER_API_HOST`（string，default `"mg3aarxm84.re.qweatherapi.com"`）。
  - NVS：`settings_open("weather", true)` 读 `"key"`/`"location"`/`"host"` 覆盖 Kconfig 默认。

**边界**：v1 返回 `humidity` 是 0-1 浮点（如 0.95），必须 ×100 转百分比；`temperature.value` 是浮点（如 23.3），weather_page 的 `temp` 字段是字符串，`snprintf("%.0f")` 取整。v1 forecast/air 端点需查 swagger `https://dev.qweather.com/assets/openapi/qweather-apis-zh.yml` 确认——若不确定，先只做 current，forecast/air 留空。

#### G-Coding — Coding Plan 用量 API 集成（新建客户端 + 本地折线图）

用户提供了两个真实 API 端点（`quota/limit` + `model-usage`），含完整 JSON 响应。当前 `coding_plan_page_update()` 无调用者、页面永远无数据、折线图从 LittleFS 读预渲染位图。需新建 API 客户端 + 改本地绘制折线图 + 应用层接线。

- **G-C1 新建 `components/network/coding_plan_api.h/.c`**：
  - **数据结构**（对齐已有 `coding_plan_data_t`）：
    ```c
    typedef struct {
        char reset_time[32];          /* quota/limit → nextResetTime 格式化 */
        uint64_t five_hour_tokens;    /* quota/limit unit=3 number=5 → 已用百分比×额度 */
        uint64_t week_tokens;         /* quota/limit unit=6 → 已用百分比×额度 */
        coding_plan_model_usage_t per_model[CODING_PLAN_MAX_MODELS];
        int per_model_count;
        /* 折线图数据：model-usage → tokensUsage[168]（7天×24小时） */
        uint64_t hourly_tokens[168];
        int hourly_count;
    } coding_plan_api_data_t;
    ```
  - **`parse_quota_limit_json(const char* json, coding_plan_api_data_t* out)`**：解析 `data.limits[]` 数组。`type=="TOKENS_LIMIT" && unit==3` → 5h window（`number`=窗口数5、`percentage`=已用百分比）；`unit==6` → weekly。`nextResetTime`（Unix ms）→ `reset_time`（`localtime_r` 格式化为 "MM-DD HH:MM"）。额度基准值：5h=2M tokens、week=10M（与 `coding_plan_page.c:40-41` 的 `CODING_PLAN_5H_QUOTA_TOKENS`/`CODING_PLAN_WEEK_QUOTA_TOKENS` 一致）；已用 = 额度 × `percentage/100`。
  - **`parse_model_usage_json(const char* json, coding_plan_api_data_t* out)`**：解析 `data.totalUsage.modelSummaryList[]` → `per_model[]`（`modelName`/`totalTokens`）；`data.tokensUsage[]` → `hourly_tokens[]`（168 元素）；`data.totalUsage.totalTokensUsage` → `week_tokens`。
  - **HTTP 请求**：`coding_plan_api_fetch()`——两个 GET 请求（`quota/limit?type=1` + `model-usage?startTime=...&endTime=...`），header 三件套：`authorization: <token>`、`bigmodel-organization: <org>`、`bigmodel-project: <project>`。用 G-W1 的 `http_get_with_headers`（若 G-Weather 先做则复用；否则在 coding_plan_api.c 内自带）。

- **G-C2 改 `coding_plan_page.c` 折线图为本地绘制**：
  - **删除** `load_chart_strip`/`render_chart` 中的 LittleFS/photo_storage 缓存位图逻辑（`:89-167`）。
  - **新增 `render_chart_from_data`**：用 `r->data.hourly_tokens[168]`（需在 `coding_plan_data_t`/`coding_plan_page.h` 加 `uint64_t hourly_tokens[168]; int hourly_count;`）绘制简易柱状图：找 `max_tokens`，每个柱 `高度 = (tokens[i]/max) * CHART_H`，用 `rawdraw_fill_rect` 画柱（168 柱太密，按 6 小时聚合 → 28 柱或按天聚合 → 7 柱）。accent 色。加 Y 轴 max 标注。
  - **`coding_plan_page_update`** 填充 `hourly_tokens` + `hourly_count`。

- **G-C3 应用层接线**（`main/application.c` 或 `main/main.c`）：
  - WiFi 连接后调 `coding_plan_api_fetch()` → 解析 → `coding_plan_page_update((page_renderer_t*)&mgr->coding_plan, &data)` → `ui_manager_request_active_page_refresh`。
  - 定时刷新：复用 weather_api 的 hourly timer 模式，或手动在 `application_run` 循环中按间隔触发（如每 30 分钟）。

- **G-C4 Kconfig + NVS 配置**：
  - `Kconfig.projbuild` 新增 `config CODING_PLAN_API_TOKEN`（string，default 用户给的 token）、`config CODING_PLAN_API_ORG`（default `org-c0fb217715454D74b92930fE336e7BAd`）、`config CODING_PLAN_API_PROJECT`（default `proj_f0a4484835804c13Bca7473E0563C567`）。
  - NVS namespace `"coding_plan"` 读 `"token"`/`"org"`/`"project"` 覆盖。
  - API host 固定 `https://open.bigmodel.cn`（不改）。

**边界**：智谱 API 的 `model-usage` 的 `startTime`/`endTime` 需动态计算（当前时间往前推 7 天）。`percentage` 字段是 0-100 整数。`hourly_tokens` 数组可能不足 168（若 API 返回范围不是完整 7 天），`hourly_count` 记实际数。


## 执行编排：依赖关系与并发建议

经全任务文件冲突分析（34 个任务、59 对同文件冲突），按**文件归属**划分出 5 个必须串行的集群 + 8 个完全独立的单文件任务。同一集群内的任务触及同一文件，**必须由同一 agent 顺序执行**或分波串行；不同集群/独立任务文件不相交，**可并行**。

### 文件冲突集群（集群内必须串行）

| 集群 | 涉及文件 | 任务（按集群内执行顺序） | 冲突原因 |
|------|----------|--------------------------|----------|
| **ui_manager** | `components/ui/ui_manager.c` | B0 → B1 → B2 → F2.4 | 4 任务都改同一文件不同区域 |
| **application** | `main/application.c`、`application.h` | B0b → B3 → B4 → C3 → G-C | 5 任务都改 application.c |
| **rawdraw_ext** | `components/rawdraw/rawdraw_ext.c` | F1.4 → F3.1 → F3.2 → F3.5 | 4 任务改同一文件不同函数 |
| **photo_pages** | `photo_gallery_page.c`、`photo_detail_page.c` | F1.1 → F1.3 → F1.6 → F3.3 → F4.4 → F4.6 | 6 任务触及这两个文件 |
| **network** | `weather_api.c`、`photo_downloader.c`、`holiday_fetcher.c`、`CMakeLists.txt` | F1.2 → F2.3 → F4.2 → G-W | 共享文件 + CMakeLists |

### 完全独立任务（可与任何集群并行）

F2.1（`rtc_pcf8563.c`）、F2.2（`ble_image_receiver.c`）、F3.4（`progress_bar.c`）、F3.6（`settings_themes.c`）、F4.1（`nvs_state.c`）、F4.3（`system_info.c`）、F4.5（`list_item/slider/progress_bar/card.c`）——各触及唯一文件，无交叉。

### 跨集群冲突（需注意）

- **G-C** 同时触及 `coding_plan_page.c`（photo_pages 集群）、`application.c`（application 集群）、`network/CMakeLists.txt`（network 集群）——它是**跨集群枢纽**，必须在三个相关集群都完成后或由 application 集群的 agent 末尾承接。
- **F3.5** 同时触及 `rawdraw_ext.c`（rawdraw 集群）和 `ebook_page.c`（photo_pages 集群）——归入 rawdraw 集群执行，但需 photo_pages 集群的 F1.6（ebook PSRAM 化）先完成。
- **C3** 依赖 F4.3（`system_info_get_device_id`）—— F4.3 是独立任务，必须在 C3 之前完成。

### 推荐执行波次（5 波）

```
Wave 0（串行，阻塞后续全部）:
  A — BSP 迁移（移动 9 文件 + 改 2 个 CMakeLists）
  理由：A 改变所有后续任务的文件路径，必须先完成。

Wave 1（A 完成后，可并行 7 个 agent）:
  ┌─ agent-ui_mgr:   B0 → B1 → B2 → F2.4          （ui_manager.c 集群）
  ├─ agent-app:      B0b → B3 → B4                  （application.c 集群，前半）
  ├─ agent-rawdraw:  F1.5 → F1.4 → F3.1 → F3.2     （rawdraw 宏统一 + ext.c 集群，先做宏统一避免冲突）
  ├─ agent-network:  F1.2 → F2.3                    （network 集群，前半）
  ├─ agent-photo:    F1.1 → F1.3 → F1.6             （photo_pages 集群，前半）
  ├─ agent-indep-1:  F2.1 + F2.2 + F4.1             （3 个独立单文件任务合并）
  └─ agent-indep-2:  F4.3 + F3.6                    （2 个独立单文件任务合并）

Wave 2（Wave 1 完成后，可并行 4 个 agent）:
  ┌─ agent-app:      C3 → G-C                        （application.c 集群后半；C3 依赖 F4.3 已在 Wave 1 完成）
  ├─ agent-rawdraw:  F3.5 → F3.4                     （rawdraw_ext.c 收尾 + progress_bar 独立）
  ├─ agent-photo:    F3.3 → F4.4 → F4.6              （photo_pages 集群后半；F3.3 依赖 F1.1 已在 Wave 1 完成）
  └─ agent-network:  F4.2 → G-W                      （network 集群后半）

Wave 3（Wave 2 完成后，可并行 2 个 agent）:
  ┌─ agent-app:      G-C（若 Wave 2 的 C3 agent 未承接）
  └─ agent-misc:     F4.5                            （widget custom_colors，独立）

Wave 4（全部完成后）:
  E1-E4 — 验证、编译、测试、文档（单 agent 顺序）
```

### Subagent 并发规则总结

- **最大并行度 = 7**（Wave 1）。每个 agent 拥有一个文件集群或若干独立文件，互不干涉。
- **同一文件的多个任务必须归同一 agent**——不可拆分到不同 agent（会产生 merge 冲突）。
- **agent-app 是关键路径**（5 任务最长集群），决定整体完成时间。建议 Wave 1 和 Wave 2 的 application 集群由同一 agent 连续承接（B0b→B3→B4→C3→G-C），避免上下文切换。
- **G-C 放最后**——它跨三个集群（photo_pages 的 coding_plan_page.c、network 的 CMakeLists、application.c），必须在相关集群都进入稳定状态后执行。
- **C1/C2（audio 组件骨架）**可插入 Wave 1 或 Wave 2 任意空位（全新文件，无冲突），但 C3 接入 application.c 需在 application 集群中排队。

## Assumptions & contingencies

- **RTC 看板模式（G5）默认不做**：用户决定延后音频，而看板模式依赖网络同步且为省电增强非功能正确性。若需做，驱动已具备，按 B5 接入即可。
- **wifi 快速重连（D1）默认不做**：FAST_RC 缓存移植量大（C++ `wifi_station.cc` ~400 行），且当前 wifi_manager 非阻塞重连已可用，列为后续独立优化。`system_info` 的 `GetDeviceId`/`GetUserAgent` 在 F4.3 完成（Phase C protocol 依赖）；`GetFlashSize`/诊断方法暂不补。
- **Phase 4 仅文本骨架**：音频/Opus/唤醒词（`audio_codec.c`/`wake_word.c`/`audio_service.c`、esp-sr/esp-opus-encoder 依赖、I2S 双向管线）整体作为后续独立计划，本次不实现、不声明依赖。若后续启动，`components/audio/` 已预留目录结构，`protocol_t` 已预留 `on_incoming_audio` 回调位。
- **签名已确认**：B1/B2/B4 涉及的 `photo_gallery_*`（`photo_gallery_page.h:61-71`）、`custom_lcd_display` 全刷新（`request_urgent_full_refresh`/`get_framebuffer`/`is_refresh_pending`，`custom_lcd_display.h:87-92`）、`application_run` 结构（`:572-577` 1s pump 循环）均已在本轮验证中查实，接线无需再查。
- **`components/network/CMakeLists.txt` 需更新**：G-C1 新增 `coding_plan_api.c` 后，在 SRCS 行加 `"coding_plan_api.c"`（当前 `:1` 列了 weather_api/holiday_fetcher/photo_downloader/photo_storage/bluetooth_manager/ble_gatt_service/ble_image_receiver）。G-W1 若复用 F4.2 的 `http_client_util.c` 也一并加入。
