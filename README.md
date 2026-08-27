# ZecTrix 4.2寸 BWRY 四色电子纸面板固件（纯 C）

将原 C++ 版本的四色（黑/白/红/黄）电子纸面板固件移植到纯 C 语言的 ESP-IDF 工程（ESP32-S3）。使用轻量级 `rawdraw` 2D 图形库直接操作 2bpp 帧缓冲区渲染 19 个 UI 页面，集成完整的电源管理、深睡唤醒（RTC 闹钟 + 按键）、网络数据同步（天气/编程套餐用量/节假日）、WiFi/BLE 传图与 LAN HTTP 服务。

## 项目特点

* **纯 C 实现**：C++ 类 → C 结构体 + 前缀函数；虚函数 → ops 函数指针表；`std::vector` → 定容数组。
* **轻量化 rawdraw 引擎**：直接在 2bpp 帧缓冲区（00=黑，01=白，10=黄，11=红）绘制几何图形、抖动区域、圆角边框与 UTF-8 文本；15 个可复用控件（button/slider/modal/calendar/progress_bar…）。
* **19 个页面 + 页面注册表**：相册（幻灯片轮播）、天气、天气详情、日历/黄历、编程套餐用量、聊天、WiFi、新闻、电子书、日志、人生进度、年度进度、设置、AP 传图等，`PAGE_REGISTER` 宏 + GCC constructor 自动注册，快捷切换浮层。
* **EPD 刷新调度**：双缓冲 Popcount 差分对比 + 去抖合并窗口；SSD2683 四色面板强制全刷；首刷合并窗口（默认 2s，WiFi 启动路径 10s）避免开机双闪。
* **深睡体系**：RTC（PCF8563）闹钟唤醒 + BOOT 按键 EXT1 唤醒；RTC IO 模式保持唤醒脚上拉；RTC 内存保存最后浏览页面；幻灯片快路径唤醒可跳过 WiFi。
* **网络与传输**：天气（QWeather）/ 编程套餐（智谱 BigModel）/ 节假日 API 同步；WiFi AP + HTTP 传图（Floyd-Steinberg 抖动、1bpp/2bpp）；BLE GATT 传图；LAN HTTP 服务器（相册管理）。
* **外设**：PCF8563 RTC、GT23SC6699 NFC（NDEF 读写）、ES8311 音频（I2S + esp_codec_dev）、电池充电状态监控。
* **主机端单元测试**：图形/布局/网络解析/UI 页面冒烟等 10 组测试脱离 ESP-IDF 直接在 Linux 上运行。

## 项目结构

组件遵循 ESP-IDF 默认布局：`include/` 放公共头文件，源码在组件根目录；功能性子目录（`ui/pages/`、`rawdraw/widgets/`）保留。

```
zetrix_epd_panel/
├── main/                        # 应用层
│   ├── main.c                   # 入口：按键配置、显示驱动接线、渲染回调
│   ├── application.c/.h         # 应用单例：事件队列、按键路由、状态栏
│   ├── application_internal.h   # 应用模块拆分的共享状态/声明
│   ├── app_sync.c               # 数据同步编排（天气/套餐/节假日/SNTP 路由）
│   ├── app_sleep.c              # 深睡管理：RTC 闹钟、外设下电、同步定时器
│   ├── app_settings_menu.c      # 设置菜单构建与回调分发
│   ├── app_page_runtime.c/.h    # 后台服务注册表（LAN/AP 所有权状态机）
│   └── app_sntp.c               # SNTP 时间同步
├── components/
│   ├── app_state/               # NVS 应用状态缓存（nvs_state）
│   ├── audio/                   # WebSocket 协议 + 流式文本分块管线
│   ├── bsp_board/               # 板级：电源轨、I2C 总线、LED 任务、系统信息
│   ├── bsp_connectivity/        # WiFi STA 管理器、NFC
│   ├── bsp_display/             # epd_driver（SSD2683 SPI）+ epd_refresh 刷新调度
│   ├── bsp_peripherals/         # RTC、音频播放器、充电状态
│   ├── bsp_power/               # 睡眠管理器（睡眠投票）
│   ├── bsp_storage/             # 存储管理、NVS 设置
│   ├── data_types/              # 纯数据 DTO（天气/照片/套餐），零依赖
│   ├── network/                 # 天气/节假日/套餐 API、照片下载与存储、
│   │                            # BLE GATT、AP/LAN 传输服务器
│   ├── rawdraw/                 # 2bpp 绘制引擎、布局、主题、时钟、帧缓冲
│   │   └── widgets/             # 15 个 UI 控件
│   ├── ui/                      # UI 管理器、页面注册表、状态栏、快捷切换
│   │   ├── include/             # ui_manager / page_registry / page_runtime
│   │   └── pages/               # 19 个页面渲染器（*.c + 私有头）
│   └── zetrix_fonts/            # 项目自有字体（思源黑体 slim、图标字体）
└── tests/                       # 主机端测试 + run_tests.sh
```

## 页面运行时框架

页面通过 `PAGE_REGISTER_WITH_RUNTIME` 声明运行时策略（flash 常量 `page_runtime_policy_t`）：数据兴趣（拉什么）、唤醒间隔与对齐（多久醒 / 对准内容变化点）、唤醒是否联网、绑定的后台服务。前台页面策略生效，未声明字段回退系统默认（= 框架引入前的全局行为，未迁移页面零变化）。页面切换时旧页面被冻结：数据兴趣清零、页面所有服务释放。仲裁器 `components/ui/page_runtime.c`（纯逻辑，主机可测）+ `main/app_page_runtime.c`（服务所有权 ESP 胶水）。

后台服务（LAN HTTP / AP 传输）由服务注册表管理所有权：页面所有（切页即停，传输中粘性升级防中断）或用户所有（跨页存活，30 分钟空闲自动关）。

## 编码规范

* 标识符一律 snake_case（常量不带 `k` 等前缀）；`s_` 前缀 = 文件内 static 状态，`g_` = 跨文件全局单例。
* 格式由 `.clang-format` 约束（4 空格缩进、120 列、K&R 大括号、不对齐连续声明/赋值）。

## 编译与烧录

### 1. 硬件编译（ESP-IDF v6.0）

```bash
# 激活 ESP-IDF 环境
. ~/.espressif/v6.0.2/esp-idf/export.sh

# 编译（16MB flash 分区表 partitions/v2/16m.csv）
idf.py build

# 烧录与监视（默认串口 /dev/ttyACM2）
idf.py -p /dev/ttyACM2 flash monitor
```

### 2. 主机端单元测试（Linux）

测试独立于 ESP-IDF，直接编译运行，覆盖 2bpp 像素打包、布局、主题、字体、网络 JSON 解析、NVS 状态、UI 页面渲染冒烟与音频管线：

```bash
bash tests/run_tests.sh              # 全部 12 组
bash tests/run_tests.sh theme audio  # 指定组
```

## 开发与硬件移植注意事项

### 1. SSD2683 四色屏刷新机制
SSD2683 四色电泳墨水屏（黑/白/红/黄）由于其特有的物理波形，**不支持差分局部刷新**（使用局部刷新会导致严重的画面斜向错位与花屏）。刷新调度中识别为四色面板时必须强行将 `should_full` 设为 `true` 执行全屏刷新。

### 2. SPI DMA 缓冲区对齐
SPI DMA 传输的临时缓冲区（如 `line_buffer`）必须 4 字节对齐（`__attribute__((aligned(4)))`），否则控制器寻址偏移会导致传输数据整行移位或破损。

### 3. ES8311 编解码器 I2C 地址
使用 `esp_codec_dev` 配置 ES8311 时，`.addr` 必须填 **8 位读写地址 `0x30`**（7 位物理地址 `0x18` 左移 1 位），库内部会右移 1 位转换：
```c
audio_codec_i2c_cfg_t i2c_cfg = {
    .port = 0,
    .addr = 0x30, // 对应物理 I2C 地址 0x18
    .bus_handle = g_board.i2c_bus,
};
```
错误传入 `0x18` 会导致设备被识别为 `0x0C`，引发 `Fail to read/write from dev` 报错。

### 4. 深睡唤醒脚约束
ESP32-S3 仅 GPIO0-21 为 RTC GPIO；BOOT(GPIO0) 与 RTC_INT(GPIO5) 是仅有的合法 EXT1 唤醒源。唤醒脚需切到 RTC IO 模式并保持内部上拉（普通 `gpio_config()` 上拉在深睡中不保持）。
