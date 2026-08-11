# 代码评审报告：`feature/c-porting` — C++ 到 C 全面移植

**分支：** `feature/c-porting` vs `main`（合并基点 `6343836`）
**范围：** 202 个文件，+40,719 / -2,392 行
**评审日期：** 2026-08-11
**评审方式：** 16 个并行 reviewer agent 按模块划分评审 + 主评审整合

---

## 总体结论：🔴 需要修改（REQUEST_CHANGES）

| 严重程度 | 数量 |
|---|---|
| 严重（Critical） | 4 |
| 重要（Major） | 21 |
| 一般（Minor） | 15 |
| 建议（Nit） | 9 |

C 移植的架构设计扎实——组件边界划分清晰、CMake 结构合理、C 语言 OOP 模拟到位、rawdraw 渲染核心正确。但存在 **4 个严重问题阻断合并**，以及围绕「缺少同步」「未检查内存分配失败」「移植不完整」三个主题的 21 个重要问题。

---

## 严重问题（Critical，共 4 个）

### C1. 19 个 UI 页面中有 9 个缺少 `.render` 虚函数表槽位——页面不渲染任何内容

**严重程度：** 严重
**影响范围：** 近半数设备页面显示空白

| 页面 | 文件 | 行号 |
|---|---|---|
| 日历 | `components/ui/pages/calendar_page.c` | 366–377 |
| 天气 | `components/ui/pages/weather_page.c` | 355–366 |
| 天气详情 | `components/ui/pages/weather_detail_page.c` | 367–378 |
| 人生进度 | `components/ui/pages/lifebar_page.c` | 362–373 |
| 年度进度 | `components/ui/pages/yearprogress_page.c` | 373–384 |
| 新闻 | `components/ui/pages/news_page.c` | 466–477 |
| 电子书 | `components/ui/pages/ebook_page.c` | 483–494 |
| 聊天 | `components/ui/pages/chat_page.c` | 634–646 |
| WiFi | `components/ui/pages/wifi_page.c` | 421–431 |

**问题：** 以上 9 个页面在 `page_renderer_ops_t` 虚函数表中注册了 `.init`、`.enter`、`.handle_input`，但**遗漏了 `.render` 槽位**。`page_renderer_render()` 分发函数以 `if (r && r->ops && r->ops->render)` 守卫，当槽位为 NULL 时静默跳过。`ui_manager.c:837` 是唯一的内容渲染入口。所有 9 个页面都已完整实现了 `*_page_render()` 函数（如 `calendar_page_render` 在 `:234`、`weather_page_render` 在 `:83`），函数存在、能编译，但**从未被调用**。

**影响：** 近半数设备页面渲染空白——仅显示状态栏边框。日历、天气、新闻、电子书阅读器、聊天、WiFi 配置页均不可用。

**证据：** 交叉检查了全部 19 个 `*_page_ops` 定义。10 个页面正确连接了 `.render`（almanac、ap_transfer、car_move、coding_plan、font_debug、font_metrics、log、photo_detail、photo_gallery、settings），9 个未连接。这是系统性的复制遗漏。

**修复：** 在 9 个 ops 结构体中分别添加 `.render = <page>_render,`。

---

### C2. 生产凭据硬编码并提交到 `sdkconfig`

**严重程度：** 严重（安全）
**文件：** `sdkconfig`

**问题：** 提交的 sdkconfig 包含真实密钥（以下值已脱敏）：

- WiFi：`CONFIG_DEFAULT_WIFI_SSID="[REDACTED]"` / `CONFIG_DEFAULT_WIFI_PASSWORD="[REDACTED]"`
- 和风天气 API Key：`CONFIG_WEATHER_API_KEY="[REDACTED]"`
- 智谱 API Token（完整 JWT）：`CONFIG_CODING_PLAN_API_TOKEN="[REDACTED]..."`
- 服务器地址：`CONFIG_SERVER_ADDRESS="[REDACTED]"`
- 手机号码：`CONFIG_CAR_MOVE_PHONE_NUMBER="[REDACTED]"`

**影响：** 任何拥有仓库访问权限的人都能获取 WiFi 密码、天气 API Key 和有效的智谱认证 JWT。

**修复：** 将所有密钥迁移到 Kconfig 默认值（引用编译时环境变量或 NVS 存储值）。将 `sdkconfig` 加入 `.gitignore`，改为提交 `sdkconfig.defaults`。**立即轮换已暴露的 API Key 和 JWT。**

---

### C3. 未认证的局域网 HTTP 服务器暴露远程控制接口

**严重程度：** 严重（安全）
**文件：** `components/ui/pages/ap_transfer_server.c:584-589`、`application.c`

**问题：** AP 传输服务器通过 `ap_transfer_server_start_lan()` 将相同的未认证 HTTP 处理器绑定到站点/局域网 IP。`application.c` 在每次 WiFi 连接时自动启动局域网服务器。所有端点均无认证。同一网络中的任何人都可以：

- `POST {"sleep":true}` → 强制设备进入深度睡眠
- `POST {"wifi_enabled":false}` → 使设备离线
- `POST {"service_enabled":false}` → 停止服务器
- `DELETE /photo?id=...` / `POST /photos/move` → 删除/重排相册

**修复：** 将破坏性控制动词（sleep/wifi-off/照片操作）限制在 AP 模式或共享令牌之后。至少将局域网服务器限制为只读照片上传。

---

### C4. NFC NDEF 写入缓冲区溢出

**严重程度：** 严重
**文件：** `components/bsp/zectrix_nfc.c` — `nfc_write_uri_ndef`

**问题：** `ndef_msg[512]` 在 URI 后缀超过约 504 字节时溢出。NTAG 标签容量为 888 字节，因此合法的长 URI 会触发栈缓冲区溢出。这是本次 diff 中最直接的内存破坏缺陷。

**修复：** 根据实际负载大小分配缓冲区，或在打包前校验 URI 长度。

---

## 重要问题（Major，共 21 个）

### M1. 音频：协议发送函数中 cJSON NULL 解引用

**文件：** `components/audio/protocol.c:184-196`（及同模式的发送函数）
`cJSON_PrintUnformatted()` 在内存分配失败时返回 NULL；结果未经检查直接传给 `strlen()` → NULL 解引用崩溃。需同时检查 `cJSON_CreateObject()` 和 `cJSON_PrintUnformatted()` 的返回值。

### M2. 音频：text_chunker 在 malloc 失败时静默丢弃缓冲文本

**文件：** `components/audio/text_chunker.c:108-127`
当 `try_emit_chunk` 中 `malloc(boundary + 1)` 失败时，memmove/buffer_len 递减/return true 仍然执行——静默丢弃已接收的 LLM 文本。将簿记逻辑移入 `if (chunk)` 守卫内；失败时返回 false 以便重试。

### M3. 音频：`stream_pipeline_t` 中 32KB 死代码 `arena_buf`

**文件：** `components/audio/stream_pipeline.c:10-26`
`arena_buf[32768]` 被写入但从未读取（死存储）。每个实例占用 ESP32-S3 全部 DRAM 的约 10%。若栈分配，仅此数组就超过典型任务栈大小（4-8KB）→ 栈溢出。删除或改为堆分配。

### M4. 音频：chunker/pipeline 状态在 WebSocket 任务与控制任务间无同步

**文件：** `components/audio/stream_pipeline.c:40-55`
incoming-text 回调在 WebSocket 任务执行；控制 API（`begin_stream`/`reset`）在 UI 任务执行。两者都修改同一个 `text_chunker_t.buffer`（含 `realloc`/`memmove`），无任何互斥锁 → use-after-free、撕裂读。添加互斥锁或强制单任务访问。

### M5. BSP 显示：物理 EPD 刷新完成前清除刷新忙标志

**文件：** `components/bsp/custom_lcd_display.c:556-567`
`refresh_task_loop` 在调用 `epd_complete_refresh()` 之前就清除 `refresh_in_progress` 并调用 `update_display_busy_locked()`（清除 `SLEEP_BUSY_SRC_DISPLAY`）。睡眠管理器可能在面板仍在物理刷新时使系统进入深度睡眠。应在清除标志之前先调用 `epd_complete_refresh()`。

### M6. BSP 存储：`storage_manager_get_info()` 按值返回约 21KB 结构体→栈溢出

**文件：** `components/bsp/storage_manager.c:81-92`
`storage_info_t` 包含 `storage_file_info_t files[256]`（每条 84 字节）= 21,528 字节。作为局部变量按值返回。ESP32-S3 FreeRTOS 任务通常只有 4-8KB 栈。任何在普通任务上的调用方都会栈溢出。主机测试通过仅因为 Linux 栈为 8MB。改用输出参数或堆分配。

### M7. BSP 连接性：WiFi 状态在事件循环任务与 UI 任务间无同步

**文件：** `components/bsp/wifi_manager.c`
`s_connected`/`s_ip_address`/`s_ssid`/`s_retry_count` 由事件循环任务写入，由 UI/main 任务读取，无锁 → SSID/IP 字符串和连接状态的数据竞争。

### M8. BSP 连接性：`nfc_read_ndef` 使用不可重入的静态读缓冲区

**文件：** `components/bsp/zectrix_nfc.c`
使用文件静态读缓冲区，按块 I2C 互斥锁保护，但多块读取在整序列上不是原子的。并发读取会损坏缓冲区。

### M9. BSP 连接性：`wifi_manager_connect`/`disconnect` 未取消重连定时器

**文件：** `components/bsp/wifi_manager.c`
重连 esp_timer 已创建但在显式连接/断开时从不停止/取消 → 定时器过期时与新的连接状态竞争。

### M10. 网络API：`holiday_fetcher_fetch` 将非 NUL 终止缓冲区传给 `cJSON_Parse`

**文件：** `components/network/holiday_fetcher.c:275-291`
`resp` 由 `http_get_with_headers_cert` 填充（文档明确「不添加 NUL 终止」），但在传给 `cJSON_Parse(resp)` 前未终止。每次获取都有堆越读，且在目标设备上几乎必然解析失败——日历永远获取不到假日数据。兄弟调用方（`coding_plan_api.c`、`weather_api.c`）正确终止了缓冲区，唯独此处遗漏。预留 1 字节用于终止符。

### M11. 网络照片：`photo_storage` 全局索引在多个 FreeRTOS 任务间无锁访问

**文件：** `components/network/photo_storage.c:76-78`
`s_photos[]`/`s_photo_count` 由 `photo_save`（BLE/httpd/网络任务）修改，由 `photo_list`/`photo_get_by_index`（UI 渲染任务）读取，无互斥锁。撕裂读导致显示错误/损坏的照片。在所有公共入口函数添加 FreeRTOS 互斥锁。

### M12. 网络照片：`photo_save` 忽略写入失败并报告成功

**文件：** `components/network/photo_storage.c:455-458`
`write_meta_file()` 和 `save_index()` 的返回值被丢弃；`photo_save` 无条件返回 0。LittleFS 空间满时，内存状态反映了新照片但未持久化 → 重启后照片消失，调用方被告知成功。检查返回值，失败时回滚。

### M13. BLE：`s_service_ready` 不论 `START_EVT` 状态均设为 true

**文件：** `components/network/ble_gatt_service.c:138-140`
`status` 被记录但未做门控；`s_service_ready=true` 无条件赋值。失败的 `esp_ble_gatts_start_service` 对调用方表现为「就绪」。仅在 `param->start.status == ESP_GATT_OK` 时设置。

### M14. BLE：图片控制读取状态丢弃 `expected_size` 低字节

**文件：** `components/network/ble_gatt_service.c:178-184`
4 字节状态块：`[status, recv_hi, recv_lo, exp_hi]`——`expected_size` 仅贡献高字节。15000 字节图片（0x3A98）客户端读到 14848。扩展为 5 字节。

### M15. BLE：`bluetooth_manager_init` 在部分失败时泄漏资源

**文件：** `components/network/bluetooth_manager.c:112-145`
四步启动在首个失败即返回 false，无回滚。控制器/bluedroid 处于中间状态 → init 不可重试，本次启动 BLE 永久禁用。在错误路径添加拆解逻辑。

### M16. UI 核心：AP/HTTP 回调在未序列化情况下修改共享 UI 状态

**文件：** `components/ui/ui_manager.c:617-631`
`image_received_cb`、`settings_changed_cb`、`photos_changed_cb`、`show_photo_cb` 在 httpd 任务执行，但修改了 UI 任务同时渲染的画廊/设置页面状态。渲染过程中页面结构的数据竞争。通过现有事件队列将回调转发到 UI 任务。

### M17. 控件：天气图标解析器将「晴间多云」误分类为「晴」

**文件：** `components/rawdraw/widgets/weather_card.c:15-22`
`strstr(text, "晴")` 最先检查；「晴间多云」包含「晴」，匹配到晴分支。多云分支成为死代码。应先检查更具体的子串。

### M18. 控件：日历在溢出单元格确认时返回错误月份的日期

**文件：** `components/rawdraw/widgets/calendar.c:530-545`
启用 `show_overflow` 时，确认暗淡的相邻月份单元格会将该单元格的日期设置到当前显示月份——例如在一月查看时确认「12月28日」得到的是一月28日。应拒绝溢出单元格或触发月份切换。

### M19. 设置/AP：`settings_handler` 在 JSON 错误 POST 路径泄漏 NVS 句柄

**文件：** `components/ui/pages/ap_transfer_server.c:527-532`
`read_json_body` 返回 NULL 时触发 `send_json(); return ESP_FAIL;`，跳过了第 562 行的 `nvs_close` 清理。每次畸形 POST 泄漏一个句柄 → 耗尽。

### M20. 日志页：环形缓冲区 head 指针不前进——仅最后一条记录存活

**文件：** `components/ui/pages/log_page.c:36-46`
`add_log_entry()` 仅在溢出分支前进 `head`。正常分支中，每次调用写入 `entries[0]`，覆盖前一条。页面显示一行而非 7-8 行。每次写入都应前进 `head`。

### M21. 电子书：竖排阅读器在 OOM 时解引用 NULL 帧缓冲区

**文件：** `components/ui/pages/ebook_page.c:234-238`
`heap_caps_malloc` 分配约 30KB 竖排缓冲区，回退到 `malloc`，然后 `memset(portrait, 0x55, ...)` 无 NULL 检查 → OOM 时崩溃。添加 `if (!portrait) return;`（兄弟代码 `photo_gallery_page.c` 已正确处理）。

---

## 一般问题（Minor，共 15 个）

| 编号 | 文件 | 问题 |
|---|---|---|
| m1 | `audio/protocol.c:144-163` | WebSocket continuation 帧（op_code 0）被静默丢弃——分片消息丢失 |
| m2 | `audio/protocol.c:55-86` | 无 `protocol_stop`/`protocol_destroy`；ws_client 和 idle_timer 从未释放 |
| m3 | `bsp/epd_refresh.c:76-79` | `epd_refresh_stop` 在任务自删除后对任务句柄 use-after-free |
| m4 | `bsp/charge_status.c:62-64` | ADC 单元句柄在 `adc_oneshot_config_channel` 失败时泄漏 |
| m5 | `bsp/nvs_state.c:486-494` | `nvs_state_save()` 不论 Flash 写入是否失败均报告成功 |
| m6 | `bsp/nvs_state.c:457-462` | `nvs_state_init()` 在重复进入时泄漏 NVS 句柄 |
| m7 | `bsp/board.c` | `charge_status_tick` 定时器创建/启动失败被静默吞掉 |
| m8 | `bsp/zectrix_nfc.c:301-309` | `map_set_string` 持久化完整值但缓存截断值→缓存/Flash 不一致 |
| m9 | `network/photo_downloader.c:286-301` | `photo_download_single` 获取服务器元数据后丢弃，改用硬编码值 |
| m10 | `ui/pages/news_page.c:286-289` | 新闻预览滚动：只能上滚不能下滚——溢出文本不可达 |
| m11 | `ui/pages/chat_page.c:296-369` | `is_listening`/`bottom_status_text` 已写入但从不渲染 |
| m12 | `ui/ui_manager.c:540-558` | `handle_quick_switch_input` 在 `quick_count==0` 时除以零 |
| m13 | `ap_transfer_server.c:406-411` | `upload_handler` 在成功的 `httpd_resp_send` 后返回 `ESP_FAIL`（三元运算符反转） |
| m14 | `ap_transfer_server.c:950-959` | `start_access_point` 在 `set_ip_info`/`dhcps_start` 失败时泄漏 `ap_netif` |
| m15 | `application.h:51-53` | `application_arm_sync_sleep_timer` 已声明但从未定义 |

---

## 建议（Nit，共 9 个）

| 编号 | 文件 | 问题 |
|---|---|---|
| n1 | `audio/text_chunker.c:137-142` | 热路径上冗余的 `strlen` 双重扫描 |
| n2 | `ble_gatt_service.c:356-358` | `device_info` 结构体跨任务读写无同步（外观撕裂读） |
| n3 | `bluetooth_manager.c:308-316` | `build_ndef_text_tlv` memmove 读取未初始化内存（死代码） |
| n4 | `bsp/nvs_state.c` | 缓存将 `weather_city` 截断为 63 字符但持久化完整值 |
| n5 | `network/photo_storage.c:434-435` | 文件路径从未清洗的 photo id 构建（纵深防御缺口） |
| n6 | `bsp/CMakeLists.txt` | `nvs_flash` 放在 `PRIV_REQUIRES` 但 `settings.h` 公开暴露了 `nvs_flash.h` 类型 |
| n7 | `ui/ui_manager.c:95-103` | 残留死字段：`full_refresh_pending`、未使用的定时器句柄、`input_refresh_locked` |
| n8 | `sdkconfig` | 断言从 level 2 降级为 level 1（静默）——在生产中掩盖 bug |
| n9 | `spiffs/photos.idx` | 8 字节二进制索引文件提交到仓库——应在运行时生成 |

---

## 测试质量评估

| 测试文件 | 断言数 | 行数 | 评估 |
|---|---|---|---|
| `test_storage_manager.c` | 45 | 160 | ✅ 有真实断言 |
| `test_photo_blit.c` | 39 | 88 | ✅ 有真实断言 |
| `test_epd_refresh.c` | 24 | 184 | ✅ 有真实断言 |
| `test_clock.c` | 21 | 139 | ✅ 有真实断言 |
| `test_system_info.c` | 11 | 77 | ✅ 有真实断言 |
| `test_widgets_basic.c` | 0 | 376 | ⚠️ 仅冒烟测试（无 assert 调用） |
| `test_widgets_intermediate.c` | 0 | 583 | ⚠️ 仅冒烟测试 |
| `test_widgets_interactive.c` | 0 | 495 | ⚠️ 仅冒烟测试 |
| `test_widgets_complex.c` | 0 | 372 | ⚠️ 仅冒烟测试 |
| `test_widgets_calendar.c` | 0 | 230 | ⚠️ 仅冒烟测试 |

5 个控件测试文件（共 2,056 行）包含 `assert.h` 但包含 **零** 个 `assert()` 调用——仅验证「不崩溃」，而非不变量。核心数学/存储测试的断言使用正确。

---

## 已验证正确的部分（亮点）

- **BLE 图片接收重组** —— 仅追加模式、无攻击者可控偏移、mutex 在检查+拷贝全程持有（无 TOCTOU）、`expected_size` 以缓冲区大小为上限。安全关键路径可靠。
- **页面注册表** —— GCC constructor 自动注册正确搭配 CMakeLists.txt 中的 `WHOLE_ARCHIVE`；插入排序以 `MAX_PAGES` 为界。
- **Rawdraw 核心** —— 2bpp 帧缓冲区数学、像素边界检查、线/圆/矩形裁剪、布局算术——全部正确。
- **RTC PCF8563 驱动** —— BCD 编解码、I2C 错误累积和退出——较原始版本严格改进。
- **UTF-8 边界处理** —— text_chunker 和 ui_text_util 中的自同步、边界检查正确。
- **天气/编程计划 API 客户端** —— cJSON 根在所有路径上删除、响应缓冲区 NUL 终止、数组边界已强制执行。

---

## 总结

C 移植的架构设计扎实——组件边界、CMake 结构、OOP 模拟和 rawdraw 渲染核心执行到位。但 **4 个严重问题阻断合并**：

1. **9 个页面渲染空白**，因缺少 `.render` 虚函数表连接——每页一行修复，但使近半数 UI 不可用。
2. **生产密钥已提交**到 sdkconfig（WiFi 密码、API Key、JWT）——需要轮换凭据。
3. **未认证局域网控制**——至少需要对破坏性动词做 AP 模式门控。
4. **NFC NDEF 缓冲区溢出**——合法输入的直接内存破坏。

严重问题之外，重要发现集中在三个主题：**缺少同步**（WiFi 状态、照片存储、UI 回调、音频管道）、**未检查内存分配失败**（cJSON NULL 解引用、text_chunker 数据丢失、NULL 帧缓冲区 memset）、**移植不完整**（丢弃服务器元数据、死代码 arena 缓冲区、未连接的监听状态）。这些在合并前均应修复。
