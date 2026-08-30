# 架构加固计划：安全收敛 + 依赖卫生 + 扩展规则

> 入口：B（追溯实例化）——源起 2026-08-29 架构评审（对 b99586f 工作区的结构/可维护性/可扩展性评审，结论见仓库会话记录）。
> 平台：ESP32-S3 / ESP-IDF 6.0.2 / SSD2683 四色 EPD。
> 状态：**已实施完成并合并 main（1eaf263，2026-08-30 上板验证通过）**。

## 修订日志

| 版本 | 角色 | 概要 |
|------|------|------|
| v0 | P（追溯实例化） | 从评审发现实例化需求：SEC1 token 泄露、S1/S2/S3 依赖倒置与幽灵声明、M1 转发枢纽税；ask 收口 token 渠道与 M1 范围 |
| v1 | A（架构作者） | 需求→方案映射、三批次方案、特判映射、并发与恢复分析 |
| v2 | B1 系统专家 + B2 安全专家 | B1：ensure_auth_token 并发竞态→预生成；AP 模式 token 可达性→传图页显示；fm_fetch 任务上下文确认为既有债务。B2：?token= URL 通道收紧；settings/photos 残余面。拒绝 2 项（token 轮换、AP 密码本轮改） |
| v3 | C（PM/UX） | 手册漂移清单；网页 401 可见反馈；设置菜单扩容确认（visible_option_count=8 容得下）；拒绝二维码显示 |
| v4 | D（Product Owner） | 决策 D1–D9；发布门禁；[ASSUMPTION] 复核；切分 P1 安全先行 |
| v4.1 | 实施期修订（P2 执行中） | S3「network 幽灵依赖」被实施证伪：`#    include`（# 后缩进）与无组件前缀头（style.h/nvs_state.h）导致 v1 取证漏检，network→rawdraw/app_state 为真实依赖——§3.2.3/D5 撤销、判据 4 修订、附录 A-3 增补（级联自 v1 取证错误）。§3.2.2 增补：epd_refresh.c 对 rawdraw_ext 的真实依赖以「几何助手下沉 display_types.h 为单一事实源 + rawdraw_ext.c 公开函数委托」断边，替代原「仅移类型头」方案 |
| v4.2 | 用户决策 + 实施期修订（P1 合并前） | D10：认证功能编译开关 CONFIG_TRANSFER_AUTH_ENABLE 默认不启用（用户 2026-08-29 指示；风险已告知——默认构建 LAN 端点无认证）。同轮证伪 [v2] 前提：is_authorized 的 AP 模式旁路（b99586f 既有，WPA2 密码即门槛）使 AP 模式永不查 token，而传图页仅 AP 模式可见 → §3.1.3 令牌行/三注入在所有配置下均为死代码，撤销并移除；手册 §6.2 表述随实还原 |
| 完成 | 实施复盘（2026-08-30） | 上板验证通过，arch-hardening（7 提交）fast-forward 合入 main。未预见问题两条均已按 §8 回写：v1 取证漏检（S3，v4.1）、[v2] AP 前提错误（D11，v4.2）；额外收获：审计链发现并立修 main 既有省电模式重启 bug（fd85e00）。[ASSUMPTION] A1 随 D11 失效（AP 永不需要 token）、A3 已上板证实（设置页 13 项正常）。实施工时：3 批次 + 1 增补共 7 提交，6 任务 ×（实施+规格审+质量审）子代理流水线，4 轮返工闭环 |

---

## 第〇节 需求实例（v0）[追溯实例化]

- **原始诉求**： 用户原话「请作为架构师对代码结构，可维护性，可拓展性方面进行评审」→ 评审交付后确认「继续」，将优先级 1–3 项落为可实施计划。
- **问题陈述**：
  1. LAN HTTP 的 `/status` 无认证返回访问令牌，使 D11 端点认证体系在局域网模式整体失效（安全）；
  2. 两处类型级反向依赖（rawdraw→bsp_peripherals、bsp_display→rawdraw）与两处 CMake 幽灵依赖残留在已重构的分层上（结构）；
  3. `ui_manager` 以「每页 N 个 setter 转发」模式随页面数线性膨胀，且仓库已同时存在新旧两种调用约定（可扩展性）。
- **主场景**： 单人维护的家用墨水屏面板固件；局域网内手机/电脑浏览器传图与管理（高频、日常）；维护者本人新增页面/数据源（每次功能迭代）。
- **范围内**：
  1. SEC1：token 撤出 `/status`；设置页新增「访问令牌」只读项；网页首访手输 + localStorage 持久化（[v4.2] 原「AP 传图页显示令牌」随 D11 撤销）；
  2. 认证补齐：`POST /settings` 全字段、`GET /photos` 纳入认证；删除 `?token=` URL query 认证通道；
  3. S1：`time_year_is_plausible` 纯谓词下沉 data_types，斩断 rawdraw→bsp_peripherals；
  4. S2：`display_types.h` 移入 data_types，bsp_display 摘除 `REQUIRES rawdraw`；
  5. S3：删除 network CMake 幽灵 `REQUIRES rawdraw、app_state`；
  6. M1：固化「page-specific setter 直调页面 API」规则 + 迁移 fridge_memo 3 个 setter 作示范；
  7. 手册同步（user-manual.md §6.1、§5 设置表、Q8）。
- **范围外**（与范围内同权重）：
  - 语音流式链路修复（B1 挂账，见附录 A-1，语音恢复前置）；
  - `network→sleep_manager` 语义越界清理（附录 A-3 遗留）；
  - `settings_page.c` 继续拆分（附录 A-4 另立项）；
  - `application.c` 按钮样板表驱动（明确不做，纯审美收益）；
  - ui_manager 存量其余转发（chat×8/settings×3/wifi×2/gallery）迁移——标记遗留，不本轮迁移；
  - AP 密码强度/随机化（附录 A-2，需配置通道，另立项）；
  - token 轮换/TOTP（明确不做，见附录 B-B1）。
- **成功判据**（可观测、含口径）：
  1. LAN 模式 curl 矩阵（CONFIG_TRANSFER_AUTH_ENABLE=y 构建下）：`GET /status` 响应体无 `token` 字段；无/错 token 的 `POST /upload`、`GET|DELETE /photo`、`POST /photo/meta`、`POST /photos/move`、`POST /photo/show`、`POST /settings`、`GET /photos` 全部 HTTP 401；持正确 token 全部 2xx。默认构建（=n）：同矩阵全部 2xx（无认证）。[v4.2 修订：口径随 D10 配置化]
  2. 设置页「访问令牌」项：CONFIG_TRANSFER_AUTH_ENABLE=y 时显示 8 位 token（与 NVS `auth/token` 一致，串口日志核对）；默认（=n）显示「未启用」且零 token 生成路径。[v4.2 修订：删「AP 传图页显示同一 token」——AP 模式旁路使然，见 §3.1.3 撤销]
  3. 浏览器无缓存首访（CONFIG_TRANSFER_AUTH_ENABLE=y 构建下）：出现令牌输入提示；输入正确后上传/删除/轮播设置全流程可用；二次访问免输（localStorage）。默认构建（=n）：全程无令牌提示。[v4.2 修订：判据随 D10 配置化]
  4. CMake 断言：`rawdraw/CMakeLists.txt` 无 `bsp_peripherals`；`bsp_display` 无 `rawdraw`；`idf.py build` 零 warning。[v4.1 修订：删「network 无 rawdraw、app_state」——该两项为真实依赖（ble_image_receiver.c:32 用 style.h 屏宽常量、weather_api/coding_plan_api 用 nvs_state 读写配置），S3 证伪，见 D5]
  5. `grep -c fridge_memo components/ui/ui_manager.c` = 0；app_sync 经 `page_registry_get_instance` 直调页面 API；host 14 套件全绿（[v4.1 勘误]：基线即 14，v1 误记 13）；
  6. 手册描述与实测行为一致（token 获取/输入流程逐条对）。
- **硬约束**：
  - SSD2683 全刷 ~15s、无局部刷新——设置页新增项不得引入额外刷新轮次；
  - 不允许变：AP 模式进入/退出流程、传图功能、30min 空闲自动关、传输中粘性、每设备 token 机制本身；
  - 分层不变式：bsp_* 不依赖 rawdraw/network/ui；rawdraw 不依赖 bsp_*/network/ui；audio 不依赖 ui；
  - 手册是行为契约：任何用户可见变化同步 user-manual.md；
  - host 测试体系（tests/run_tests.sh，14 套件）必须持续全绿（[v4.1 勘误]：基线即 14）。
- **[ASSUMPTION] 未决项**：
  - A1: 无路由器（AP 模式）场景用户可接受「从传图页屏幕读 token → 手输网页」一步。[v2] 已通过传图页显示令牌把该成本最小化；复核条件：上板实测 AP 模式完整传图一次。
  - A2: 家庭 LAN 信任模型下静态 token（无轮换）足够。复核条件：出现多住户/办公环境需求时重开。
  - A3: 设置菜单 `items[12]→[13]` 无渲染风险（`visible_option_count=8`，网络节 4 项 < 8，右侧面板可滚动）。复核条件：上板目检设置页。
- **已知事实清单**（评审取证，file:line 以 b99586f 为准）：
  - `ap_transfer_server.c:611-627` `/status` 无 `is_authorized`，`:623` token 写入响应体与 URL；
  - `ap_transfer_server.c:500,780,836,881,907` upload/photo(DELETE)/meta/move/show 已认证；`:433-452` 常数时间比较；`:396-424` 每设备 token（NVS auth/token）；
  - `ap_transfer_server.c:653-664` `POST /settings` slideshow_interval 无认证写入；`:718` `GET /photos` 无认证；
  - `ap_transfer_server.c:472-481` `?token=` query 认证通道；
  - `rawdraw/clock.c:7,91,106` include `rtc_time_valid.h`，仅用 `time_year_is_plausible`；
  - `rawdraw/include/display_types.h`（33 行纯几何类型）+ `rawdraw.h:7`、`epd_refresh.h:6` 双方 include；`bsp_display/CMakeLists.txt:3` `REQUIRES rawdraw`；`epd_driver.c` 无 rawdraw 引用；
  - `network/CMakeLists.txt:6` `REQUIRES ... rawdraw app_state`。[v4.1 修正：v1 判断「代码零引用」为取证错误——`#    include` 缩进形式与无组件前缀头（ble_image_receiver.c:32 `style.h`、weather_api.c:30 `nvs_state.h`）漏检；两项均为真实依赖，REQUIRES 保留]
  - `ui_manager.c:567-753` chat×8/settings×3/wifi×2/fridge_memo×3 转发；`:779-847` gallery slideshow；
  - `application.c:162-183,653` 已用 `page_registry_get_instance` 直调页面 setter（ap_transfer、photo_gallery）——新约定已事实存在；
  - `app_settings_menu.c:240` `settings_page_item_t items[12]` 已满；`settings_page.c:47` `visible_option_count 8`；
  - `user-manual.md:334`「网页自动从设备获取本机唯一 token 并携带，无需手动输入」为文档化现状；`:271-275` 设置表网络节三项；`:341` AP 模式描述；
  - `fridge_memo_api.c:506,526` fetch/delete 走独立任务 `fm_fetch`/`fm_del`（16KB 栈，优先级 5）；
  - host 测试 14 套件 3.4–3.6s 全绿（2026-08-29 实测；[v4.1 勘误] 原 v1 误记 13）。
- **ask 问答记录**（2026-08-29，原样入档）：
  - Q1 token 交付渠道 → 答：**设置页只读项显示**（推荐项）；网页首访输入 + localStorage；/status 不再返回 token。
  - Q2 M1 实施范围 → 答：**立规 + 迁 fridge_memo 作示范**（推荐项）；其余存量标记遗留。

---

## 1. 需求→方案映射表（v1）

| v0 范围内条目 | 方案响应 | 阶段 |
|---|---|---|
| SEC1 token 撤出 /status | §3.1.1 /status 响应删 token 字段与 url 的 query 串 | P1 |
| 设置页「访问令牌」项 | §3.1.2 app_settings_menu 网络节新增 NORMAL 项 + APP_SETTINGS_TOKEN_INDEX | P1 |
| AP 传图页显示令牌 [v2] | ~~§3.1.3 ap_transfer 指引页增加令牌行~~ [v4.2 撤销：前提证伪，见 D11] | — |
| 网页手输 + localStorage | §3.1.4 内嵌 HTML bootstrap 改造 + 401 可见反馈 [v3] | P1 |
| /settings、/photos 认证补齐 | §3.1.5 两个 handler 补 is_authorized | P1 |
| 删 ?token= query 通道 [v2] | §3.1.6 is_authorized 去掉 query 分支 | P1 |
| ensure_auth_token 并发 [v2] | §3.1.7 server start + 菜单构建时预生成 | P1 |
| S1 谓词下沉 | §3.2.1 data_types/time_window.h | P2 |
| S2 display_types 归位 | §3.2.2 移文件 + 三方 CMake 调整 | P2 |
| S3 幽灵 REQUIRES | ~~§3.2.3 network 清理~~ [v4.1 证伪撤销：真实依赖，不清理] | — |
| M1 立规 + 示范迁移 | §3.3 规则落点 + fridge_memo 直调改造 | P3 |
| 手册同步 | §3.4 三处修订 | P1（随行为变化） |

## 2. 目标与非目标

**目标**：LAN 模式攻击面从「同网任意设备可完全控制」收敛为「需物理接触屏幕或持有 token」；分层依赖图反向边清零；新增页面的 ui_manager 触点归零（规则化）。

**非目标**：不改传图功能与 UX 主流程（除 token 输入一步）；不迁移存量转发；不修语音链路；不做 token 轮换。

## 3. 方案设计

### 3.1 P1 安全收敛（用户可感知，独立提交）

**3.1.1 /status 净化**（`ap_transfer_server.c:611-627`）
响应体改为 `{"status":"ready","mode":"%s","ip":"%s","url":"http://%s/"}`——删除 token 字段与 url 的 `?token=` 后缀。mode/ip 保留（低敏感，网页需要）。

**3.1.2 设置页令牌项**（`app_settings_menu.c:240-295`）
- `items[12]` → `items[13]`；网络节「局域网IP」之后插入 `{"访问令牌", <token>, SETTINGS_ITEM_NORMAL}`（只读、不可聚焦动作）。
- 值来源 `ap_transfer_server_get_token()`（菜单构建时调用即触发 D6 预生成）。
- `application_internal.h` 增加 `APP_SETTINGS_TOKEN_INDEX`；LAN 服务开关回调处同步该项可见性不需要——token 常显（见 D1 理由）。

**3.1.3 AP 传图页令牌行 [v2]（[v4.2] 撤销）**：原判断「AP 模式网页同样要求 token（upload_handler:500 不分模式）」为前提错误——handler 层确不分模式调 is_authorized，但 is_authorized 内部对 AP_SERVER_MODE_AP 直接放行（b99586f 基线既有：「AP mode keeps its WPA2 password semantics」），且传图页仅 AP 模式下可见。故令牌行与三处注入在所有配置下均为死代码，已实施后整体移除（渲染循环 sizeof 修正保留）。AP 直连模式的安全门槛=热点 WPA2 密码（与 D10 无关的无条件既有语义）。

**3.1.4 网页 bootstrap 改造**（`ap_transfer_server.c` 内嵌 HTML，:162-239 区域）
- `authToken` 初始化改为 `localStorage.getItem('inkscreen_token') || ''`；
- 任一请求 401 → 显示令牌输入浮层（输入框 + 确认按钮），提交后写 localStorage 并重试；
- 401 反馈必须可见（「令牌错误或缺失」文案），禁止静默空列表 [v3]；
- 删除对 `j.token` 的依赖。

**3.1.5 认证补齐**（`ap_transfer_server.c`）
- `settings_handler`（:629）：方法为 POST 即先 `is_authorized`（原有 :680-688 的破坏性分支检查随之冗余，删除）；
- `photos_handler`（:718）：入口补 `is_authorized`。

**3.1.6 删除 query 认证通道**（:472-481）[v2]
`is_authorized` 删除 `?token=` 分支，仅保留 Authorization header（Bearer/bare）。理由：URL query 进浏览器历史与中间设备日志；屏幕不显示带 token 的 URL 后无消费方。

**3.1.7 ensure_auth_token 预生成**（:401-424）[v2]
现状：多个 httpd handler 可并发首次调用 `ensure_auth_token`，对静态 `s_auth_token[9]` 存在 snprintf 竞态。改为：`ap_transfer_server_start`（LAN/AP 两模式共同入口）与 app_settings_menu 构建时各调用一次 `ap_transfer_server_get_token()`；handler 内 `ensure_auth_token` 保留为幂等兜底（首字节非空即返回）。

### 3.2 P2 依赖卫生（零行为变化）

**3.2.1 谓词下沉**：新建 `components/data_types/include/time_window.h`（`time_year_is_plausible` 迁入，纯函数）；`rtc_time_valid.h` include 之并保持 `pcf8563_regs_time_plausible`（寄存器语义留在 bsp）；`rawdraw/clock.c:7` 改 include `time_window.h`；`rawdraw/CMakeLists.txt:7` 删 `bsp_peripherals` 增 `data_types`。`calendar_page.c` 继续用 `rtc_time_valid.h`（其还需 NAV 窗口），传递包含即可。

**3.2.2 display_types 归位**：`git mv components/rawdraw/include/display_types.h components/data_types/include/`；`rawdraw/CMakeLists.txt` 增 `data_types`；`bsp_display/CMakeLists.txt:3` 删 `rawdraw` 增 `data_types`（`epd_refresh.h:6` 与 `rawdraw.h:7` 的 include 语句不变）。

**3.2.3 幽灵清理**（[v4.1] 撤销）：v1 取证断言「network 代码零引用 rawdraw/app_state」被实施证伪——`ble_image_receiver.c:32` include rawdraw 的 `style.h`（用 `STYLE_SCREEN_WIDTH`）、`weather_api.c:30`/`coding_plan_api.c` 经 `nvs_state.h` 读写应用配置（均为 `#ifdef ESP_PLATFORM` 内的 `#    include` 缩进形式，头名不带组件前缀，前缀式 grep 漏检）。删除 REQUIRES 实测构建失败。结论：REQUIRES 保留；network→rawdraw 的常量借用记附录 A-3 遗留。

### 3.3 P3 扩展规则固化（M1）

**规则**：page-specific setter 一律由编排层（main/`app_sync`）经 `page_registry_get_instance(UI_PAGE_X)` 直调页面公开 API；**禁止**在 ui_manager 新增页面转发函数。落点：`page_registry.h:21-39` 注释区扩写 + `ui_manager.h` 转发声明区顶部加「遗留转发层，新页面禁止扩展」标注。

**示范迁移**（fridge_memo）：
- `app_sync.c:172-220`：`ui_manager_update_fridge_memo` / `ui_manager_set_fridge_memo_footer` / `ui_manager_set_fridge_memo_offline` 三处改直调 `fridge_memo_page_*`（经 `page_registry_get_instance(UI_PAGE_FRIDGE_MEMO)`）；
- 删除 `ui_manager.c:723-753` 三个函数与 `ui_manager.h` 对应声明；
- 全仓 grep 确认无残余调用点。

### 3.4 手册同步（P1 内）
- `user-manual.md:334` 改为：「令牌显示在设备上（设置→网络→访问令牌；AP 模式在传图页）。网页首次访问需输入一次，之后浏览器记住。脚本调用在请求头加 `Authorization: Bearer <token>`」；
- `:271-275` 设置表网络节补「访问令牌」行（只读）；
- `:341` AP 模式描述补「网页需输入屏幕显示的令牌」。

## 4. 特判映射表

| 特判 | 去处 | 阶段 |
|---|---|---|
| AP 模式 token 获取路径不同于 LAN | §3.1.3 传图页令牌行 | P1 |
| settings_handler 破坏性分支旧检查 | §3.1.5 整体前置后删除 | P1 |
| ensure_auth_token 幂等兜底保留 | §3.1.7 | P1 |
| calendar_page 继续用 rtc_time_valid.h | §3.2.1 传递包含，不强制改 | P2 |
| 存量 ui_manager 转发不迁移 | §3.3 规则只约束增量 | P3 |

## 5. 分阶段计划与验收

| 阶段 | 内容 | 独立验收（引用 v0 判据） | 回退 |
|---|---|---|---|
| P1 安全 | §3.1 全部 + §3.4 手册 | 判据 1/2/3 + 门禁 3 | 单提交 revert，网页回退为 /status 自动获取（接受已知漏洞回退） |
| P2 卫生 | §3.2 全部 | 判据 4 + host 全绿 + build 零 warning | 单提交 revert（纯移动） |
| P3 规则 | §3.3 | 判据 5 | 单提交 revert |

每阶段完成即提交（英文 conventional commits），P1 上板验证通过后方合入 P2/P3（P2/P3 可并行于 P1 验证期间开发，但不先合）。

## 6. 并发与恢复分析（v1，[v2] 补充）

- **httpd handler 并发**：max_open_sockets=4（`ap_transfer_server.c:935`），handler 可并发；`s_last_lan_activity_ms`（:58）与 `ensure_auth_token` 为共享可变状态。前者为 int64 原子对齐写、语义为 touch，可容忍；后者由 §3.1.7 预生成消除窗口。[v2]
- **fm_fetch / fm_del 任务**（`fridge_memo_api.c:506,526`）：回调在独立任务上下文执行，页面 setter 跨任务写 `needs_full_refresh_flag` 与页面 struct——P3 迁移**不改变**该既有并发面（ui_manager 转发同样是跨任务调用）。标为既有债务（附录 A-5），本计划不扩大也不修复。[v2]
- **深睡/刷新交互**：设置页新增项不引入新刷新路径；LAN 服务 30min 自动关与传输粘性逻辑（D3/D11 既有决策）不动。
- **失败恢复**：token 输错 → 401 → 网页浮层重试；NVS auth 命名空间损坏 → `ensure_auth_token` 重新生成（既有行为）；P2 文件移动若遗漏 include → 编译期即失败，无运行时风险。

## 7. 风险表

| 风险 | 概率 | 影响 | 缓解 |
|---|---|---|---|
| 内嵌 HTML 改造引入网页 JS 错误 | 中 | 传图不可用 | 浏览器实测（判据 3）；HTML 为字符串数组、改动局部 |
| ap_transfer 页 7 行布局超界 | 低 | 显示拥挤 | 行高自适应循环（:124）；上板目检 [A3] |
| 移动 display_types.h 遗漏使用方 | 低 | 编译失败 | include 语句不变 + build 零 warning 门禁 |
| 设置菜单 13 项焦点序偏移 | 低 | 导航错位 | APP_SETTINGS_*_INDEX 常量集中管理（application_internal.h） |
| 删 query 通道破坏未知脚本 | 低 | 外部脚本 401 | 手册从未承诺 query 通道（:334 只写 header）；发布说明注明 |

## 8. 测试与验收

- **host**：`tests/run_tests.sh` 14 套件全绿（[v4.1 勘误]；P2 触及 rtc_time_valid 传递包含、P3 触及 app_sync——若 app_sync 无 host 覆盖则以编译+板测覆盖）。
- **target**：`source ~/.espressif/v6.0.2/esp-idf/export.sh && idf.py build` 零 warning。
- **板测脚本**（判据 1/2/3）：curl 矩阵（7 端点 × 无/错/对 token）；设置页与传图页目检；浏览器首访/二次访问/输错重试三路径。
- **回归**（门禁 1）：切页双刷新不复发（input_refresh_locked）；充电插入刷屏循环不复发（P1 验证时插拔充电器操作设置页）。

---

## 9. 决策记录（v4，终态）

### D1 token 交付渠道：设备屏幕显示 + 网页手输一次（发布批次 P1）
- **规则**：`/status` 永不返回 token；设置页「访问令牌」只读项常显（不随服务开关隐藏——服务关时 token 无害，且免得用户开服务后发现要回设置页找）；AP 传图页显示令牌行；网页 localStorage 记忆。
- **理由**：保密通道与显示通道分离；EPD 屏幕即离线可信通道。被否选项：URL?token= 上屏（token 入 URL 历史/日志，且与 D2 冲突）；保持 /status 自动下发（等于放弃认证）。用户 ask 已选定（2026-08-29）。
- **落点**：§3.1.1–3.1.4。
- **回退**：revert P1 提交。

### D2 认证语义收敛：全端点除 index/status 外一律认证；删除 query 通道（P1）
- **规则**：`is_authorized` 仅认 Authorization header；`POST /settings`、`GET /photos` 补齐；index（无敏感）与 status（已净化）豁免。
- **理由**：B2 发现 query 通道使 token 进浏览器历史与代理日志；无屏幕 URL 消费方后成纯攻击面。被否：保留 query 通道（兼容性收益为零）。
- **落点**：§3.1.5–3.1.6。

### D3 M1 范围：立规 + fridge_memo 示范迁移（P3）
- **规则**：新页面 setter 禁止进 ui_manager；存量 chat/settings/wifi/gallery 转发标记遗留。
- **理由**：用户 ask 选定；fridge_memo 最新、回归面最小（单页面三函数）。被否：全量迁移（回归面覆盖三大高频页面，收益边际）。
- **落点**：§3.3。

### D4 类型下沉落点：data_types 组件，不新建（P2）
- **理由**：33 行几何类型 + 1 个纯谓词；data_types 已是双方（ui/network）共同依赖的叶子。被否：独立 display_hal/geo_types 组件（多一个组件边界，维护税>纯度收益；旧计划 4.1 的 display_hal 构想在此仓库尺度下过度）。
- **落点**：§3.2.1–3.2.2。

### D5 CMake 依赖清理清单（P2）
- ~~`network` 删 `rawdraw`、`app_state`~~（[v4.1] 证伪撤销：真实依赖，理由见 §3.2.3，级联自 v1 取证错误）；`rawdraw` 删 `bsp_peripherals` 增 `data_types`；`bsp_display` 删 `rawdraw` 增 `data_types`。验收 = 判据 4 的 grep 断言 + build 零 warning。[v4.1 增补：epd_refresh.c 对 rawdraw_ext.c 的链接依赖以「display_types.h 内联 display_rect_area/union/align_x8 单一事实源 + rawdraw_ext.c 公开函数一行委托」断边，公开 API 零变化]

### D6 ensure_auth_token 预生成时机：server start + 设置菜单构建（P1）
- **规则**：两处显式调用 `ap_transfer_server_get_token()`；handler 内保留幂等兜底。
- **理由**：B1 发现的多 handler 并发首调用竞态；顺带保证设置页任何时刻有值可显。被否：加互斥锁（多余复杂度，预生成后无竞争窗口）。

### D7 语音链路（B1/H1）：挂账不实施（不在发布批次）
- **规则**：附录 A-1 为语音恢复的强制前置清单；恢复语音开发前必须先完成清单项，禁止带着已知静默丢文本缺陷接线。
- **理由**：链路 parked（stream_pipeline begin/feed/end 零调用者），现在修无法验证；但挂账防止未来直接踩 H1。

### D8 机会项处置（2026-08-29）
- AP 密码随机化/可配置：**拒绝本轮**——需配置通道（网页/NFC/串口）与 UX 设计，另立项（附录 A-2）。
- 设置页令牌二维码：**拒绝**——4 色屏有二维码先例（挪车页）但手机本就在输 URL，扫码输 token 的边际收益低于实现与布局成本（附录 B-B2）。
- settings_page 拆分：**另立项**（附录 A-4）。
- S4 network→sleep_manager：**遗留不修**（附录 A-3，方向合法、影响为零）。

### D9 发布切分：P1 先行独立上板验证（发布批次 P1 → P2+P3）
- **理由**：P1 是唯一用户可感知批次（安全+手册），价值拐点在 P1 交付；P2/P3 零行为变化，风险隔离后可合并交付。被否：三批次一次合入（P1 网页改造出问题时无法单独回退依赖移动）。

### D10 认证功能编译开关：CONFIG_TRANSFER_AUTH_ENABLE，默认不启用（发布批次 P1 增补，2026-08-29 用户指示）
- **规则**：Kconfig bool（menu 路径 ZecTrix E-Paper Panel → Server & API → Require token auth for the image transfer web server），default n。开启时恢复 P1 全部 LAN 认证语义（7 端点 401、token 预生成、网页手输、设置页显示 8 位令牌）；关闭（默认）时 LAN 端点信任同网段、零 token 生成/NVS 写、设置页令牌项显示「未启用」、网页无令牌提示。AP 直连模式维持既有旁路（WPA2 密码即门槛），与开关状态无关。
- **理由**：用户取舍——家庭局域网信任模型下降低使用摩擦（IP 变更/换浏览器需重输令牌），安全增强保留为可选编译项。风险已明示并接受：默认固件 LAN 端点无认证。被否：默认开启（违背用户指示）；运行时 NVS 开关（认证被绕过时开关本身无保护，且需默认凭据引导）。
- **实现落点**：`main/Kconfig.projbuild`（TRANSFER_AUTH_ENABLE）；`ap_transfer_server.c`（is_authorized 双态/token_matches 条件编译/start 预生成包裹）；`app_settings_menu.c`（令牌项双态，关闭时不调 get_token）；`application.c`（原三处注入已随 §3.1.3 撤销移除）；`user-manual.md` §4.2/§6.1/§6.2 双态描述。
- **回退**：menuconfig 打开即恢复；代码级 revert 本增补提交。
- **门禁联动**：发布门禁 2/3 的 curl 矩阵与核心价值指标仅在 =y 构建下适用；默认构建的门禁=双配置均零 warning 构建 + host 全绿 + 默认态 nm 零 token 符号引用。

### D11 [v2]「AP 模式需要 token」前提修正（级联自 v2，v4.2 收口）
- **规则**：AP 直连模式永不要求 token——`is_authorized` 对 `AP_SERVER_MODE_AP` 无条件放行（b99586f 基线既有语义，热点 WPA2 密码即门槛），与 D10 开关状态无关。§3.1.3 传图页令牌行与三处注入据此撤销移除；手册 §6.2 按「AP 模式免令牌」表述。
- **理由**：v2 B1 的发现基于「upload_handler:500 不分模式」的表面读法，未穿透到 is_authorized 内部旁路；传图页仅在 AP 模式可见，故该特性在所有配置下均为死代码。经验教训入册：handler 层「检查不分模式」≠语义层「要求不分模式」。

## 10. 发布门禁（一票否决）

1. **历史事故回归**（第一优先）：切页双刷新（input_refresh_locked 语义，`ui_manager.c:459-523` 路径）不复发；充电插入刷屏循环（`app_sleep.c:117-125` 咽喉）不复发——P1 板测时在设置页操作期间插拔充电器。
2. **v0 成功判据 1–6 逐条核对**（口径已在判据内写死：curl 矩阵端点清单、grep 断言、host 套件数）。
3. **核心价值指标**：LAN 模式下无 token 设备对 7 个写/读端点 100% 收到 401（判据 1 的统计口径）。
4. **既有功能不劣化**：LAN+AP 双模式传图全流程（上传/列表/删除/元数据/移动/设为展示/轮播设置/关服务）；30min 空闲自动关；传输中切页粘性。
5. **构建与测试门禁**：host 14 套件全绿（[v4.1 勘误]）；`idf.py build` 零 warning；手册 diff 审阅（§3.4 三处）。

---

## 附录 A：挂账与既有债务

### A-1 语音恢复强制前置清单（D7，B1 挂账）
1. `chat_page.c:483` `!r->is_streaming` 守卫：断线重连场景 begin/end 配对语义（`protocol.c:154-164` DISCONNECTED 不清流状态）——需 end_stream + 丢弃或缓冲策略；
2. `protocol.c:179` `process_incoming_text` 忽略 `payload_offset/payload_len`——esp_websocket 对超缓冲大 JSON 分帧回调，需重组；
3. LLM 事件→`stream_pipeline_{begin,feed,end}` 接线（当前零调用者，`stream_pipeline.c:65-117`）；
4. 接线后回归：流式中断线→恢复→文本不静默丢失。

### A-2 AP 密码强度（机会项，另立项）
`InkScreen-AP`/`12345678` 印于手册（user-manual.md:339）与屏幕（ap_transfer_page.c:110-117）。物理邻近者读屏即可接入。需配置通道设计后立项。

### A-3 network→sleep_manager 语义越界（遗留）
`coding_plan_api.c:10`、`weather_api.c:12`。方向合法、零行为影响。编排层时序重构时顺带清理。

### A-3b network→rawdraw 常量借用（遗留，[v4.1] 新增）
`ble_image_receiver.c:32,281-282` include rawdraw `style.h` 借用 `STYLE_SCREEN_WIDTH`。数据服务层感知图形层屏宽常量属语义越界；屏宽属设备物理属性，应源于 bsp_board/config。下次触碰 BLE 图片接收时改为 config 常量注入。

### A-4 settings_page.c 拆分（另立项）
1251 行，最大页面。about/themes 已拆，余列表+对话框+设备信息。

### A-5 API 回调跨任务写页面状态（既有债务，[v2]）
fm_fetch/fm_del 任务回调直写页面 struct 与 `needs_full_refresh_flag`（渲染在主任务读）。weather/coding_plan 同模式。当前以「bool 标志 + 原子对齐」侥幸安全；任何页面 struct 变为多字段联动写入时必须回到此处改为事件队列投递。

## 附录 B：明确不做（拒绝留档）

- **B1 token 轮换/TOTP/挑战应答**（[v2] B2 提出，拒绝）：家庭 LAN 威胁模型下静态每设备 token + 常数时间比较已足；轮换引入设备-浏览器同步问题，复杂度不成比例。重开条件：多住户环境（A2）。
- **B2 设置页令牌二维码**（[v3] C 提出，拒绝）：见 D8。
- **B3 ui_manager 存量转发全量迁移**（拒绝）：见 D3 被否选项。
- **B4 统一事件总线**（旧计划 4.3 已否，维持）：data_refresh.c 20 行单回调够用；页面数 <25 时无重构收益。
