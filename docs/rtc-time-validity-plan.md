# RTC 时间有效性与日历离线渲染守护计划

| 版本 | 角色 | 概要 |
|---|---|---|
| v0 | 产品经理（P） | 故障实例化：半夜日历变 1月1日；分诊=系统性；4 项决策问询已收口 |
| v1 | 架构作者（A） | 三层守卫（bsp 有效性判定 / main 信任链与自愈 / 渲染与睡眠 fallback）；P1–P3 阶段切分 |
| v2 | B1 系统专家 | 塌缩三态为 bool；年份窗口按语义分立（TIME 2099 / NAV 2050）；补 fallback 顺序依赖与 VL 误伤权衡 |
| v4 | Product Owner | 判据 4 口径修正（D2）；O-2 批准并入 P2、O-1/O-3 否决（D3）；[ASSUMPTION] 维持（D4）；门禁冻结（D5）；v4 冻结为实施基线 |
| 实施 | — | P1+P2 代码落地（2026-08-26）：host 13 套件全绿 + idf.py 零 warning；上板验证待做。细节偏差见修订日志下方实施注记 |

实施注记（与方案的细节差异，均不改变设计语义）：
1. NVS 防固化落地为「导航钳制」：UP/DOWN 在 2020-01 / 2050-12 边界直接返回 false，任何写路径都产生不了窗口外的值；存量污染由 init 侧拒绝兜住。
2. 3.4 系统时间 fallback 顺带回写 RTC 基准（`pcf8563_set_time`）——B1-6 顺序约束的必然推论：不回写基准则 day 位永失配。
3. 早期退出收窄到「无人值守唤醒」（RTC 闹钟/TIMER 且非 BOOT 键/充电）：按键唤醒继续开机走 Wi-Fi+SNTP 自愈，避免"用户面前睡回去"。
4. 占位文案为「时间未同步 / 联网后自动校准」两行（方案原文的"按任意键联网校时"与实际自动 SNTP 行为不符）。
5. 新增 `calendar_page_time_source` 注入钩子与 `widget_calendar_set_today`（widget 不再自行读时钟，today 由 owner 注入）——host 可测性的最小改动。
6. `app_sleep_deep_sleep_now(keep_rtc_alarm, timer_us)` 收敛了 scheduled/manual/早期退出三条睡眠尾巴的重复 ~30 行。

## 第〇节 需求实例（v0，故障型）

- 原始诉求: 「半夜发现日历变成1月1日了。」
- 问题陈述: 无效 RTC 时间（PCF8563 掉电复位值 2000-01-01 或读取失败后的 epoch 0）未经任何校验注入系统时间，经夜间离线唤醒路径渲染上屏并可能持久化污染 NVS，且整夜无纠错机会。
- 主场景: 冰箱贴设备夜间以电池供电深睡；00:01 MIDNIGHT 对齐 RTC 闹钟唤醒刷新日历；用户次日（或半夜路过）看到日历页停留在错误日期。频率=每晚一次刷新窗口；严重度=核心页面显示荒谬内容且不自愈直到白天联网。
- 范围内:
  1. 时间有效性判定（固件层守卫：RTC VL 位 + 年份窗口）
  2. 启动路径 `settimeofday` 前的校验
  3. 日历页渲染/数据接口对无效时间的拒绝与占位
  4. 时间无效的 RTC 唤醒：跳过刷新、重设闹钟重试、保持 EPD 旧画面
  5. NVS `cal_year`/`cal_month` 污染防护（写回与恢复双侧）
  6. 诊断日志（boot 时输出 RTC VL 位与原始寄存器，供硬件根因定位）
- 范围外:
  1. 硬件维修/改板（后备电池、供电轨）——本方案只产出诊断证据，硬件处置另立项
  2. SNTP 协议、时区、NTP 服务器改造
  3. 为修时间牺牲夜间省电设计（skip_wifi 路径必须保持：重试也不连网）
  4. 其余 18 个未迁移页面的时间守卫（仅日历链路，其它页留机会项）
- 成功判据:
  1. 注入无效时间（year<2020 或 VL=1 或 I2C 读失败）的任何启动路径，日历页不渲染该日期，EPD 保持上一画面（验证：host 单测 + 上板 mock RTC 复位值）
  2. 时间无效的 RTC 唤醒：重新武装闹钟（默认 +5min）后入睡，不连网、不渲染（验证：唤醒日志 + 电流/日志时序）
  3. 白天联网 SNTP 校正后，日历自动跳回正确当月（现有 `APP_EVENT_TIME_SYNC` 路径回归不劣化；上板回归）
  4. NVS 任何写入路径不再接受 year<2020 的日历导航数据；已污染的 NVS 值被拒绝并回退到**上次有效导航位置或占位**（时间恢复前不存在"有效时间"可退，[v4] D2 修订原"回退到有效时间"口径）；host 单测覆盖 2000/1970/2019/2020/2050 边界
  5. boot 日志含 RTC VL 位与时间寄存器原始值（上板验证日志输出）
  6. host 测试套件全绿 + `idf.py build` 零 warning（既有门禁）
  - SSD2683 全刷 ~15s：守卫拒绝渲染必须发生在刷新调度之前，不消耗刷新预算
  - 深睡唤醒 = 系统重启，时间权威 = PCF8563（`main.c:232` 每次启动重读）；系统时间无其它持久备份
  - 夜间离线路径（`main.c:310-324` skip_wifi）是电池寿命设计，不得为时间纠错连网
  - 未迁移页面行为零变化（page_runtime 框架既有约束）
- [ASSUMPTION] 未决项:
  1. 硬件掉电压根因未定位（用户确认后备电池存在且应正常 + 电池供电过夜仍出现复位值）：本方案以固件防御为主、诊断日志留证；若上板日志显示 VL 反复置位 → 触发硬件专项。复核条件=首周诊断日志。
  2. 无效时间重试上限未定：默认连续 3 次 +5min 重试后放弃本次日历服务（转入下一个自然唤醒周期），避免整夜每 5 分钟唤醒耗电。PO 轮复核。
  3. 用户未细看屏幕具体形态（Q1）：方案须同时覆盖「月视图整体跳坏月」与「底部信息条/老黄历显示坏 today」两条渲染路径。
- 已知事实清单:
  - `components/bsp_peripherals/rtc_pcf8563.c:77-93` — `pcf8563_get_time` 读 7 寄存器，**不检查 VL 位**（REG_SECONDS bit7）；I2C 成功即 return true
  - `components/bsp_peripherals/rtc_pcf8563.c:91` — `tm_year = from_bcd(buf[6]) + 100`：PCF8563 掉电复位寄存器=2000-01-01 00:00 → 解析为 2000 年
  - `main/main.c:231-242` — 启动读 RTC → `mktime` → `settimeofday`，**无年份合理性校验**；I2C 失败时保留系统时间（=0 → 1970-01-01，同样无守卫）
  - `main/main.c:310-324` — 日历页 RTC 唤醒 + holiday 缓存命中 → `skip_wifi=true` → SNTP 不启动 → 夜间无纠错
  - `components/ui/pages/calendar_page.c:209-214` — `calendar_page_init` 无守卫取 `today_*`
  - `components/ui/pages/calendar_page.c:233-244` — NVS `cal_year/cal_month` 校验失败回退 `r->today_year`（坏时间 → 月视图整体跳坏月）
  - `components/ui/pages/calendar_page.c:334-335,341-343` — UP/DOWN 导航把 `r->year/month` 写回 NVS（坏月可被固化）
  - `components/ui/pages/calendar_page.c:66-71` — 老黄历子视图直接取 `today_*`
  - `components/rawdraw/widgets/calendar.c:283-289` — widget init 同样无守卫取 today
  - `components/rawdraw/widgets/calendar.c:802-804` — 当月匹配时底部渲染「今天 M月D日」
  - `main/application.c:585-603` — `APP_EVENT_TIME_SYNC` 是唯一「SNTP 校正后跳回当月」路径（白天自愈依赖它，回归必须保持）
  - `components/rawdraw/clock.c:89,105` — 时钟控件已有 `<2020 → "--:--"` 守卫（仓库既有惯例：无效时间显示占位）
  - `components/ui/page_runtime.c:132-163` — MIDNIGHT 对齐/catch-up 算法（纯逻辑，host 可测）
  - `main/app_sleep.c:127-148` — MIDNIGHT 对齐武装 RTC 闹钟；catch_up=+5min
  - `components/bsp_board/include/config.h:31-33` — PCF8563T/5，RTC_INT=GPIO5，I2C 0x51
  - `components/bsp_board/board.c:122-128` — RTC I2C 设备注册（400kHz）
- ask 问答记录（2026-08-26）:
  - Q1 半夜屏幕形态 → 「没细看」（→ [ASSUMPTION] 3）
  - Q2 RTC 后备电池状态 → 「有且应该正常」（→ [ASSUMPTION] 1，硬件根因成悬案，诊断优先）
  - Q3 出问题当晚供电 → 「电池供电」（主电池低电压假说保留，待日志验证）
  - Q4 时间无效时日历显示 → 「跳过该次刷新」（+5min 重试闹钟；EPD 保持旧画面；重试不连网）→ 已并入成功判据 2

## 第一节 现状与根因（v1）

根因不是缺某个检查，是**缺一个横切的「时间有效性」概念**：同一判定（"这个时间可信吗"）散落/缺失于 4 处——`rawdraw/clock.c:89` 有 `<2020` 守卫，而 `main.c:232`（时间注入）、`calendar_page.c:209-244`（today 取值与 NVS 回退）、`rawdraw/widgets/calendar.c:283-289`（widget init）三处裸取。无效时间因此能直达系统时间 → 直达渲染 → 经 NVS 写回持久化。

放大器（为什么偏偏夜间爆炸）：
1. `main.c:310-324` 夜间 RTC 唤醒 + holiday 缓存命中 → `skip_wifi` → SNTP 不跑 → 无纠错，坏画面保持到天亮。
2. `main.c:232` 每次唤醒用 PCF8563 **无条件覆盖**系统时间——深睡延续时间（白天 SNTP 校准过）本可作为备份却被坏值覆盖。
3. `pcf8563_set_alarm`（`rtc_pcf8563.c:100-103`）匹配 day+hour+min：RTC 时间寄存器复位为 1 日后，日期位失配 → 闹钟永不响 → 若读路径直接判 false 跳过武装，设备只剩按键/充电唤醒（`app_sleep.c` 无 timer 唤醒 fallback）。

## 第二节 目标与非目标（v1）

目标：
1. 无效时间（VL=1 / 年份窗口外 / I2C 失败 / epoch）在任何路径上不得渲染为日历内容、不得写入 `cal_year`/`cal_month`。
2. PCF8563 无效但系统延续时间有效时，**自愈**：用延续时间回写 PCF8563（清 VL），后续闹钟/渲染恢复正常。
3. 时间完全未知（epoch）时：日历跳过本次刷新（EPD 保持旧画面），timer 唤醒 +5min 重试（≤3 次，`[ASSUMPTION] 2`），全程不连网。
4. boot 诊断日志：VL 位 + 7 寄存器原始 hex + 判定结果，供硬件根因定位（第〇节 [ASSUMPTION] 1 的证据通道）。

## 第三节 架构设计（v1）

### 3.1 判定原语（bsp_peripherals）[v2]

`pcf8563_get_time` 语义升级：读成功后检查 `buf[0] bit7`（VL）与年份窗口（2020–2099），任一失败 → 返回 false 且 **out 置零**（不输出坏 tm）。失败保持 bool 二态——I2C_FAIL 与 INVALID 在两个调用点（`main.c:232`、`app_sleep.c:134`）的处置完全相同，原因只进诊断日志（B1-2：拒绝三态 API，避免过度设计）。新增 `pcf8563_get_raw(uint8_t buf[7])` 供诊断打印。年份窗口按语义分立（B1-3）：时间可信窗 `TIME_PLAUSIBLE_YEAR_MIN=2020`（共享下限，bsp 头文件定义，main/ui 引用）/ `TIME_PLAUSIBLE_YEAR_MAX=2099`（PCF8563 两位年硬件上限）；日历导航窗保持既有 2020–2050（`calendar_page.c:234`），上限不合并。诊断日志内容（B1-7）：唤醒原因 + VL 位 + 7 寄存器原始 hex + 判定结果 + 重试计数值。

### 3.2 启动信任链（main.c）

```
read RTC →
  OK        → settimeofday（现状行为，逐字段不变）
  INVALID   → 若系统延续时间有效(year≥2020)：pcf8563_set_time(延续值) 自愈 + 诊断日志
              否则：保持 epoch，置「时间未知」状态
  I2C_FAIL  → 同 INVALID 的 fallback 分支（现状仅打日志）
```
「时间未知」判定不引入全局新状态机：即 `localtime(now).tm_year+1900 < TIME_PLAUSIBLE_YEAR_MIN`，各消费点按需自检（与 `clock.c:89` 惯例同构，不新增概念）。


### 3.3 渲染守卫（ui/calendar_page + widget）

- `calendar_page_init`：today 取值后校验年份；无效时 today 保持 0，NVS 回退分支不落到 today（保持上次有效 `cal_year/cal_month`，NVS 也没有时显示占位「时间未同步，按任意键联网校时」）。
- `calendar_page_handle_input` UP/DOWN：写 NVS 前校验 `r->year` 窗口（防固化坏月）。
- `refresh_almanac_data`：today 无效时不进入老黄历（BOOT click 返回 false，不消耗 15s 刷新——沿用「handle_input true 必须绑定画面实际变化」既有规律）。
- `APP_EVENT_TIME_SYNC`（application.c:585-603）不动：SNTP 校正后跳回当月的自愈路径原样保留（成功判据 3）。
- [v3]（C-1）跳过刷新的静默性补偿：时间无效期间不渲染、EPD 保持旧画面是物理取舍（不刷=不耗电=无提示）；但下一次成功渲染时，若上次日历服务因时间无效被跳过，footer 陈旧度文案升级为「时间未同步」而非仅「更新于 …」（复用 `cal_fresh` 旁路一个 `cal_time_invalid` NVS 标志：跳过时置位、成功渲染后清除）。用户由此可区分故障与正常陈旧。
- [v3]（C-2，记录不修）时间无效时老黄历/翻月按键返回 false，观感为“按键失灵”：接受——一次 15s 全刷换取无实际动作的提示不成立；随 C-1 的 footer 联动，下次成功渲染后用户获得解释。

### 3.4 睡眠 fallback（app_sleep.c）[v2]

- **顺序约束（B1-6）**：fallback 用系统时间计算闹钟目标前，若 PCF8563 判定无效而系统时间有效，必须**先自愈回写（3.2 路径）再武装闹钟**——闹钟 day 位匹配的是 PCF8563 自身计数基准，基准仍为 2000 年时目标日期永失配、闹钟不响。boot 顺序天然满足（main.c 自愈早于任何睡眠武装），但实现不得把自愈挪出 boot 路径。
- 武装路径 `pcf8563_get_time` 失败时：改用系统时间（`localtime`）计算闹钟目标——系统时间有效（延续值已自愈回 PCF8563 的场景 3.2 已覆盖）则照常 `set_alarm`（**勿复制 500ms settle 忙等**，B1-5：timer 分支无此需求）；系统时间也无效（epoch）→ **不武装 RTC alarm**，改 `esp_sleep_enable_timer_wakeup(5min)`（与 ext1 按键/充电唤醒 OR 共存，唤醒原因=TIMER）。
- 重试计数：`RTC_NOINIT_ATTR uint32_t s_time_retry_count` + magic，timer 唤醒且时间仍无效时 +1，≥3 后不再 timer 唤醒（等按键/充电自然唤醒）；任一时间有效路径清零。计数逻辑放 `page_runtime.c`（纯逻辑、host 可测，与 `s_served_day` 同构）。

## 第四节 需求→方案映射表（v1）

| v0 条目 | 响应 |
|---|---|
| 范围内 1 时间有效性判定 | 3.1 |
| 范围内 2 settimeofday 前校验 | 3.2 |
| 范围内 3 日历渲染拒绝/占位 | 3.3 |
| 范围内 4 唤醒跳过刷新+重试 | 3.4 |
| 范围内 5 NVS 污染防护 | 3.3（读写双侧） |
| 范围内 6 诊断日志 | 3.1 get_raw + 3.2 日志点 |
| 判据 1/4 | 3.1+3.3，host 单测边界 1970/2000/2019/2020/2050 |
| 判据 2 | 3.4 timer 唤醒路径 |
| 判据 3 | 3.3 TIME_SYNC 不动 + 回归项 |
| 判据 5 | 3.1/3.2 日志 |
| 判据 6 | 既有门禁（tests/run_tests.sh + idf.py build） |
| 硬约束：不连网/不耗刷新 | 3.3 占位不进刷新、3.4 重试无网络 |

## 第五节 特判映射表（v1）

| 特判 | 去处 | 阶段 |
|---|---|---|
| `calendar_page.c:234` 内联年份校验 | 下限收敛共享 `TIME_PLAUSIBLE_YEAR_MIN`；导航窗上限 2050 保持不动（B1-3 语义分立）[v2] | P1 |
| `clock.c:89` 既有 `<2020` 魔数 | 换用共享常量（行为不变） | P1 |
| main.c RTC 读失败 else 分支 | 并入 3.2 INVALID fallback 统一处理 | P1 |
| `s_served_day` RTC_NOINIT+magic 模式 | 重试计数复用同构实现 | P2 |

## 第六节 分阶段计划（v1）

### P1 判定与守卫（独立可合入）
- `rtc_pcf8563.c`：get_time 可信性校验（bool 二态，B1-2）+ `pcf8563_get_raw`；`main.c:231-242` 信任链改造（含自愈回写）。[v2]
- `calendar_page.c`/`widget_calendar` 渲染与 NVS 双侧守卫；`clock.c` 常量收敛。
- host 单测：判定边界（VL 位、1970/2000/2019/2020/2050/2099）、calendar init 占位分支、NVS 拒写。[v2]
- 验收：判据 1/4/5/6；上板模拟 RTC 复位（断 RTC 电池或短接）→ 日历占位 + 日志证据。
- 用户手册核对：`docs/user-manual.md` 中日历页/夜间刷新相关描述与新行为（时间无效跳过+占位）无漂移。[v3]

### P2 睡眠 fallback 与重试（依赖 P1 的 bool 判定）[v2]
- `app_sleep.c` get_time 失败 fallback（系统时间 → timer 唤醒）；重试计数入 `page_runtime.c`。
- [v4]（D3/O-2 批准）`set_alarm` I2C 失败路径同样落 timer 保底（与 epoch 分支同机制，else 分支级成本），消除"武装失败即睡死"整类风险。
- host 单测：重试计数边界（0→3→放弃→清零）、timer 路径选择。
- 验收：判据 2；上板断 RTC 后夜间不睡死（timer 唤醒日志）；[v4] 拔 RTC 供电模拟 set_alarm 失败 → timer 唤醒路径覆盖。

### P3 诊断与回归收口（依赖 P1/P2 上板）
- 一周诊断日志观察（[ASSUMPTION] 1 复核点：VL 是否反复置位 → 触发硬件专项与否）。
- 回归：白天 SNTP 后跳月（判据 3）、夜间正常 MIDNIGHT 刷新不劣化、12 host 套件全绿、build 零 warning。

## 第七节 并发与恢复分析（v1）

- `app_sync.c:157` SNTP 回调写 PCF8563 与 `app_sleep.c` 读/武装同属主任务+SNTP 回调两上下文：现状即如此（I2C mutex 保护总线），本次不改变并发结构，仅 `set_time` 多一个自愈调用点（主任务 boot 路径，与 SNTP 回调不同时——boot 时 SNTP 未启动）。
- 恢复路径矩阵：冷启动(epoch)→占位+重试；深睡唤醒(RTC 坏+延续好)→自愈+正常；深睡唤醒(双坏)→占位+timer 重试；白天任意→SNTP 校正（既有）。四路径终点一致：时间恢复后自动回到正确月份（TIME_SYNC / init 校验）。
- 重试计数 RTC_NOINIT：断电重启丢 magic → 计数归零重试——可接受（最坏 3 次×5min）。
- [v2]（B1-4 边界）自愈回写 `pcf8563_set_time` 为 7 次独立 I2C 写、非原子——与既有 SNTP 回调写同病；若写中途掉电撕裂，下次启动自愈幂等收敛，不额外加锁或读改写协议。

## 第八节 风险表（v1）

| 风险 | 概率/影响 | 缓解 |
|---|---|---|
| VL 位误报（PCF8563 首次 SNTP 前正常上电即置位） | 中/低（只影响首启动，SNTP 后清零） | 自愈路径用系统延续时间回写即清；首启动占位可接受 |
| timer+ext1 双唤醒源兼容性 | 低/高（睡死或秒醒） | ESP32-S3 官方支持 timer+ext1 并存；上板 P2 验证唤醒原因日志 |
| 延续时间漂移跨午夜误判日期 | 低/中（分钟级漂移 vs 00:01 边界+catch_up 机制） | 既有 catch_up +5min 已兜住小偏差；不新增机制 |
| NVS 已被污染的存量设备 | 高/低（本机即可能是） | P1 的 init 校验拒绝路径即治愈（回退占位→SNTP→TIME_SYNC 跳正） |

## 第九节 测试与验收（v1）

- host 新增 `test_time_validity.c`（bool 判定、重试计数、calendar 守卫边界）；纳入 run_tests.sh。[v2]
- 上板脚本化场景：① 拔 RTC 电池冷启动 → 占位+日志；② 恢复电池+连网 → SNTP 校正跳回当月；③ 夜间模拟 VL 置位入睡 → timer 唤醒 ≤3 次 → 白天自然恢复。
- 回归：正常 MIDNIGHT 夜刷、白天 TIME_SYNC 跳月、时钟页 `--:--` 行为不变。

## 附录 A：盘点中发现的既有问题（v1）

1. `port-refinement-plan.md` F2.1（RTC I2C 写返回值恒 true）已在此前重构修复（`rtc_pcf8563.c:66-74` 现已累积 err）——仅记录，无需动作。
2. `app_sleep.c` 无任何 timer 唤醒保底：RTC alarm 武装失败即只能按键唤醒。本方案 3.4 顺带补上（epoch 分支）；其余武装失败路径（set_alarm I2C 失败）仍无保底——记机会项，PO 决策。

## 附录 B：明确不做（v1）

- 夜间时间无效时连网 NTP 纠错（违反省电硬约束，PO 轮复核）。
- 其余 18 页时间守卫（clock.c 已有惯例，日历外无月视图语义）。
- PCF8563 闹钟日期位屏蔽改造（保持 day+hour+min 匹配，无效场景走 timer fallback 而非改闹钟语义）。
- 硬件供电轨/后备电池处置（等 P3 诊断证据）。

## 附录 C：机会项（C 轮提交，PO 决策）[v3]

- O-1 重试耗尽错误页：3 次 timer 重试后仍无效时，渲染一张极简「时间异常，联网后自动恢复」页面（代价=1 次 ~15s 全刷）。C 轮评估：换明确提示 vs 打破“不耗刷新预算”约束，倾向不做，留 PO。→ [v4] **否决**：夜间用户大概率不可见，15s 全刷违反刷新预算约束，C-1 footer 联动已提供事后可解释性。
- O-2 `app_sleep.c` set_alarm I2C 失败路径的 timer 保底（附录 A2 扩展）。→ [v4] **批准并入 P2**（D3）。
- O-3 footer 陈旧度指示与 `cal_time_invalid` 联动的 UI 细化（若 C-1 的 MVP 联动不够醒目）。→ [v4] **否决**：待 P1 上板验证 C-1 MVP 效果，确不足再立项。


## 第十节 决策记录（v4 终态）

### D1 守卫架构与批次：三层守卫（bool 判定 / 信任链+自愈 / 渲染与睡眠 fallback）按 P1→P2→P3 交付（P1 批次）
- 规则/内容: P1=判定+信任链+渲染守卫（价值拐点，消灭荒谬日期）；P2=睡眠 fallback+重试+set_alarm 保底；P3=一周诊断观察+回归收口。每阶段独立可合入。
- 理由: v2 塌缩三态后概念数=1（TIME_PLAUSIBLE_YEAR_MIN）+1（重试计数）；被否选项：三态 API（过度设计）、全局时间状态机（与 clock.c 既有惯例冲突）。
- 实现落点: `rtc_pcf8563.c` / `main.c:231-242` / `calendar_page.c` / `app_sleep.c` / `page_runtime.c`。
- 回退开关: 各层守卫独立提交；判定层回退=`pcf8563_get_time` 恢复无条件 true（单函数）。

### D2 判据 4 口径修正：污染 NVS 回退到「上次有效导航位置或占位」（P1 批次）
- 规则/内容: 时间恢复前不存在"有效时间"可退；回退链=NVS 有效值 > 占位。原 v0 措辞"回退到有效时间"作废。
- 理由: 3.3 设计与 v0 判据字面冲突，PO 修订判据使其与设计语义一致（防团队被迫实现不可达行为）。
- 实现落点: 第〇节判据 4（本节修订）；`calendar_page.c:233-244`。
- 回退开关: 不适用（口径修正）。

### D3 机会项裁决：O-2 批准并入 P2；O-1、O-3 否决（P2 批次）
- 规则/内容: 见附录 C 各条 [v4] 标注。
- 理由: O-2 成本一个 else 分支且消除整类睡死风险；O-1 违反刷新预算且夜间不可见；O-3 证据不足先观察。
- 实现落点: `app_sleep.c`（O-2）。
- 回退开关: O-2 单独提交，revert 即恢复"武装失败仅按键唤醒"现状。

### D4 假设维持：重试上限 3 次；硬件根因走诊断路径（P3 复核）
- 规则/内容: [ASSUMPTION] 2 维持 3×5min（重试仅在"双无效"时递增，自愈路径每次唤醒都会先尝试）；[ASSUMPTION] 1 维持固件防御+诊断日志，P3 一周内 VL 反复置位 → 硬件专项立项。
- 理由: 持续性根因重试无意义，瞬时根因首次自愈即恢复；硬件处置成本高且证据未足。
- 实现落点: `page_runtime.c` 重试计数；boot 诊断日志。
- 回退开关: 重试上限改常量即可。

### D5 发布门禁（一票否决，按序）
1. 历史事故回归（第一优先）：无效时间渲染事件=0——上板 mock RTC 复位值（2000-01-01）冷启动+深睡唤醒两条路径，日历均不出现 1月1日（判据 1，日志+目视审计）
2. v0 成功判据 1–6 逐条过（判据 4 按 D2 修正口径）
3. 核心价值指标：夜间时间无效路径刷新次数=0、连网次数=0（日志 grep "skipping Wi-Fi" + 无 SNTP start）
4. 既有功能不劣化：正常 MIDNIGHT 夜刷、白天 TIME_SYNC 跳月、时钟页 `--:--`、gallery 幻灯片唤醒、skip_wifi 省电行为
5. 构建与测试门禁：`bash tests/run_tests.sh` 全绿（含新增 test_time_validity）+ `idf.py build` 零 warning

### D6 立修项：无
- 本次故障修复即方案本体；附录 A1 为已修复记录，无其它与方案无关的即时可感知 bug。