# 页面运行时框架重构计划：前台页面 / 后台服务 / 睡眠参数页面化

> 日期：2026-08-25
> 状态：计划（未实施）
> 修订：v2 —— 内核/嵌入式评审（P0 时序、兴趣重拉循环、idle_ms 死空间、并发上下文、refcount、wake_align、手动睡眠）；v3 —— 产品/UI-UX 评审（传图中途切页中断、陈旧度指示、夜间闪烁价值定位、sync_interval 语义碰撞、手册过时、LAN HTTP/充电机会项）；v4 —— Product Owner 决策（第十节 D1–D10：发布切分 R1/R2、机会项批准、发布门禁、立修项）；v5 —— 按 requirement-to-plan Skill 追溯补全（第〇节 v0 需求实例·追溯实例化 + 第十一节 B2 安全镜头评审 + D11）；v6 —— 用户更正主场景（床头/冰箱贴、仅耗尽后充电）→ PO 级联：v0 主场景修订、D4 理由重写（结论维持）、新增 D12 充电插入唤醒。

---

## 第〇节 需求实例（v0）[追溯实例化]

> **补录说明**：原始对话从三问诊断直接进入"请出架构方案"，跳过了需求实例化——形态级决策被隐式埋进 v1，直到 v4 才倒推。本节于 v5 按 `requirement-to-plan` Skill 入口 B 规则追溯补录：从文档+代码反推隐含需求，无法反推的经 ask 与用户确认（2026-08-25，记录见下）。评审期本节仅 PO 可修订。

- **原始诉求**（用户原话，不改写）：
  1. "日历页面进入休眠后多久会自动唤醒并更新一次画面？从用户体验角度是否可以优化？wifi快速重连是否已经实现？"
  2. "请作为架构师针对'唤醒期拉取与页面无关的数据'这个问题……是否可以通过一个框架，将页面分为前台页面和后台服务……将定时器以及进入睡眠触发时间、唤醒时间等都与页面关联，这些系统参数都使用处于前台的页面的，如果页面没有配置才用系统默认的，页面切换时将处于非前台状态的页面冻结，关联的资源（如定时器、唤醒后需要做的动作等）都释放。然后编写一份修改计划到 docs 目录下。"
- **问题陈述**：数据拉取、睡眠/唤醒参数、后台服务生命周期均与页面解耦，靠 11 处手写特判粘合（§四），导致唤醒期拉取无关数据、无效刷屏与耗电。
- **主场景**：**床头 + 冰箱贴——壁挂/吸附式电池供电静态显示，低频交互（瞥一眼），仅电量耗尽后才充电**。夜间静默零闪烁关键；冰箱金属门旁 RF 环境弱于桌面（v2 已设计的拉取失败退避 30s→10min 天然兼容，无需新工作）。〔v6 用户更正，取代 v5 ask 的"混合：桌面+床头"误答；级联见 D4/D12〕
- **范围内**：数据兴趣页面化（P1）、睡眠/唤醒参数页面化（P2）、后台服务生命周期（P3）、设置项与文档同步、剩余页面迁移（P4）、LAN HTTP 认证加固（R2，D11）。
- **范围外**：附录 B 全部（帧哈希跳刷、FAST_RC、vtable 扩展、页面级任务、页面级 idle 超时、服务 refcount）+ BOOT 唤醒 ~20s 延迟（另立项）。
- **成功判据**（可观测、含口径；**无续航硬指标，用代理指标**〔ask 确认〕）：① 日历页夜间（22:00–07:00）EPD 全刷 ≤1 次（00:01 日界刷新属预期）；② 缓存命中时唤醒窗口 <5s（串口 esp_startup→refresh idle 时间戳差）；③ 唤醒次数 ≤2/天；④ 幻灯片 5min 时间线回归；⑤ 每版 BOOT+RTC 双唤醒源实测。测量方法均存在（串口日志/LED+逻辑分析仪）。
- **硬约束**：ESP32-S3 深睡清 RAM（唤醒=冷启动，状态须 RTC_NOINIT/NVS 恢复）；4 色 EPD 全刷 ~15–20s、无法部分刷新状态栏；PCF8563 闹钟分钟级（匹配分/时/日）；三按键交互不变；LAN/AP 传图既有用户流程不劣化。
- **[ASSUMPTION] 未决项**：无遗留——主场景、B2 处置、续航口径三项均已于 v5 经 ask 确认。
- **已知事实清单**：见 §一（file:line 级证据 11 处特判）与 §3.5/§3.7。
- **ask 问答记录**（2026-08-25，追溯实例化）：
  | 问 | 答 |
  |---|---|
  | 主使用场景？ | ~~混合：桌面+床头~~ → **床头、冰箱贴，基本仅耗尽后充电**（v6 用户更正原答） |
  | B2 安全发现处置？ | 纳入 R2（recommended 采纳） |
  | 续航验收口径？ | 无硬指标，代理指标即可（recommended 采纳）〔v6 注：场景更正后代理指标权重上升——无外接电源，唤醒次数即电池寿命的主导项〕 |

---
## 0. TL;DR

引入一个**声明式页面运行时策略**（`page_runtime_policy_t`，flash 常量，随 `PAGE_REGISTER` 注册）+ 一个**单一事实源仲裁器**（`page_runtime`）：

1. 每个页面声明自己的数据兴趣（拉什么）、唤醒间隔（多久醒）、唤醒对齐（对准内容变化点）、唤醒是否需要网络、绑定的后台服务。
2. **前台页面的策略生效，未配置的字段回退系统默认**（默认策略 = 当前全局行为，保证未迁移页面零行为变化）。
3. 页面切换时，旧页面被**冻结**：其数据兴趣标志清除、页内定时器停止、页面所有的后台服务释放；`exit()` 生命周期钩子已有但语义未使用，本次补全契约。
4. 现有 11 处手写特判（gallery 幻灯片、calendar 节假日可见性、AP 传输服务挡睡眠、设置菜单定时器重启等）全部收敛进框架，删除。

预期收益（以日历页为例）：唤醒窗口内只做节假日缓存校验（NVS 命中则零网络请求），唤醒间隔对准 00:01 而非固定 30 分钟，46/47 次无效全屏刷新中的绝大多数被消除。

**[v3] 用户价值映射（PM 视角，防阶段价值判误）**：用户可感知的头号收益是**消除夜间每 30 分钟一次的 15~20 秒全屏闪烁**（user-manual.md:20，床头/冰箱贴场景光/声污染——v6 场景确认后该收益即设备的核心价值）——该收益**完全落在 P2**（P1 只减网络请求，唤醒后仍整屏刷新，用户感知有限）；P1 是 P2 的地基；P3 是运维卫生（用户优先级最低，可让路）；P4 含设置项 UI 与文档同步。价值拐点在 P2，排期时不可降级。

| 阶段 | 用户可感知结果 |
|---|---|
| P1 | 夜间唤醒时网络请求减少（间接：更快回到睡眠、更少路由器日志）；屏幕行为不变 |
| P2 | 日历页夜间零闪烁；数据随日界更新；失败时可见"更新于"时间戳 |
| P3 | 传图模式进出自动启停，无后台残留；切页不再误停传图（升级规则） |
| P4 | 设置菜单可调刷新间隔；手册与行为一致 |

---

## 一、现状与问题（代码证据）

### 1.1 数据拉取与页面无关

WiFi 一连上，三个数据集**无条件**全部拉取，不管前台是哪个页面：

```c
// application.c:151-168  process_wifi_event(WIFI_EVENT_CONNECTED)
s_app.need_coding_plan_refresh = true;
s_app.need_weather_fetch = true;
s_app.need_holiday_fetch = true;

// application.c:357-372  application_notify_wifi_if_connected —— 同样的三连
```

周期刷新也是全局的：

```c
// application.c:640-643  application_run 1s 循环
if (++s_cp_refresh_counter >= APP_CODING_PLAN_REFRESH_SECONDS) {  // 30 分钟，application_internal.h:121
    app_sync_refresh_coding_plan();   // 不管前台是什么页面
}
```

数据落地后的可见性守卫同样是手写特判（只此一处）：

```c
// application.c:630-637  节假日拉取成功后
if (ui_manager_get_current_page(s_app.ui_mgr) == UI_PAGE_CALENDAR) {
    ui_manager_request_active_page_refresh(s_app.ui_mgr);
}
```

### 1.2 睡眠/唤醒参数散落在 4 个文件里做同一件事

"用哪个间隔入睡/唤醒"的判断逻辑重复了三遍，且都是对 gallery 的硬编码特判：

| 位置 | 逻辑 |
|---|---|
| `app_sleep.c:112-114` | gallery 前台 → 唤醒间隔用幻灯片间隔 |
| `app_sleep.c:223-230` | gallery 前台 → 跳过 sync sleep 定时器 |
| `main.c:286-305` | RTC 唤醒 + gallery + 幻灯片开启 → 跳过 WiFi 连接 |
| `application.c:301-329` | RTC 唤醒 + gallery → 快速路径切相册翻页 |

非 gallery 页面没有这样的通道——它们的唤醒间隔固定读全局 `sync/sync_interval`（默认 30 分钟，`app_sleep.c:115-122`），且设置菜单**没有**修改入口（`app_settings_menu.c:194-298` 无此项）。

### 1.3 后台服务与页面的关系靠手写启停

LAN HTTP / AP 传输服务的启停散布在 4 个事件分支里：

- `application.c:486-489`（BOOT 长按退出 AP 模式 → stop）
- `application.c:491-508`（gallery 长按开/关 LAN HTTP；wifi 页长按进 AP 模式）
- `application.c:176-178`（WiFi 断开 → 停 LAN HTTP）
- `app_settings_menu.c:122-176`（设置菜单开关 LAN HTTP）

"服务运行中要挡住睡眠"的知识则写在睡眠模块里：

```c
// app_sleep.c:63-67 / 210-217 / application.c:651
if (app_sleep_is_local_http_running()) { /* 推迟入睡 */ }
```

### 1.4 页面生命周期钩子已有但语义空洞

`page_renderer_ops_t` 有 `init/enter/exit`（page_renderer.h:13-16），`ui_manager_switch_page` 也正确调用了 exit→enter（ui_manager.c:303-304）。但**没有任何页面在 `exit` 里释放资源**——钩子存在、契约缺失。另外 `ui_manager_set_current_page_without_render`（ui_manager.c:319-329）不发 `page_switch_cb`，是一条绕过切换通知的暗道。

### 1.5 现成的接入缝隙（本次不改 vtable 的依据）

- `page_entry_t`（page_registry.h:39-47）用**位置初始化**（`PAGE_REGISTER` 宏，page_registry.h:56-61）——结构体**尾部追加字段**对未迁移页面零影响（缺省为零/NULL）。
- `ui_manager_set_page_switch_callback`（ui_manager.c:894-900）已实现且**当前无人注册**——现成的切换监听缝隙。
- 数据兴趣目前就是 `application_t` 里三个 bool（application_internal.h:84-86）——替换为 bitmask 不影响外部。
- `holiday_fetcher` 已示范 `#ifdef ESP_PLATFORM` 双态编译模式（holiday_fetcher.c:238-247, 304-307）——框架核心沿用，保证 host 可测。

---

## 二、目标与非目标

### 目标

1. **G1 数据兴趣页面化**：唤醒/连网后只拉前台页面声明的数据；周期刷新同样按前台页面策略。
2. **G2 睡眠参数页面化**：RTC 唤醒间隔、唤醒对齐点、唤醒是否需要网络，均取前台页面策略，未配置回退系统默认。**[v2] 入睡触发超时不再页面化**（idle_ms 死策略空间，见 3.2），保持系统级 30s 交互超时。
3. **G3 后台服务页面化**：服务（LAN HTTP、AP 传输）带所有权语义——页面所有（切页即停）或用户所有（跨页存活，直到用户关闭）。
4. **G4 冻结语义**：页面离开前台时，清除其数据兴趣标志、停止其定时器、释放其页面所有的服务；数据回调统一用 `page_runtime_is_page_active()` 守卫。
5. **G5 删除全部 9 处特判**（见第五节映射表），未迁移页面行为不变（默认策略 = 现状）。

### 非目标

- 不改 `page_renderer_ops_t` 槽位（策略是数据不是行为；避免 19 个页面全部重写）。
- 不做帧哈希比对跳刷、WiFi FAST_RC 快速重连（独立优化，见 docs/port-refinement-plan.md:216；帧比对可后续叠加在 G2 的唤醒对齐之上）。
- 不改深睡唤醒 = 冷启动的芯片事实（见 docs/sleep-wake-recovery-analysis.md），框架所有运行时状态要么是 flash 常量、要么经 RTC_NOINIT/NVS 恢复。
- 不引入动态内存/任务创建：策略为 flash 常量，仲裁器为静态单例。

---

## 三、架构设计

### 3.1 三层模型

```mermaid
flowchart TB
    subgraph SYS["系统层（常驻，与页面无关）"]
        CLOCK["1s 时钟泵<br>main.c:386"]
        SNTP["SNTP 校时"]
        PROT["protocol WebSocket+音频<br>语音唤醒任意页可用"]
        STATUS["状态栏/电量"]
    end

    subgraph RT["page_runtime 仲裁器（单一事实源）<br>main/app_page_runtime.c + components/ui/page_runtime.c"]
        POL["策略查询<br>page_runtime_policy()"]
        INTR["数据兴趣位图"]
        TIM["睡眠/唤醒参数仲裁"]
        SVC["服务注册表<br>（所有权状态机）"]

    subgraph PAGES["页面层"]
        FG["前台页面<br>policy 生效"]
        BG["后台页面（冻结）<br>interests 清零 / 定时器停 / 页面所有服务释放"]
    end

    subgraph SVCS["后台服务"]
        LAN["LAN HTTP 服务<br>USER 所有"]
        APT["AP 传输服务<br>AP_TRANSFER 页所有"]
    end

    FG -->|"enter()"| RT
    BG -->|"exit()"| RT
    RT -->|"唤醒间隔/对齐"| SLEEP["app_sleep.c"]
    RT -->|"interests"| SYNC["application_run 拉取循环"]
    RT -->|"acquire/release"| SVCS
    SVC -->|"busy 源"| SM["sleep_manager"]
    SYS -.->|"不参与页面冻结"| RT
```

关键分层判断：

- **策略数据与策略查询**放 `components/ui/`（页面元数据的自然归属，host 可测纯逻辑）。
- **仲裁器 ESP 胶水**放 `main/app_page_runtime.c`（它要调 `app_sleep_*`、`wifi_manager_*`、`ap_transfer_server_*`——这些是应用层依赖，组件层不得反向依赖）。
- **protocol/WebSocket/音频通道维持系统级**：语音唤醒从任意页面可用（application.c:155-156 在连接时启动），绑到 chat 页会破坏这一点。这是"系统服务"与"页面服务"必须分层存在的实证。

### 3.2 声明式策略：`page_runtime_policy_t`

```c
/* components/ui/include/page_runtime.h */
typedef enum {
    PAGE_WAKE_ALIGN_NONE = 0,     /* 入睡时刻 + interval */
    PAGE_WAKE_ALIGN_MIDNIGHT,     /* 对准下次 00:01（日历：内容只在日边界变化） */
} page_wake_align_t;

/* 数据兴趣位图（替换 application_t 三个 bool） */
typedef enum {
    PAGE_DATA_NONE        = 0,
    PAGE_DATA_WEATHER     = 1u << 0,
    PAGE_DATA_CODING_PLAN = 1u << 1,
    PAGE_DATA_HOLIDAY     = 1u << 2,
    PAGE_DATA_SNTP        = 1u << 3,
    /* ...按需扩展 */
} page_data_interest_t;

typedef struct {
    uint16_t wake_interval_min;   /* 0 = 回退系统默认 sync_interval        */
    uint8_t  wake_align;          /* page_wake_align_t；设置时完全支配，    */
                                  /* wake_interval_min 仅在 ALIGN_NONE 时用 */
    uint32_t data_interests;      /* PAGE_DATA_* 掩码                       */
    bool     needs_network_on_wake;/* false = 唤醒跳过 WiFi（gallery 泛化） */
    uint32_t services;            /* APP_SVC_* 掩码，页面所有               */
    uint16_t periodic_refresh_s;  /* 0 = 无周期刷新（替换全局 30min 计数器）*/
} page_runtime_policy_t;

/* [v2] 删除了 idle_ms 字段：sm_kick() 单调只升不降（sleep_manager.c:24-33），
 * 且每次页面切换事件本身先吃一个 30s user_interaction kick
 * （application.c:431-432），任何 <=30s 的页面空闲超时永远是 no-op——
 * 死策略空间。系统 30s 交互超时保留。若未来确需更短超时，须先给
 * sleep_manager 增加 sm_deadline_lower() 原语（YAGNI，暂不做）。 */

/* 哨兵：显式表达"继承系统默认"，与全零区分 */
#define PAGE_POLICY_INHERIT ((const page_runtime_policy_t *)0)
```

注册方式——扩展 `page_entry_t` 尾部字段 + 新宏（旧宏不动，18 个页面零改动）：

```c
/* page_registry.h —— page_entry_t 尾部追加 */
typedef struct {
    /* ...现有 7 个字段不动（page_registry.h:39-47）... */
    const page_runtime_policy_t *runtime_policy;   /* NULL = 系统默认 */
} page_entry_t;

/* 新增 opt-in 宏；PAGE_REGISTER 保持原样 */
#define PAGE_REGISTER_WITH_RUNTIME(id, name, icon, quick, order_val, ops_ptr, inst_ptr, policy_ptr) \
    static const page_entry_t _page_entry_##id = {..., (policy_ptr)};                               \
    void __attribute__((constructor(200))) _page_register_##id(void) {...}
```

**日历页示例**（迁移后一行注册）：

```c
static const page_runtime_policy_t s_cal_policy = {
    .wake_interval_min     = 0,                        /* ALIGN 支配时忽略        */
    .wake_align            = PAGE_WAKE_ALIGN_MIDNIGHT, /* 对准下次 00:01          */
    .data_interests        = PAGE_DATA_HOLIDAY | PAGE_DATA_SNTP,
    .needs_network_on_wake = true,   /* 节假日 NVS 未命中时才需要；见 3.6 */
    .services              = 0,
    .periodic_refresh_s    = 0,       /* 内容只在日边界变化，由唤醒对齐覆盖 */
};
PAGE_REGISTER_WITH_RUNTIME(UI_PAGE_CALENDAR, "日历", ..., &s_cal_policy);
```

**默认策略 = 当前全局行为**（weather + coding_plan + holiday + SNTP，网络唤醒，30 分钟）——这是"未配置才用系统默认"的具体化，也是零回归迁移的保证。

### 3.3 仲裁器：`page_runtime`

```c
const page_runtime_policy_t *page_runtime_policy(ui_page_id_t page);   /* 查表+默认回退 */
uint32_t page_runtime_effective_interests(ui_page_id_t page);          /* 策略声明的兴趣 */
int      page_runtime_effective_wake_interval_min(ui_page_id_t page);
bool     page_runtime_effective_network_on_wake(ui_page_id_t page);
bool     page_runtime_is_page_active(ui_page_id_t page);               /* 冻结守卫     */

/* main/application_internal.h —— ESP 胶水 */
void     page_runtime_on_page_entered(ui_page_id_t page);  /* 切换/启动时调用 */
void     page_runtime_on_page_exited(ui_page_id_t page);
void     page_runtime_init(void);                          /* 紧跟 ui_manager_init */
uint32_t page_runtime_pending_interests(void);             /* [v2] 见下 */
void     page_runtime_set_pending(uint32_t bits);          /* [v2] 置位 pending      */
```

**[v2] declared vs pending 状态机（防重拉死循环）**：`effective_interests()` 是策略的
静态函数——若 1s 拉取循环直接消费它，位永不清零 → 每秒重拉。必须区分：

- **declared**（策略常量）：该页想要什么，仅用于合成与守卫；
- **pending**（动态 RAM 位图）：还没拿到什么。置位事件 = 页面进入前台 /
  WiFi 连上 / 周期刷新到期 / 落地后发现缓存过期；清零事件 = **派发拉取时**。
  拉取循环只消费 `pending & declared`（pending 中不属于前台页声明的位在切页时清除）。


职责边界（单向依赖：main → ui）：

```
application_run / app_sleep / app_sync   ──读──▶  page_runtime（main 胶水）
page_runtime（main 胶水）                 ──读──▶  page_runtime（ui 纯核心）+ page_registry
ui 纯核心                                ──读──▶  page_entry_t（const，flash）
```

### 3.4 前台/后台与冻结语义

**定义**：页面处于前台 ⟺ `ui_manager_get_current_page() == page`。深睡唤醒后由 `RTC_NOINIT` 恢复的 `s_rtc_last_page`（ui_manager.c:50-52）决定首个前台页——框架无需新增恢复机制。

**冻结的精确含义**（页面离开前台瞬间执行，顺序固定）：

1. **清 pending 位**：从 pending 位图中清除该页声明的位（declared 是 flash 常量无需清）→ 在途拉取照常完成，落地回调发现页面非前台，不触发 EPD 刷新（守卫见下）。
2. **复位周期计数器**：`periodic_refresh_s` 对应的计数器清零（迁移后不再存在全局 `s_cp_refresh_counter`）。
3. **释放页面所有的服务**：`services` 掩码中 owner==PAGE 且 owner_page==该页的服务执行 stop（见 3.7）。

**数据回调统一守卫**（替换 application.c:634-636 的手写特判）：

```c
/* 天气/节假日/coding plan 落地回调里 */
if (page_runtime_is_page_active(UI_PAGE_CALENDAR)) {
    ui_manager_request_active_page_refresh(s_app.ui_mgr);
}
```

**切换通知补全**：`ui_manager_set_current_page_without_render`（ui_manager.c:319-329）当前不发 `page_switch_cb`——P0 中统一两个切换路径都发回调，消除暗道。

### 3.5 睡眠参数仲裁

| 参数 | 现状 | 框架化后 |
|---|---|---|
| 入睡触发超时 | 交互 kick 30s（application.c:432）+ boot kick 10s/30s 硬编码（application.c:349-353） | **[v2] 不页面化**——保持系统级 30s 交互超时 + 10s/30s boot kick（idle_ms 已删，理由见 3.2） |
| 兜底 sleep 定时器 | `app_sleep_arm_sync_timer` 读全局 sync_interval，gallery 特判跳过（app_sleep.c:223-230） | 间隔 = `page_runtime_effective_wake_interval_min(前台页)`；gallery 特判删除（其策略 `wake_interval_min=幻灯片间隔` 由 ui_manager 设置时写入运行时覆盖）。**[v2] 系统默认间隔 RAM 缓存**：现状每次布防都 `settings_open` 读 NVS（app_sleep.c:232）——首读缓存，设置写入时失效 |
| RTC 唤醒闹钟 | `app_sleep_enter_scheduled`：gallery 特判 + 全局间隔（app_sleep.c:111-122） | 同一函数只调 `page_runtime_effective_wake_interval_min()`。**[v2] 语义明确：`wake_align != NONE` 时完全支配（闹钟=对齐点，interval 被忽略）；`ALIGN_NONE` 时闹钟=now+interval**。PCF8563 闹钟匹配 分+时+日（rtc_pcf8563.c:100-103），00:01 日对齐可直接表达 |
| 唤醒是否连网 | main.c:286-305 gallery 幻灯片特判 | `page_runtime_effective_network_on_wake(恢复页)` |

**[v2] MIDNIGHT 边界缺陷与对策**：设对齐点=下次 00:01（严格未来）。缺陷场景：设备在
00:01 后不久入睡且**今天的日界内容尚未服务**（如用户 00:00:30 手动交互后立刻闲置入睡），
则下次闹钟落在**次日** 00:01，页面陈旧近 24h。确定性对策：`RTC_NOINIT uint32_t
s_last_served_day`（ymd 编码 + magic），布防闹钟时若"今日边界已过且未服务"→ 闹钟改设
now+5min 的补刷点，落地渲染成功后写 served_day。约 15 行，随 P2 实现。

**[v2] 运行时覆盖机制（按页数组，非单槽）**：幻灯片间隔是用户运行时可改的（设置菜单），
而 policy 是 flash 常量。`page_runtime` 维护 `uint16_t override_min[UI_PAGE_COUNT]`
（19×2=38B RAM，0=无覆盖）：`ui_manager_set_gallery_slideshow_interval_minutes` 写
GALLERY 槽。深睡唤醒后从 NVS `gallery/slide_min` 重放（main.c:293-298 已读该键）。
单槽 hack 会把 gallery 固化为隐式特例——按页数组消除之。

### 3.6 唤醒流水线（main.c 改造后）

```c
/* main.c —— 替换 282-309 的特判块 */
const ui_page_id_t restore = ui_manager_get_rtc_saved_page();
const bool is_rtc_wakeup = ...;                          /* 现有 EXT1+RTC_INT 判断 */
const bool need_wifi =
    !is_rtc_wakeup ? true                                /* 冷启动：正常连 */
                   : page_runtime_effective_network_on_wake(restore);
if (strlen(ssid) > 0 && need_wifi) {
    wifi_manager_connect(ssid, password);
}
```

日历页的进阶路径（P2 可选）：`needs_network_on_wake` 对日历**动态化**——`holiday_fetcher` 的 NVS 年份缓存命中（holiday_fetcher.c:171-212 的 `load_cache`）且当天非年末时返回 false，实现真正的离线唤醒刷新。这需要在 `page_runtime_effective_network_on_wake` 里给 `PAGE_DATA_HOLIDAY` 页面加一个缓存探测钩子；若不想引入钩子，则退化为静态 true（网络唤醒但 interests 只含 HOLIDAY+SNTP，拉取量也已从 3 个 HTTPS 请求降到 1 个年缓存命中）。

### 3.7 服务注册表与所有权

```c
/* main/app_page_runtime.c */
typedef enum {
    APP_SVC_NONE        = 0,
    APP_SVC_LAN_HTTP    = 1u << 0,   /* ap_transfer_server LAN 模式 */
    APP_SVC_AP_TRANSFER = 1u << 1,   /* ap_transfer_server AP 模式  */
} app_service_id_t;

typedef enum { SVC_OWNER_NONE, SVC_OWNER_PAGE, SVC_OWNER_USER } svc_owner_t;

typedef struct {
    app_service_id_t id;
    const char *name;
    bool (*is_running)(void);
    bool (*start)(void);            /* 需要的上下文经静态单例获取（ip 等） */
    void (*stop)(void);
} app_service_def_t;

/* [v2] 所有权状态机，非引用计数：每服务至多一个 PAGE owner + 一个 USER 布尔，
 * 不存在多持有者场景。refcount 引入泄漏/下溢错误类却零收益。 */
typedef struct {
    svc_owner_t owner_kind;         /* NONE / PAGE / USER                 */
    ui_page_id_t owner_page;        /* owner_kind==PAGE 时有效            */
} app_service_state_t;

/* acquire/release 由主任务串行调用（见第七节并发分析） */
bool page_runtime_service_acquire(app_service_id_t id, svc_owner_t owner, ui_page_id_t page);
void page_runtime_service_release_by_page(ui_page_id_t page);   /* owner_page 匹配才停 */
void page_runtime_service_release_user(app_service_id_t id);
void page_runtime_service_release_all(void);                    /* [v2] 手动睡眠路径用 */
bool page_runtime_service_any_running(void);   /* 替换 app_sleep_is_local_http_running 的职责 */
```

**[v2] 手动睡眠路径必须入表**：`app_sleep_enter_manual`（app_sleep.c:268-312）现状无条件
停所有服务（:274-276）、**不设 RTC 闹钟**（:288-292，BOOT-only 唤醒 → 全量冷启动路径）、
自旋等待 `sm_can_sleep_now` 最多 35s（:282-286）。P3 后该函数改调
`page_runtime_service_release_all()`，保证注册表状态与实际一致（否则"手动睡眠停了
USER 服务但注册表仍记 USER 持有"→ 下次唤醒状态漂移）。

**[v3] 传输中粘性升级规则（防上传中断）**：AP 传输页的 `handle_input` 只吞噬
`BTN_BOOT_CLICK`（ap_transfer_page.c:299-304），而 BOOT **双击**是管理器级快捷键、
任意页面可打开快速切换覆盖层（ui_manager.c:463-464），确认后经
`ui_manager_set_current_page_without_render` 切页（ui_manager.c:151-158）——即**传图中途
用户可以无预警切走页面**。规则：`ap_server_state_cb` 上报 client connected（state 2）至
收图完成/失败（state 5/6，application.c:119-131）之间视为传输进行中，此时 PAGE 所有权
**升级为粘性**（等价 USER），切页不停服；传输结束回落 PAGE 语义；长按 BOOT 显式退出
始终立停（用户意图明确）。同时修复现状"切页后 AP 服务后台残留、耗电、状态栏残留 `*`"
的缺陷。"传图中切页"记入 P3 验收用例。

所有权语义（保持现有用户预期）：

| 服务 | 所有权 | 依据 |
|---|---|---|
| LAN HTTP | **USER** | 设置菜单/gallery 长按开启后，用户切换页面仍期望可访问（现状即如此，application.c:491-508 无切页停机逻辑） |
| AP 传输 | **PAGE** + 传输中粘性升级（见上） | 离页自动停；传输进行中不停（防上传中断） |
| protocol WebSocket/音频 | 系统级，不入表 | 任意页语音唤醒依赖（application.c:155-156） |

---

## 四、现有特判 → 框架映射表（G5 验收清单）

| # | 现有代码 | 框架化去处 | 阶段 |
|---|---|---|---|
| 1 | application.c:165-167（连网后三连拉） | `page_runtime_set_pending(effective_interests(前台页))` | P1 |
| 2 | application.c:367-369（notify_wifi 同三连） | 同上，抽公共函数 | P1 |
| 3 | application.c:640-643（30min 全局 coding plan） | `policy.periodic_refresh_s`（coding_plan 页声明 1800） | P1 |
| 4 | application.c:634-636（节假日落地只刷日历） | `page_runtime_is_page_active()` 守卫推广到全部数据回调 | P1 |
| 5 | app_sleep.c:112-114（gallery 唤醒间隔特判） | `page_runtime_effective_wake_interval_min()` + 运行时覆盖 | P2 |
| 6 | app_sleep.c:223-230（gallery 跳兜底定时器） | 同上（gallery 策略 wake_interval 即幻灯片间隔，无需特判跳过） | P2 |
| 7 | main.c:286-305（幻灯片唤醒跳 WiFi） | `needs_network_on_wake` | P2 |
| 8 | application.c:301-329（RTC 唤醒 gallery 快速路径） | 保留页面专属"唤醒动作"钩子：`policy` 增加 `void (*on_rtc_wake)(ui_page_id_t)` 函数指针，gallery 注册翻页函数 | P2 |
| 9 | app_sleep.c:63-67/210-217 + application.c:651（HTTP 服务挡睡眠） | `page_runtime_service_any_running()` 单点查询 | P3 |
| 10 | app_settings_menu.c:114-119（轮播间隔切换停/重启 sync 定时器） | 覆盖数组写入后统一 `app_sleep_arm_sync_timer()` 重布防（间隔已来自策略+覆盖） | P2 |
| 11 | app_sleep.c:274-276（手动睡眠无条件停服务） | `page_runtime_service_release_all()`，注册表状态同步 | P3 |

其中 #8 的 `on_rtc_wake` 钩子是 policy 中唯一的函数指针——幻灯片翻页（application.c:326 `photo_gallery_select_next`）无法用纯数据表达。放在 flash 常量结构里依然安全（与 vtable 同类）。

---

## 五、页面迁移总表

| 页面 | data_interests | wake_interval_min | network_on_wake | services | 备注 |
|---|---|---|---|---|---|
| CALENDAR | HOLIDAY+SNTP | 0（对齐 MIDNIGHT） | true（P2 可动态化） | — | 试点页 |
| GALLERY | — | 运行时覆盖（幻灯片） | false（幻灯片开启时） | — | on_rtc_wake=翻页 |
| WEATHER / WEATHER_DETAIL | WEATHER+SNTP | 30 | true | — | |
| CODING_PLAN | CODING_PLAN+SNTP | 0 | true | — | periodic_refresh_s=1800 |
| CHAT | SNTP | 0 | true | — | ws 为系统服务 |
| AP_TRANSFER | — | — | true | AP_TRANSFER（PAGE 所有） | 自动启停 |
| NEWS | 待盘点（fetch 路径未查证） | | | | P4 盘点 |
| 其余 12 页 | 默认策略（=现状全量） | 0 | true | — | 按需后续收窄 |

---

## 六、分阶段实施计划

每阶段独立可合入、可验证；P0/P1 不改变任何可观察行为（除 #1-#4 收敛为等价实现）。

### P0 地基（无行为变化）

1. `page_registry.h`：`page_entry_t` 尾部加 `runtime_policy` 字段；新增 `PAGE_REGISTER_WITH_RUNTIME` 宏。
2. 新建 `components/ui/include/page_runtime.h` + `components/ui/page_runtime.c`（纯核心：查表、默认回退、合成函数；`#ifdef ESP_PLATFORM` 分离胶水，仿 holiday_fetcher.c 模式）。
3. 新建 `main/app_page_runtime.c`（胶水：interests 位图状态、服务注册表、enter/exit 钩子）。
4. `ui_manager.c`：`set_current_page_without_render` 补发 `page_switch_cb`（消暗道）。
5. **[v2] 注册时序修正（防漏首个切换）**：`page_switch_cb` 必须在 `ui_manager_init` 返回后**立即**注册（application.c:270 之后、RTC 唤醒块 :286 之前）——RTC 幻灯片快速路径在 `application_init` 内部就调用 `ui_manager_switch_page(GALLERY)`（application.c:311），若注册放在末尾会漏掉 `on_page_entered(GALLERY)`。注册后对恢复的初始页（ui_manager.c:245-249 已从 RTC_NOINIT 恢复）显式调一次 `on_page_entered`。`page_runtime` 的其余初始化（服务表等）可留在末尾。

**验收**：现有全部测试通过；日志打印"page X policy=default"且 19 页均命中默认回退；`idf.py build` 无警告。

### P1 数据兴趣（收益：唤醒期不再拉无关数据）

1. `application_t` 三个 bool → `page_runtime` 内部的 pending 位图（application_internal.h:84-86 删除）。
2. **[v2] pending 状态机**（见 3.3）：置位 = 页面进入/WiFi 连上/周期到期/缓存过期；清零 = 派发拉取时。`process_wifi_event` / `application_notify_wifi_if_connected` 改为 `page_runtime_set_pending(effective_interests(前台页))`；`application_run` 1s 循环消费 `pending & declared`，派发即清位。
3. `app_sync_on_data_refresh_request`（app_sync.c:121-134）改为置位 pending 中该页声明的位（页面自己声明的才能拉）。
4. 数据落地回调统一加 `page_runtime_is_page_active()` 守卫。
5. 迁移 CALENDAR（HOLIDAY+SNTP）、WEATHER、CODING_PLAN 三页。
6. host 单测：`tests/test_page_runtime.c`——默认回退、兴趣合成、冻结清位、越界页号。

**验收**：日历页前台时连网，日志仅出现 holiday（缓存未命中时）+ SNTP 请求，**无** weather/coding_plan HTTP 请求；切到天气页后 weather 拉取发生；切走后落地回调不触发刷新。

### P2 睡眠/唤醒参数（收益：唤醒节奏按内容变化）

1. `app_sleep_enter_scheduled` / `app_sleep_arm_sync_timer` 间隔统一走 `page_runtime_effective_wake_interval_min(前台页)`；删除 gallery 两处特判（映射表 #5/#6）+ 设置菜单定时器重启特判（#10）。
2. `wake_align` 实现：align 支配时闹钟 = 下次 00:01（PCF8563 匹配 分+时+日，rtc_pcf8563.c:100-103）；含 `s_last_served_day` 补刷逻辑（见 3.5 MIDNIGHT 边界）。
3. `main.c` 唤醒连网判断走 `needs_network_on_wake`（#7）；`application_init` gallery 快速路径改为 `on_rtc_wake` 钩子（#8）。
4. 运行时覆盖数组（按页，38B）：`ui_manager_set_gallery_slideshow_interval_minutes` 写 GALLERY 槽；唤醒后从 NVS 重放。
5. **[v3] 陈旧度指示（UX 硬需求，随 MIDNIGHT 一起交付）**：对齐唤醒 + 00:01 拉取失败（退避后入睡）会让用户整日看到昨日画面且零提示。全局维护"最后成功数据刷新时间戳"（数据落地成功回调写当前 RTC 时间），日历页 footer 栏渲染"更新于 MM-DD HH:MM"（复用 `widget_footer_bar`，calendar 页已有实例）。空缓存首启动不显示。

### P3 服务注册表（收益：服务生命周期自动管理）

1. `main/app_page_runtime.c` 实现服务表 + 所有权状态机（非 refcount，见 3.7）。
2. LAN HTTP 四个启停点（1.3 节列表）改走 `page_runtime_service_acquire(USER)` / `release_user`。
3. **[v3] 传输中粘性升级**随本阶段实现（规则见 3.7）：`ap_server_state_cb` 驱动升级/回落。
4. `app_sleep_is_local_http_running` 的三处调用改为 `page_runtime_service_any_running()`（#9）。

**验收**：设置菜单开 LAN HTTP → 切任意页服务仍在、睡眠被挡；进 AP 传输页服务自动起、切走自动停；WiFi 断开服务自动停（现状 application.c:176-178 保留）。

### P4 收尾与推广

1. NEWS 页数据路径盘点并迁移。
2. 其余页面按第五节表格逐页收窄默认策略（每页一次独立提交，回归其刷新行为）。
3. **[v3] 设置菜单"刷新间隔"项规格**：循环 30 → 60 → 120 分钟，**不提供 0**——`sync_interval=0` 现状语义是"永不自动睡眠"（app_sleep.c:238-241，user-manual Q5），若可从 UI 触达而日历等对齐型页面仍按内容唤醒，用户会认为"设置不生效"；0 保留为 NVS 高级用法并在手册标注。项说明文案注明："日历等页面按内容变化自动刷新，不受此间隔影响"。
4. **[v3→PO 已批准，随 R2 交付]**：LAN HTTP 空闲自动关 + 充电抑制深睡（决策 D3/D4，见第十节）。
5. 文档同步（硬交付）：README 架构节 + user-manual.md §7 睡眠行为、§4/§5 新设置项、§6.2 AP 传输启停语义；**[v3] 同时修正手册既有过时项**：Q6/:360"唤醒后回相册"（实际已恢复上次页，ui_manager.c:245-249）、:91/:401"天气 API 未接线"（`weather_api_init` 已在 application.c:344 调用）。**[PO] 其中纯文档修正不等 P4，随 R1 一并提交**（决策 D8）。

---

## 七、并发与深睡恢复分析

- **[v2 修正] 仲裁状态单线程、跨任务读经 C11 原子**：页面切换与服务 acquire/release 只发生在 `application_run` 主任务（事件队列消费，application.c:431-596）；AP server 回调经 `handle_*_async` 是队列投递模式（application.c:203-246）。**但数据落地回调不是**——如天气回调在 fetch 任务上下文直接执行（weather_api 任务 → `app_sync_on_weather_update` → 摸 renderer + 请求刷新，app_sync.c:97-114），这是既存的跨任务 UI 访问模式（本计划延伸依赖、不新引入）。因此：当前台页号需被 fetch 任务读取时，用 `atomic_uint`（`atomic_load_explicit(..., memory_order_relaxed)`）存储——Xtensa 对齐 32-bit load 本就单指令原子，但 `volatile` 在标准 C 中不保证原子性与可见性，不用。守卫的 check→refresh TOCTOU（检查后页面切走）良性：至多多一次 EPD 刷新，注明即可。pending 位图只在主任务读写，无锁成立。
- **深睡恢复**：policy 是 flash 常量无恢复成本；运行时覆盖数组（幻灯片间隔）从 NVS 重放；pending 位图唤醒后由前台页策略重新置位（丢"睡前 pending"语义正确——冻结即清除）。`s_rtc_last_page` 恢复前台页（ui_manager.c:245-249 于 init 内、:336-342 读取）已就绪。
- **与 sleep_manager 的关系不变**：`sm_set_busy/sm_kick` 机制照旧，page_runtime 只是新增一个 busy 查询方（服务运行态）；**[v2] 不新增 kick 来源**（idle_ms 已删——`sm_kick` 单调只升不降，sleep_manager.c:24-33，页面级更短超时在该原语下无法表达）。

---

## 八、风险与对策

| 风险 | 对策 |
|---|---|
| 默认策略写错导致未迁移页面丢数据 | 默认策略 = 现状三全集 + SNTP，P1 用日志断言"未注册 policy 的页面合成结果 == 现状位图" |
| AP_TRANSFER 自动停服改变用户预期 / **[v3] 传图中途切页中断上传** | 粘性升级规则防中断（见 3.7）；非传输态离页自动停；长按 BOOT 显式退出；若反馈负面改 USER 所有仅需一行策略 |
| `on_rtc_wake` 钩子在 deep-sleep 恢复早期执行，此时 PSRAM/外设未就绪 | 钩子在 `application_init`（外设初始化完成后，main.c:369）内调用，非 app_main 早期 |
| 唤醒对齐 MIDNIGHT 与"stale alarm 立即唤醒"竞态 | 复用 app_sleep.c:129-166 现有的取整+settle 检查，对齐只是改目标时刻，不新增路径 |
| interests 清除后页面回到前台数据过期 | `on_page_entered` 时若 declared 数据无有效缓存（weather/coding_plan 已有 NVS 缓存机制）→ 置 pending 位，主循环自然重拉；这是现有 `enter()` 请求刷新语义（calendar_page.c:249-255）的数据层对应物 |
| **[v2] pending 派发失败（WiFi 断开/HTTP 超时）导致位永久丢失** | 拉取失败时重置 pending 位并计数退避（30s→60s→…封顶 10min），连续 3 次失败停止直到下次 WiFi 事件；防止"永久无数据"和"疯狂重试"两个极端 |
| **[v2] 手动睡眠（BOOT-only 唤醒，无闹钟）后策略语义** | 手动睡眠不设闹钟（app_sleep.c:288-292），唤醒=全量冷启动，走 `needs_network_on_wake` 的冷启动分支（恒 true）——语义自然正确，写入测试用例固化 |
| host 测试覆盖 | 纯核心（查表/合成/冻结清位）全部 host 可测，仿 tests/test_widgets_calendar.c 直编组件源文件 |
| **[v3] 用户误解"设置不生效"**（改了间隔但日历仍半夜刷新） | 设置项不提供 0 + 项内说明"对齐型页面按内容刷新"；手册 §7 明示两类页面语义差异 |
| **[v3] 陈旧度指示被误读为故障**（"更新于昨天"引发客诉） | 文案用中性"更新于 MM-DD HH:MM"而非警示色；手册 FAQ 补一条"为什么显示旧的更新时间"；长按任意键可触发手动重拉（现有语义） |

---

## 九、测试与验收

1. **host 单测**（`tests/test_page_runtime.c`，接入 run_tests.sh）：
   - 未注册 policy → 默认回退（interval=系统默认、interests=全集、network=true）；
   - 注册 policy 的字段覆盖、未设字段回退；
   - `wake_align=MIDNIGHT` 的下次对齐时刻计算（含 23:59 边界、00:01 后未服务→补刷点、`s_last_served_day` 编解码）；
   - **[v2] pending 状态机**：置位事件全覆盖、派发清位、切页清除非前台声明位、失败退避计数；
   - **[v2] 服务所有权状态机**：PAGE 进出启停、USER 跨页存活、`release_all`、owner_page 不匹配时 `release_by_page` 为 no-op；**[v3] 传输中粘性升级/回落的状态迁移**。
2. **目标机场景**（每阶段末执行）：
   - 日历页入睡 → 串口日志核对 RTC 闹钟时刻与唤醒后 HTTP 请求列表；
   - **[v2] 功率/时序度量（核心目标的可量化验收）**：唤醒窗口时长 = 串口日志 `esp_startup`→`refresh idle` 时间戳差，或活动 LED GPIO 翻转 + 逻辑分析仪。目标：日历页节假日缓存命中时唤醒窗口从 ~20s 降到 <5s；日/周级平均唤醒次数从 48/天降到 ≤2/天（MIDNIGHT 对齐后）；
   - **[v3] 快速切换覆盖层导航回归**：BOOT 双击打开覆盖层 → 切页（走 `set_current_page_without_render` 暗道，ui_manager.c:151-158）→ 每次切换必须触发 exit/enter 与冻结语义（P0-4 的直接验证）；
   - **[v3] 传图中途快速切页**：client connected 状态下切走 → 服务存活、上传不中断；空闲态切走 → 服务停止（P3 验收）；
   - LAN HTTP 开启后入睡被挡、关闭后可睡；**[v2] 手动睡眠后服务注册表状态归零断言**；
   - 幻灯片 5min 回归（与现状时间线一致）；**[v2] 00:00:30 交互后入睡 → 5min 补刷闹钟生效（MIDNIGHT 边界用例）**；
   - **[v3] 夜间闪烁计数（用户价值主指标）**：日历页整夜放置（22:00–07:00）→ 串口日志统计 EPD 全刷次数，**[PO 修正] 目标 ≤1（00:01 的日界内容刷新属预期行为，非缺陷）**；现状为 ~18 次/夜（每 30min 一次）。
3. **构建门禁**：`idf.py build` + 现有 tests 全绿。

---


## 十、产品决策记录（v4，Product Owner 评审拍板）

> 评审输入：v1 架构方案 + v2 内核/嵌入式评审 + v3 PM/UI-UX 评审。以下决策为最终结论，后续变更须回到本节修订。

### D1 交付切分：两个发布，价值拐点前置

- **R1（用户可感知版本）= P0 + P1 + P2 + 立修项（D8）**。理由：夜间零/单次闪烁、唤醒窗口 <5s、陈旧度指示——全部用户价值落在这三阶段；P3/P4 是运维卫生与配套，不阻塞 R1。
- **R2 = P3 + P4**（服务生命周期、设置项 UI、LAN 空闲关、充电抑制、剩余页面迁移）。
- R1 合入门槛 = 第九节全部 R1 相关用例 + D9 发布门禁。

### D2 拒绝"P1 廉价替代"，承诺框架

PM 评审曾提出的"10 行 gate"替代方案**不做**。理由：R1 的核心价值（P2 对齐唤醒）依赖 policy 数据结构存在；廉价替代省的是 P0 的两三天，丢的是 P2 的排期与 R2 的全部地基。框架照计划执行。

### D3 LAN HTTP 空闲自动关：批准（R2）

- 规则：USER 开启 LAN HTTP 后，**连续 30 分钟无任何 HTTP 请求 → 自动 `release_user`**，状态栏 `*` 清除，设置值显示"已关闭"；用户重新开启即恢复。
- 实现要点：`ap_transfer_server.c` 的 12 个 URI handler 各加一行 `s_last_lan_activity_ms` 时间戳（或公共 pre-dispatch 包装，约 10 行）；`application_run` 1s 循环检查超时。
- 理由：现状"忘关 = 永不睡眠耗电到 3% 强关"（user-manual.md:291/:373）是可预期的客诉源；修复成本一个下午。
- 边界：传图上传进行中（POST /upload 活跃）不算空闲。

### D4 充电抑制深睡：批准，取简单方案（R2）〔v6 级联：理由重写，结论维持〕

- 规则：**充电中完全抑制计划睡眠**（不自旋调制间隔）；拔电后恢复。手动睡眠（用户显式"省电模式"）不受抑制——用户意图优先。
- 迟滞：充电状态稳定 60s 后才生效/解除，防 GPIO 抖动导致睡眠抖动。
- 理由〔v6 重写〕：原批准理由（"常充电桌面场景，充电时保持响应纯增益"）随主场景更正而失效——新场景下设备仅在耗尽后充电，D4 的价值场景从"日常"降为"罕见但关键"：耗尽关机 → 用户插电 → 设备若立即再睡，将错过整个充电窗口的数据同步与电量显示。保留理由：一行成本（application.c:651 加 `!charge_status_is_charging()`，API 现成 charge_status.h:45），且充电会话正是用户会看设备的时刻（插拔、确认恢复）。interval 衰减因子方案维持否决——两套语义不可预测。
- 明确不改变：刷新策略不因充电而变多（防面板老化加速）；低频瞥一眼场景下充电中也不主动刷屏。

### D5 AP 传输粘性规则：批准（R2，随 P3）

v3 §3.7 粘性升级规则照单全收：传输中（state 2→5/6）切页不停服；空闲态离页自动停；长按 BOOT 显式退出立停。回退开关保留（改 USER 所有一行）。

### D6 设置"刷新间隔"项：确认规格（R2）

循环 30 → 60 → 120，不提供 0（v3 P4-3 规格确认）。**实现注意**：`app_settings_menu.c` 现有 11 项、`items[12]` 数组恰好满（app_settings_menu.c:210）——新增一项前先把数组扩到 14，避免越界静默截断。

### D7 陈旧度指示：R1 交付，范围限日历页

"更新于 MM-DD HH:MM"只上日历页 footer（v3 P2-5）。天气/编程计划页本版本不做——等 R2 看日历页实际客诉反馈再决定是否推广。

### D8 立修项：不等阶段，R1 前独立提交

1. **calendar_page.c:310 丢失的 `case BTN_DOWN_CLICK:`**——DOWN 键翻不了下月是当下就存在的用户可感知缺陷，与框架无关，立即修。
2. **user-manual.md 三处过时**（唤醒回相册 ×2、天气未接线 ×2）——纯文档修正零风险，立即修。
3. ui_manager.c:333 的 NULL 默认值问题随 P0 顺手修。

### D9 发布门禁（R1 硬门槛，一票否决）

1. **唤醒源安全回归**：每版睡眠相关改动必须实测 BOOT 唤醒 + RTC 闹钟唤醒各一次（历史上有 EXT1 配错导致"不可唤醒深睡"的前科，app_sleep.c:186-188/295-296 注释为证）。
2. 夜间闪烁计数 ≤1（D9 修正后的口径）。
3. 日历页缓存命中唤醒窗口 <5s。
4. 幻灯片 5min 时间线回归（不劣化既有功能）。
5. 构建门禁：`idf.py build` + host 测试全绿。

### D10 非目标确认

v1 附录 B 全部维持：不做 vtable 扩展、不做帧哈希跳刷、不做 FAST_RC、不做页面级任务/动态内存。BOOT 唤醒 ~20s 延迟已知、接受、另立项。

### D11 LAN HTTP 认证加固：纳入 R2（v5，B2 评审产出，用户确认）

- 规则/内容: LAN HTTP 服务统一认证——① token 改为**每设备唯一**（首启动 NVS 生成，设置页/网页可查看），与 AP 密码解耦；② 认证覆盖**全部端点**（补 upload、photo GET）；③ 网页端配合（上传页携带 token）；④ AP 模式沿用其既有密码语义不变。随 R2 服务注册表工作一起交付。
- 理由: 立修需同步改网页传图流程，有破坏既有用户体验的风险；R2 本就重构服务层，边际成本低。被否选项：立修（风险）、接受现状记威胁模型（混合主场景含不可信局域网可能，用户未选）。
- 实现落点: ap_transfer_server.c `is_authorized`（:376-390）与各 URI handler 注册（:839-936）。
- 回退开关: 若网页端配合改造超期，可先交付"每设备唯一 token + 破坏性端点覆盖"，upload 认证顺延并显式记录。


### D12 充电插入即唤醒：批准（v6，主场景更正引出的新机会项）

- 规则/内容: 将 `CHARGE_DETECT_GPIO`（GPIO2，RTC 域，充电时低电平）加入深睡 ext1 唤醒掩码（ANY_LOW），并按 BOOT/RTC_INT 同款模式配置 `rtc_gpio` 上拉（app_sleep.c:189-196）——**耗尽关机后插入充电器即可自动唤醒开机**，无需手按 BOOT。落点：`app_sleep_enter_scheduled`（app_sleep.c:201）、`app_sleep_enter_manual`（app_sleep.c:307）、低电关机路径（application.c:605，现 `esp_deep_sleep_start()` 前 ext1 未配置任何充电唤醒）三处。
- 理由: 新主场景（床头/冰箱贴、仅耗尽后充电）下这是高价值缺口——现状耗尽关机后插入充电器**毫无反应**（ext1 仅 BOOT+RTC_INT，app_sleep.c:201），用户必须找到并按设备上的 BOOT 键；冰箱贴场景设备可能吸附在够不到的位置。硬件本就预留 `CHARGE_GPIO_AFFECT_SLEEP 1`（config.h:29）但从未接线——原设计意图的补全。成本约一个下午（三处掩码 + 上拉 + 回归）。
- [INFERENCE] 待硬件验证: 低电路径先 `board_power_vbat_off()`（application.c:604）再入睡——充电检测电路是否由独立电源轨供电、插入充电器时 GPIO2 是否真的拉低，需上板实测；若检测电路随 vbat 断电，则本决策降级为"计划中，硬件确认后实施"。
- 验收（并入 §九 R2 场景 + D9-1 唤醒源回归）: 耗尽关机 → 插充电器 → 设备自动开机进入充电显示；未充电时 GPIO2 上拉高电平、无误唤醒（回归 BOOT/RTC 唤醒不受影响）。
- 回退开关: 移除掩码中 GPIO2 一位即回到现状。

**级联说明（v6）**：主场景更正同时复核了 D9 门禁（口径不变，夜间闪烁/唤醒窗口对新场景权重上升）与 D7 陈旧度指示（低频瞥一眼场景下价值上升，维持 R1 交付不变）；其余决策（D1/D2/D3/D5/D6/D8/D10/D11）不受场景影响。
---

## 十一、B2 安全镜头评审（v5 补录）

> 按 `requirement-to-plan` Skill 补跑原四轮评审缺失的安全镜头。证据均为本轮新取证。

| ID | 标题 | 证据 | 严重度 | 处置 |
|---|---|---|---|---|
| S1 | LAN HTTP 认证 token 为源码硬编码弱值，且与 AP 密码**同值**、全设备一致、印在用户手册 | ap_transfer_server.c:39（`AP_PASSWORD "12345678"`）、:387（`strcmp(auth_hdr, "Bearer 12345678")`）、user-manual.md Q8 | 重要 | 接受→D11（R2：每设备唯一 token） |
| S2 | `upload` 端点完全无认证——同网任意主机可写入照片存储 | ap_transfer_server.c:404-513（全函数无 `is_authorized` 调用） | 重要 | 接受→D11（R2：端点全覆盖） |
| S3 | `photo` GET 读取分支无认证（仅 DELETE 有）——同网主机可任意拉取照片 | ap_transfer_server.c:668-696（认证仅见 :681 DELETE 分支） | 次要 | 接受→D11（R2 随端点全覆盖顺带） |
| S4 | token 比较用 `strcmp` 非常数时间 | ap_transfer_server.c:387 | 次要 | 记 TODO（随 D11 实现时改常数时间比较） |

**缓解性事实**（核对到的正面项，非发现）：上传定长校验（:420-430）、错误路径释放缓冲（:442-444）、破坏性设置操作要求认证（:582）、AP 为 WPA2（:1008）。

**验收**（并入 §九 R2 场景）：无 token 请求 upload/photo GET → 401；带每设备 token → 正常；旧硬编码 token → 401。
---
## 附录A：盘点中发现的既有 bug（随近阶段顺带修，独立提交）

1. **calendar_page.c:310-318**：DOWN 翻下月的 `case BTN_DOWN_CLICK:` 标签丢失，`widget_calendar_next_month` 分支不可达——DOWN 键无法翻下月（页头注释 calendar_page.c:6 声称 UP/DOWN=翻月）。
2. **ui_manager.c:333**：`ui_manager_get_current_page` 对 NULL mgr 返回 `UI_PAGE_CHAT`（枚举 0）而非断言/默认首页，静默误导调用方。
3. **[v3] user-manual.md 文档过时（文档 bug，P4 修）**：:413 Q6 与 §7.1 :360 声称"唤醒后回到相册"——代码自 RTC_NOINIT 恢复上次页面（ui_manager.c:245-249）；:91/:401 声称"天气 API 未接线"——`weather_api_init` 已被调用（application.c:344）。
4. **[v3] AP 传输切页后台残留（行为缺陷，P3 由粘性规则+离页停服一并修复）**：现状任意页面 BOOT 双击可经快速覆盖层切走（ui_manager.c:463-464、151-158），AP 服务无人停，耗电且状态栏残留 `*`。
5. **[v5] LAN HTTP 认证缺陷（安全，R2 修，见第十一节 S1–S4 / D11）**：硬编码弱 token 与 AP 密码同值且印在手册、upload 与 photo GET 端点无认证。

## 附录B：明确不做

- **[v2] 页面级 idle_ms（更短空闲入睡超时）**——`sm_kick` 单调只升不降（sleep_manager.c:24-33），页面级超时在该原语下无法表达；等真有页面需要时先给 sleep_manager 加 `sm_deadline_lower()` 原语再说（YAGNI）。
- **[v2] 服务引用计数**——每服务至多一个 PAGE owner + 一个 USER 布尔，多持有者场景不存在，refcount 只引入泄漏/下溢错误类。
- vtable 槽位扩展（`restore`/`wake` 等行为钩子）——`on_rtc_wake` 放 policy 数据结构已够用，避免 19 页面重写。
- 帧哈希比对跳刷、WiFi FAST_RC 快速重连——独立优化项，另立计划。
- 页面级任务/动态内存——保持静态单例 + flash 常量。
