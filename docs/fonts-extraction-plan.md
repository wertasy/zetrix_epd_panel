# ZecTrix EPD Panel — 字体组件抽离与字符集策略

> **文档状态**：已通过 Review 修订（v2）。修订点见文末「修订记录」。

## Context

项目当前把 `78/xiaozhi-fonts@1.5.5` 以**符号链接**方式挂载在 `components/78__xiaozhi-fonts/`（git mode 120000），指向仓库外的另一份拷贝。经与 ESP Component Registry 官方 1.5.5 原版压缩包（component_hash `e5526ea3c290…`，与项目 `.component_hash` 完全一致）逐文件 sha256 比对，确认：

- 项目**没有真正修改上游任何源文件**；
- 所做的只是「删掉上游绝大部分内容 + 新增 10 个项目自有字体文件 + 重写 `CMakeLists.txt`」；
- 上游公开符号（`font_noto_* / font_puhui_* / font_awesome_* / font_emoji_* / cbin_font`）在全项目 `main/` + `components/` 中**引用数为 0**；
- 项目实际只用自有的 10 个文件，它们对上游**零依赖**（include 仅有 `lvgl.h` / `<stdint.h>`）。

因此可干净地把自有字体抽离为本地组件 `zetrix_fonts`，彻底解除对 `78/xiaozhi-fonts` 的依赖。本文档给出实施步骤，并回答一个相伴的问题：**是否应在编译时动态生成字符集，而非使用固定的 GB2312 集合？**

---

## 一、基线事实（证据）

### 1.1 字体资产清单

项目自有的 10 个文件（全部为新增，非上游产物）：

| 文件 | 公开符号 | 消费方 |
|---|---|---|
| `src/SourceHanSansSC_Regular_slim.c` | `SourceHanSansSC_Regular_slim` | ~15 个页面 + `main/application.c` + `font_engine.h` (`BUILTIN_TEXT_FONT`) |
| `src/SourceHanSansSC_Medium_slim.c` | `SourceHanSansSC_Medium_slim` | ~15 个页面（标题字体） |
| `src/font_zectrix_16_1.c` + `include/font_zectrix.h` | `font_zectrix_16_1` + 30+ 个 `FONT_ZECTRIX_*` 字形宏 | `log_page`、`photo_gallery_page`、`wifi_page`、`ui_text_icon_glyph_for_code` |
| `src/font_zectrix_48_1.c` | `font_zectrix_48_1` | `wifi_page.c:26`（`kWifiLargeIconFont`，活跃使用） |
| `src/weather_icons/weather_icons_16.c` + `include/weather_icons.h` | `weather_icons_16` | `weather_page`、`weather_detail_page` |
| `src/weather_icons/weather_icons_48.c` | `weather_icons_48` | `almanac_page`、rawdraw `weather_card.c:88` |
| `src/fa_settings/fa_settings_16.c` + `include/fa_settings.h` | `fa_settings_16` + 图标宏 | `settings_page`、`calendar_page`、`ebook_page`、`news_page`、`photo_gallery_page`、`ui_manager` |

> 来源标注（来自 `.c` 文件首部 `Opts:` 行）：两个 SourceHanSans 由 `lv_font_conv --no-compress --font zfull_GB.ttf --symbols …GB2312 (6763 字符)…` 生成（Regular 16 px / Medium 24 px，均 1 bpp，字符集均已验证为 6763 CJK）；图标字体来自 IcoMoon 导出。三者均为项目自有资产。

### 1.2 组件依赖图（消费方）

#### CMakeLists REQUIRES（显式依赖）

```
main/CMakeLists.txt:3            REQUIRES … 78__xiaozhi-fonts …
components/ui/CMakeLists.txt:51  REQUIRES … 78__xiaozhi-fonts …
```

#### 自定义 header 的 `#include` 分布（全量 grep，共 8 处）

- `fa_settings.h` → `ui_manager.c`、`calendar_page.c`、`ebook_page.c`、`news_page.c`、`photo_gallery_page.c`、`settings_page.c`
- `weather_icons.h` → `weather_page.c`、`weather_detail_page.c`
- `font_zectrix.h` → `wifi_page.c`

#### 其他引用点（Review 补充——v1 漏掉）

| 位置 | 引用内容 | 处理方式 |
|---|---|---|
| `tests/run_tests.sh:12` | `-Icomponents/78__xiaozhi-fonts/include`（主机端 gcc 编译 include 路径） | **必须改为** `-Icomponents/zetrix_fonts/include` |
| `README.md:43` | 项目结构树中的 `78__xiaozhi-fonts` 注释 | 改为 `zetrix_fonts` |
| `docs/port-refinement-plan.md:122-124` | 引用 `78__xiaozhi-fonts` 作为字体符号来源 | 历史规划文档，可选更新 |

#### rawdraw 组件的隐式依赖（Review 补充——v1 未记录）

`components/rawdraw/` 的 4 个 `.c` 文件**直接引用**字体符号，但其 `CMakeLists.txt` **未声明** `REQUIRES` 字体组件：

| 文件 | 引用 |
|---|---|
| `widgets/calendar.c:847-849` | `&SourceHanSansSC_Medium_slim`、`&SourceHanSansSC_Regular_slim` |
| `widgets/weather_card.c:88-90` | `&weather_icons_48`、`&SourceHanSansSC_Regular_slim` |
| `widgets/modal.c:18` | `&BUILTIN_TEXT_FONT`（= `SourceHanSansSC_Regular_slim`） |
| `widgets/voice_wakeup.c:16` | `&BUILTIN_TEXT_FONT` |

这些符号通过 `font_engine.h:130-137` 的 `FONT_DECLARE(...)` 宏声明为 `extern`，定义在字体组件中。当前能链接成功，是因为 `main` + `ui` 都 `REQUIRES` 字体组件，字体组件的 `.a` 出现在最终链接行，rawdraw 的未解析 extern 在最终链接时被解析。**这是既有的跨组件隐式依赖，抽离后行为不变。**

> 主机端单测规避了此问题：`run_tests.sh` 的 `RAWDRAW_SRCS` 只编译 `rawdraw.c / rawdraw_ext.c / layout.c / theme.c / framebuffer.c / clock.c`，**不编译** calendar/weather_card/modal/voice_wakeup——所以单测不触碰字体符号。

字体符号声明集中在 `components/rawdraw/include/font_engine.h:130-135`（`FONT_DECLARE(...)` 宏展开为 `extern const lv_font_t …`），以及各图标 header 内的 `extern` 声明。

### 1.3 Flash 预算与字体体积（Review 修正——v1 严重高估）

应用分区大小（`partitions/v2/*.csv`）：

| 板子 | app 分区 | 备注 |
|---|---|---|
| 4M flash | factory 0x270000 ≈ **2.4 MB** | 单分区，无 OTA |
| 8M flash | ota_0/ota_1 各 0x2F0000 ≈ **3 MB** | 双 OTA |
| 16M flash | ota_0/ota_1 各 0x3F0000 ≈ **4 MB** | 双 OTA |

**关键区分：`.c` 源码体积 ≠ 编译后 flash 占用。** 字体 `.c` 文件是 hex 编码的位图数组（每个字节写成 `0xNN,` 占 5–6 字符），源码因此膨胀约 13 倍：

| 字体 | `.c` 源码 | 编译后位图（`0xNN` token 计数） | 估算 flash 总占用（位图+描述符表） |
|---|---|---|---|
| SourceHanSansSC_Regular_slim (16px) | 2.17 MB | 0.156 MB（159,976 bytes） | ~0.20 MB |
| SourceHanSansSC_Medium_slim (24px) | 3.62 MB | 0.292 MB（299,197 bytes） | ~0.37 MB |
| **两字体合计** | **5.79 MB（源码）** | **0.46 MB（位图）** | **~0.55–0.60 MB（flash）** |
| 图标字体（6 个） | ~0.12 MB（源码） | ~0.01 MB | ~0.01 MB |

> **结论：两个文本字体编译后仅占 flash ~0.5–0.6 MB，所有板子（含 4M 最小分区）均能放下。** v1 文档错误地把源码体积当作 flash 占用，得出"8M/4M 板放不下"的错误结论，已修正。
>
> 精确数值需 `idf.py size` 实测确认（当前无 `build/` 目录可查），但量级已由 token 计数锚定。

### 1.4 构建时间考量

两个文本字体 `.c` 是项目中**最大的编译单元**（2.17 MB + 3.62 MB 源码，远超其他文件）。每次全量构建中，这两个 TU 占据可观的编译时间。这是第三节考虑字符集收敛的现实动机（比 flash 节省更重要）。

---

## 二、方案 A 实施计划

### 2.1 目标

- 新建本地组件 `components/zetrix_fonts/`，承载全部 10 个自有字体文件；
- 解除对 `78/xiaozhi-fonts` 的所有引用（符号链接 + CMakeLists + 测试脚本 + 文档）；
- 行为零变化：所有 `extern` 符号、header 名、include 路径保持不变，业务代码**无需改动一行**；
- 构建产物等价（同样的 `.c` 编进同样的组件名空间）。

### 2.2 步骤

#### 步骤 1：创建新组件目录结构

```
components/zetrix_fonts/
├── CMakeLists.txt
├── idf_component.yml
├── include/
│   ├── font_zectrix.h
│   ├── weather_icons.h
│   └── fa_settings.h
└── src/
    ├── SourceHanSansSC_Regular_slim.c
    ├── SourceHanSansSC_Medium_slim.c
    ├── font_zectrix_16_1.c
    ├── font_zectrix_48_1.c
    ├── weather_icons/
    │   ├── weather_icons_16.c
    │   └── weather_icons_48.c
    └── fa_settings/
        └── fa_settings_16.c
```

#### 步骤 2：写 `components/zetrix_fonts/idf_component.yml`

```yaml
version: "1.0.0"
description: ZecTrix project fonts (SourceHanSansSC slim + custom icon fonts)
dependencies:
  idf: ">=5.3"
```

> **Review 修正（v1 错误）**：v1 在此处加了 `lvgl/lvgl: "~9.3.0"`——这是错误的。本地组件不应在 `idf_component.yml` 中声明 registry 依赖来拉取 lvgl；lvgl 已由 `main/idf_component.yml` 作为 managed component 管理，本组件只需在 CMakeLists 中 `PRIV_REQUIRES lvgl` 即可。原 `78__xiaozhi-fonts/idf_component.yml` 也只有 `idf: '>=5.3'`，无 lvgl 条目。此处保持一致。
>
> 本地组件的 `idf_component.yml` 是可选的；保留它是为了声明 idf 版本约束与 metadata，与原组件形式一致。

#### 步骤 3：写 `components/zetrix_fonts/CMakeLists.txt`

直接采用当前 `78__xiaozhi-fonts/CMakeLists.txt` 的内容（已经是显式列表，不依赖 `file(GLOB)`）：

```cmake
idf_component_register(
    SRCS
        "src/SourceHanSansSC_Medium_slim.c"
        "src/SourceHanSansSC_Regular_slim.c"
        "src/font_zectrix_16_1.c"
        "src/font_zectrix_48_1.c"
        "src/weather_icons/weather_icons_16.c"
        "src/weather_icons/weather_icons_48.c"
        "src/fa_settings/fa_settings_16.c"
    INCLUDE_DIRS
        "include"
    PRIV_REQUIRES
        "lvgl"
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC LV_LVGL_H_INCLUDE_SIMPLE)
```

> 注意：**不迁移 `src/cbin_font.c`**——该文件是从上游保留下来的死代码（全项目 0 引用，已 grep 确认）。不纳入新组件是干净的 cutover，符合"不留废弃代码"原则。

#### 步骤 4：迁移文件

从符号链接指向的真实路径（`/code/Github/youn-ink-fourcolor-firmware/firmware/main/components/78__xiaozhi-fonts/`）把 10 个文件**复制**（非移动，保留原仓库不动）到新组件对应位置。

需迁移的文件清单（核对用）：

- `include/font_zectrix.h`、`include/weather_icons.h`、`include/fa_settings.h`
- `src/SourceHanSansSC_Regular_slim.c`、`src/SourceHanSansSC_Medium_slim.c`
- `src/font_zectrix_16_1.c`、`src/font_zectrix_48_1.c`
- `src/weather_icons/weather_icons_16.c`、`src/weather_icons/weather_icons_48.c`
- `src/fa_settings/fa_settings_16.c`

**不迁移**：`src/cbin_font.c`、`include/cbin_font.h`、`png/twemoji_*/`、`CHECKSUMS.json`、`.component_hash`、`idf_component.yml`（上游的）、`CMakeLists.txt`（上游的）。

#### 步骤 5：更新消费方引用（共 4 处）

**`main/CMakeLists.txt:3`**：

```diff
- REQUIRES bsp rawdraw network ui lvgl nvs_flash 78__xiaozhi-fonts audio)
+ REQUIRES bsp rawdraw network ui lvgl nvs_flash zetrix_fonts audio)
```

**`components/ui/CMakeLists.txt:51`**：

```diff
- REQUIRES rawdraw bsp network 78__xiaozhi-fonts espressif__qrcode
+ REQUIRES rawdraw bsp network zetrix_fonts espressif__qrcode
```

**`tests/run_tests.sh:12`**（Review 补充——v1 遗漏）：

```diff
-       -Icomponents/78__xiaozhi-fonts/include \
+       -Icomponents/zetrix_fonts/include \
```

> 这是主机端 gcc 单测的 include 路径。不改会导致主机端编译时找不到 `weather_icons.h` / `fa_settings.h` / `font_zectrix.h`。

**`README.md:43`**（Review 补充——v1 遗漏）：

```diff
- │   └── 78__xiaozhi-fonts       # 外部预编译字体组件链接（SourceHanSans、天气、图标）
+ │   └── zetrix_fonts            # 项目自有字体组件（SourceHanSans、天气、图标）
```

> `docs/port-refinement-plan.md:122-124` 中对 `78__xiaozhi-fonts` 的引用属历史规划文档，可选更新，不阻塞。

> 业务 `.c` 文件中的 `#include "fa_settings.h"` 等保持不变——IDF 组件机制通过 `INCLUDE_DIRS "include"` 暴露 header，路径解析与组件名无关。

#### 步骤 6：移除符号链接

```bash
# 删除符号链接（git 记录为 symlink 删除）
git rm components/78__xiaozhi-fonts
```

> **复核**：`dependencies.lock` 的 `direct_dependencies`（`:84-92`）中**没有** `78/xiaozhi-fonts`——因为它是通过 `components/` 本地路径引用，不经 registry 拉取。所以 `dependencies.lock` **无需改动**。`.component_hash` 与 `CHECKSUMS.json` 随符号链接删除一并消失，符合预期。

#### 步骤 7：rawdraw 隐式依赖的处理决策

如 §1.2 所述，rawdraw 的 4 个 `.c` 直接引用字体符号但未声明 `REQUIRES`。两个选项：

| 选项 | 做法 | 评估 |
|---|---|---|
| **A（推荐，本次 PR）** | 不动 rawdraw 的 CMakeLists，保持隐式依赖 | 零行为变化，匹配现状；最终链接行为不变 |
| B（可选后续 PR） | 给 rawdraw CMakeLists 加 `PRIV_REQUIRES zetrix_fonts` | 显式化依赖图，更正确，但超出"纯重命名"范围 |

→ **本 PR 采用选项 A**，不做 rawdraw CMakeLists 改动。选项 B 列为可选的构建图治理后续项。

### 2.3 构建验证（Acceptance）

| 验证项 | 命令 / 方法 | 期望 |
|---|---|---|
| 配置阶段 | `idf.py reconfigure` | 无错误，`zetrix_fonts` 出现在组件列表 |
| 编译阶段 | `idf.py build` | 通过；无 `undefined reference to SourceHanSansSC_*` / `font_zectrix_*` / `weather_icons_*` / `fa_settings_*` |
| 符号检查 | `nm build/zetrix_epd_panel.elf \| grep -c 'SourceHanSansSC_Regular_slim'` | ≥1（符号存在） |
| 主机单测 | `tests/run_tests.sh` | 全部 PASS（验证 include 路径更新正确） |
| 体积对比 | `idf.py size` | 与抽离前对比，app bin 应**几乎不变**（仅少编入 `cbin_font.c` 死代码的微量贡献） |
| 运行时 | 烧录到目标板，目测各页面文字、图标、天气符号渲染正常 | 与抽离前像素级一致 |

### 2.4 风险与回滚

| 风险 | 概率 | 缓解 |
|---|---|---|
| 漏改某处 `78__xiaozhi-fonts` 引用 | 低（已全量 grep，共 4 处：2× CMakeLists + 1× run_tests.sh + 1× README） | 构建/单测会立即报错，显式可定位 |
| header include 路径解析变化 | 极低（IDF 组件机制不依赖组件名做路径解析） | 编译期即可发现 |
| rawdraw 隐式依赖断裂 | 极低（最终链接行为不变，已分析） | `idf.py build` 链接阶段即可发现 `undefined reference` |
| 符号链接删除后 git 历史丢失原字体 provenance | 低 | 新组件目录有完整文件副本，provenance 由 `.c` 文件首部 `Opts:` 行保留 |

回滚：所有变更集中在一次 commit（新组件 + 4 处引用改动 + 符号链接删除），`git revert` 单次提交即可。

### 2.5 不做的事（Out of Scope）

- **不**修改任何业务 `.c` 文件的 `#include` 或字体符号引用；
- **不**重命名 header 或字体符号（避免连锁修改）；
- **不**改 `font_engine.h` 的 `FONT_DECLARE` 列表（符号名未变）；
- **不**改 rawdraw 的 CMakeLists（隐式依赖保持现状，见 §2.3 步骤 7）；
- **不**引入 `noto-fonts` / 任何 registry 托管字体组件（理由见下文第三节）；
- **不**改动 `dependencies.lock`（无对应条目）。

---

## 三、字符集策略：是否动态生成？

### 3.1 现状

两个文本字体的字符集（来自 `.c` 首部 `Opts:` 注释，已验证）：

- **SourceHanSansSC_Regular_slim**：16 px / 1 bpp，`--symbols` 包含 GB2312 **全 6763 个汉字** + ASCII + 中英标点 + 天气/货币/箭头/单位/数学符号。源字体 `zfull_GB.ttf`。
- **SourceHanSansSC_Medium_slim**：24 px / 1 bpp，同上字符集（6763 CJK，已验证）。
- 编译后 flash 占用合计 **~0.5–0.6 MB**（见 §1.3，源码 5.79 MB 是 hex 编码膨胀）。

### 3.2 实际字符使用情况（证据）

| 来源 | distinct CJK 字符数 | 性质 | 词汇封闭性 |
|---|---|---|---|
| 源码字符串字面量（`main/` + `components/ui` + `components/network` + `components/bsp`） | **425** | 静态 UI 文案（"关闭"/"已连接"/"系统"/"重启"…） | 封闭 |
| 天气文本（QWeather API → `weather_text[]`） | 受限于气象词表，估算 30–60 | 动态，但词汇有限（"晴/多云/小雨/霾/雷阵雨"…） | 准封闭 |
| 新闻标题/摘要（`news_page` → `title/summary`） | **无界** | 动态，任意中文 | 开放 |
| 聊天内容（`chat_page`，LLM/TTS 回复） | **无界** | 动态，任意中文 | 开放 |
| 电子书（`ebook_page`，从文件加载） | **无界** | 动态，任意中文 | 开放 |

> 静态字面量仅 425 字，占 GB2312 的 **6.3%**。但天气/新闻/聊天/电子书四类动态内容的词汇**不封闭**，无法在编译期枚举。

### 3.3 候选方案对比（Review 修正——v1 体积数据全部错误）

| 方案 | 字符集来源 | flash 占用 | 构建时间 | 运行时缺字风险 | 复杂度 | 评估 |
|---|---|---|---|---|---|---|
| **A. 现状：全 GB2312 (6763)** | 固定 | ~0.55 MB | 慢（两个超大 TU） | 无 | 零 | 保守、正确 |
| **B. GB2312 Level 1 (3755)** | 固定 | ~0.30 MB（−0.25 MB） | 快约一半 | 极低（L1 覆盖常用汉字 99.9%） | 零（lv_font_conv 重生成） | **可选**：主要收益在构建时间 |
| **C. 仅源码字面量 (425)** | 编译期扫描 | ~0.05 MB | 极快 | **致命**：新闻/聊天/电子书大量 □□□ | 中 | **否决**：破坏运行时显示 |
| **D. 字面量 + 高频字表 + assets 语料** | 编译期合并多源 | ~0.10–0.15 MB | 快 | 中（依赖语料覆盖度） | 高 | 仅在确认无动态内容时考虑 |
| **E. 字面量 + 服务端 glyph_push 回退** | 编译期 + 运行时下发 | ~0.05 MB（设备端） | 极快 | 低（需服务端） | 极高（=noto-fonts 架构） | **否决**：架构不匹配（见下） |

### 3.4 结论与建议

#### 3.4.1 文本字体：不要做"纯源码字面量扫描"的动态生成

**否决方案 C / D 的核心理由**：本项目的新闻/聊天/电子书页面接收**任意中文**运行时内容。任何在编译期通过扫描源码或合并语料来生成字符集的方案，都会在内容词汇漂移时出现 `□` 缺字方块。这不是理论风险——新闻标题每天变，LLM 回复每次不同。动态字符集生成**只对词汇封闭的 UI 安全**，本项目恰好不是。

`noto-fonts` 2.0.0 的 glyph_push 服务端下发架构（方案 E）确实能解决这个问题，但它的代价是：
- 强制 `idf >=5.5.2` + `lvgl ~9.5.0`（项目当前 `lvgl 9.3.0`，全固件升级风险）；
- 引入能力协商 + CBIN 分片加载器 + PSRAM glyph 缓存运行时（设备无 PSRAM 时退化为单批清除）；
- 需要配套 xiaozhi 协议服务端——本项目是独立 EPD 面板，无此服务端。

→ **不引入 noto-fonts，不做服务端 glyph_push。**

#### 3.4.2 文本字体：GB2312 Level 1 是可选优化，收益主要在构建时间

**方案 B（可选，非阻塞）**：把字符集从 GB2312 全集（6763）收敛到 **GB2312 Level 1（3755 常用汉字）**。

- Level 1 是按使用频率排序的前 3755 字，覆盖现代汉语常用字的 **99.9%+**；
- Level 2 主要是生僻字、地名用字、姓氏用字——EPD 面板显示新闻/聊天偶尔会碰到（如人名生僻字），出现 `□`；
- flash 收益有限（省 ~0.25 MB），**主要收益是构建时间**——两个最大 TU 的数据量减半（§1.4）；
- **仍然是固定集合**，不引入编译期生成工具链的运行时风险，只改字体生成步骤。

> **Review 修正（v1 错误）**：v1 称 L1 "省 ~2.4 MB"且"直接决定 8M/4M 板能否放下"——这是把源码体积当 flash 体积的错误推论。修正后：所有板子在方案 A（全 GB2312）下都能放下字体；L1 的价值转为构建时间优化。

实施方式（若采纳）：

1. 用 `lv_font_conv` 重新生成两个 `.c`，`--symbols` 改为 GB2312 L1 字符表 + 现有 ASCII/标点/天气/符号集；
2. 替换 `zetrix_fonts/src/SourceHanSansSC_*_slim.c`；
3. 不改任何业务代码、不改 header。

> 这一步独立于方案 A 的抽离，可在抽离完成后的后续 PR 单独评估。**抽离本身不依赖此优化。**

#### 3.4.3 图标字体：保持现状，不要动态生成

`font_zectrix_*` / `weather_icons_*` / `fa_settings_*` 三组图标字体：

- 合计 flash 占用 ~0.01 MB，可忽略；
- 字形通过 IcoMoon PUA 编码（U+E900–E91D 等）映射，源码中按 `FONT_ZECTRIX_*` 宏名引用，**不是按字符内容引用**——扫描源码字面量无法正确子集化；
- 集合本身就是项目自定义的固定图标清单，没有"动态"可言。

→ 图标字体保持现状，不参与任何字符集优化。

#### 3.4.4 何时重新评估动态生成

仅在以下条件**同时**满足时，才值得重新考虑编译期字符集生成：

1. 确认新闻/聊天/电子书功能被裁剪或不再接收任意中文（即内容词汇变封闭），**且**
2. 愿意接受对未覆盖字符显示 `□`，**且**
3. 愿意承担 lv_font_conv 工具链依赖与构建脚本复杂度。

当前条件一不满足（三类动态内容均接收任意中文），故动态生成**不是本项目的正确选择**。

---

## 四、执行顺序与验收

### 4.1 推荐分两个 PR

| PR | 内容 | 验收 |
|---|---|---|
| **PR-1**（方案 A 抽离） | 步骤 1–6 + §2.3 验证 | 硬件构建通过 + 主机单测通过 + 运行时渲染像素级一致 |
| **PR-2**（可选，GB2312 L1 收敛） | §3.4.2 的字符集优化 | 构建通过，构建时间下降，长期观察是否有缺字反馈 |

PR-1 是干净的结构性重构，零行为变化，应优先独立合入。PR-2 涉及字体内容变化，需独立验证，不阻塞 PR-1。

### 4.2 PR-1 完成定义（Definition of Done）

- [ ] `components/zetrix_fonts/` 建立并包含全部 10 个文件（无 `cbin_font`）
- [ ] `main/CMakeLists.txt`、`components/ui/CMakeLists.txt` 引用改为 `zetrix_fonts`
- [ ] `tests/run_tests.sh` include 路径改为 `zetrix_fonts`
- [ ] `README.md` 结构树更新
- [ ] `components/78__xiaozhi-fonts` 符号链接删除
- [ ] `idf.py build` 通过（目标板）
- [ ] `tests/run_tests.sh` 全部 PASS（主机端）
- [ ] `nm` 确认字体符号存在
- [ ] 烧录后目测各页面文字/图标渲染正常（与抽离前对比）
- [ ] git 历史为单次 commit，可一键 revert

---

## 附：关键证据索引

| 事实 | 证据位置 |
|---|---|
| 上游 1.5.5 hash 与项目一致 | `.component_hash = e5526ea3c290…` = registry 1.5.5 `component_hash` |
| 项目 0 引用上游符号 | `grep "font_noto_\|font_puhui_\|font_awesome_\|font_emoji_\|cbin_font" main components` → 0 命中 |
| 10 个自有文件对上游零依赖 | 各文件 `#include` 仅 `lvgl.h` / `<stdint.h>` |
| 8 处自定义 header 消费 | grep `#include "(fa_settings\|weather_icons\|font_zectrix)\.h"` |
| **rawdraw 4 处直接引用字体符号** | `calendar.c:847`、`weather_card.c:88`、`modal.c:18`、`voice_wakeup.c:16` |
| **tests/run_tests.sh 引用组件路径** | `run_tests.sh:12` `-Icomponents/78__xiaozhi-fonts/include` |
| 静态字面量仅 425 CJK 字 | python 扫描 `main/`+`components/{ui,network,bsp}/` 字符串字面量 |
| 字符集 = GB2312 6763 字（两字体均已验证） | `SourceHanSansSC_{Regular,Medium}_slim.c` 首部 `Opts:` 注释解析 |
| **flash 占用 ~0.5 MB（非 5.6 MB）** | hex token 计数：Regular 159,976 B + Medium 299,197 B 位图数据 |
| Flash 分区约束 | `partitions/v2/{4m,8m,16m}.csv` |
| noto-fonts 2.0.0 架构 = 服务端 glyph_push | registry README + `charsets/{basic,common}.json` 分层定义 |

---

## 修订记录

### v2（Review 修正）

| # | 问题 | 修正 |
|---|---|---|
| 1 | **严重**：§1.3 / §3 把 `.c` 源码体积（5.6 MB）当作 flash 占用，得出"8M/4M 板放不下"的错误结论 | 实测 hex token：编译后位图仅 0.46 MB，flash 总占用 ~0.55 MB。所有板子均能放下。全文体积数据已修正 |
| 2 | §步骤 2 的 `idf_component.yml` 错误添加 `lvgl/lvgl: "~9.3.0"` | 本地组件不应在 yml 声明 registry 依赖；lvgl 由 CMakeLists `PRIV_REQUIRES` 处理。已删除该行 |
| 3 | §1.1 `font_zectrix_48_1` 标注"保留备用，低频" | 实为 `wifi_page.c:26` 活跃使用（`kWifiLargeIconFont`）。已修正描述 |
| 4 | v1 遗漏 `tests/run_tests.sh:12` 的组件路径引用 | 补入步骤 5，单测验证纳入 §2.3 验收 |
| 5 | v1 遗漏 `README.md:43` 的结构树引用 | 补入步骤 5 |
| 6 | v1 未记录 rawdraw 的跨组件隐式依赖 | 新增 §1.2 专节 + §2.3 步骤 7 的处理决策（选项 A：保持现状） |
| 7 | §3.3 方案表体积数据全部基于错误前提 | 已按修正后的 flash 数据重算，新增"构建时间"列 |
| 8 | §3.4.2 L1 收益从"省 2.4 MB flash"修正为"省 ~0.25 MB flash + 构建时间减半" | L1 定位从"flash 必需"转为"可选构建优化" |
