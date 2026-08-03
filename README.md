# ZecTrix 4.2寸 BWRY 四色电子纸显示屏 C语言固件

本项目是将原 C++ 版本的四色（黑/白/红/黄）电子纸显示屏固件移植到纯 C 语言的 ESP-IDF 工程（适用于 ESP32-S3 芯片）。该固件去除了对 LVGL 界面框架的重度依赖，使用轻量化的 `rawdraw` 2D 图形库直接操作 2bpp 帧缓冲区，并集成了完整的电源轨管理、I2C 外设控制、电池充电状态监控和按键交互。

## 项目特点

* 纯 C 语言实现：消除 C++ 复杂性，适合嵌入式微控制器，提高代码执行效率和控制精度。
* 轻量化 Rawdraw 引擎：支持直接在 2bpp 帧缓冲区（00=黑，01=白，10=黄，11=红）绘制矩形、50% 棋盘格抖动区域、圆角边框矩形以及 UTF-8 文本编码。
* 硬件级外设接口移植：
  * PCF8563 RTC：实现时间配置、时钟读取、闹钟和倒计时硬件定时中断（I2C）。
  * GT23SC6699 NFC：支持芯片硬复位时序、UID 识别及 NDEF URI/文本数据读写。
  * ES8311 Audio & I2S：基于 Espressif 官方 `esp_codec_dev` 接口直接实现 C 语言配置，支持 I2S 16-bit 双通道数据渲染及硬件正弦音调生成。
  * Wi-Fi Station 模块：集成 standard STA 模式自动重连，连接成功后自动刷新 IP 到显示区域。
  * NVS Settings：封装 NVS 命名空间隔离存储接口，支持 Wi-Fi 等系统配置载入与保存。
* 高效刷新调度：基于双帧缓冲区对比算法（Popcount 差异位统计），自动判断并选择全局刷新（Full Refresh）或局域差分刷新（Partial Refresh）。
* 电池充电状态跟踪：采用双引脚去抖动状态机逻辑，支持 thread-safe 的状态快照获取，用于背景 LED 任务状态指示。
* 本地主机单元测试：图形 and 位打包算法与 ESP-IDF 硬件层解耦，支持在 Host Linux 环境下直接使用 `gcc` 进行快速验证。

## 项目结构

```
zetrix_epd_panel/
├── CMakeLists.txt              # 项目根 CMake 配置文件
├── sdkconfig                   # 编译配置文件
├── main/
│   ├── CMakeLists.txt          # 组件编译配置，管理依赖项（driver, lvgl, esp_codec_dev等）
│   ├── idf_component.yml       # ESP 组件依赖管理器（拉取 lvgl, button, esp_codec_dev）
│   ├── Kconfig.projbuild       # 固件菜单配置选项
│   ├── config.h                # 硬件引脚分配及底层参数定义
│   ├── board.h / .c            # 电源轨配置、I2C 互斥锁及设备注册、背景 LED 指示任务
│   ├── rtc_pcf8563.h / .c      # PCF8563 芯片 RTC 时钟、闹钟和定时器操作
│   ├── zectrix_nfc.h / .c      # GT23SC6699 芯片 NFC 通信、数据读写和 NDEF 解析封装
│   ├── settings.h / .c         # NVS 命名空间配置存取接口
│   ├── audio_player.h / .c     # ES8311 编解码器初始化及 I2S 正弦音调播放
│   ├── wifi_manager.h / .c     # Wi-Fi Station 模式连接管理器与回调通知
│   ├── charge_status.h / .c    # 电池充电引脚去抖与状态快照逻辑
│   ├── sleep_manager.h / .c    # 睡眠管理器状态投票（Display/Audio/Net等）
│   ├── custom_lcd_display.h / .c # SSD2683 SPI 驱动与背景异步刷新调度器
│   ├── font_engine.h           # UTF-8 解析器与 LVGL 字体结构兼容垫片
│   ├── rawdraw.h / .c          # 2bpp 位图打包、基本几何图形和文本绘制原语
│   └── main.c                  # 主程序入口，按键配置（UP/DOWN/BOOT）与 3 页演示循环
├── components/
│   └── 78__xiaozhi-fonts       # 外部预编译字体组件链接（SourceHanSans、天气、图标）
└── tests/
    └── test_host.c             # 主机端单元测试源码
```

## 交互演示设计

固件通过注册 `iot_button` 按键回调，支持在三个演示页面之间切换：
* **Page 1（系统信息）**：显示红色的圆角标题条幅，配合黄色圆角边框显示当前电池充电状态，并动态输出当前连接的 Wi-Fi SSID、本地分配的 IP 地址和 NFC 读卡器的场信号状态（Has Field）。
* **Page 2（四色色板）**：展示黑、红、黄、白四个纯色色块，并绘制一块使用 50% 棋盘格抖动（Checkerboard）生成的灰色渐变对比区域。
* **Page 3（RTC 时钟）**：显示系统启动时与 PCF8563 硬件同步过的系统时间，并支持 1s 周期刷新时钟，以及显示手动触发 EPD 全局刷新清除残影的提示信息。

按键与音频反馈定义：
* **UP 按键 (GPIO 39)**：向前翻页，并播放 400Hz 蜂鸣器按键音（80ms）。
* **DOWN 按键 (GPIO 18)**：向后翻页，并播放 600Hz 蜂鸣器按键音（80ms）。
* **BOOT 按键 (GPIO 0)**：强制触发屏幕全局刷新，并播放双音调反馈音（800Hz 后跟 1000Hz 组合）。

---

## 编译与烧录

### 1. 硬件编译 (ESP-IDF)
首先确保你的终端已正确配置了 ESP-IDF v6.0/v6.1 的开发环境。

```bash
# 激活 ESP-IDF 环境变量
. ~/.espressif/v6.0.2/esp-idf/export.sh

# 清理并开始编译项目
idf.py fullclean
idf.py build


# 烧录到开发板 (默认串口使用 /dev/ttyACM2)
idf.py -p /dev/ttyACM2 flash

# 使用官方 idf_monitor.py 工具进行控制台监视与堆栈解析 (免 PTY 占用冲突)
'/home/wert/.espressif/tools/python/v6.0.2/venv/bin/python3' '/home/wert/.espressif/v6.0.2/esp-idf/tools/idf_monitor.py' -p /dev/ttyACM2 -b 115200 --toolchain-prefix xtensa-esp32s3-elf- --make "'/home/wert/.espressif/tools/python/v6.0.2/venv/bin/python3' '/home/wert/.espressif/v6.0.2/esp-idf/tools/idf.py'" --target esp32s3 '/home/wert/GitHub/zetrix_epd_panel/build/zetrix_epd_panel.elf'

### 2. 主机端单元测试 (Host Linux)
测试文件独立于 ESP-IDF，可以直接在本地 Linux 系统上编译 and 运行，用于检验底层 2bpp 像素打包、圆角矩形坐标限制、Popcount 差异对比以及差分像素交错的数学正确性：

```bash
# 编译本地测试程序
gcc -o test_host tests/test_host.c main/rawdraw.c

# 运行测试
./test_host
```

输出期望：
```
Starting Host Verification Tests...
Running test_pixel_packing...
test_pixel_packing passed!
Running test_dither_pattern...
test_dither_pattern passed!
Running test_frame_diff...
test_frame_diff passed!
Running test_partial_encoding...
test_partial_encoding passed!
All verification tests passed successfully!
```

## 开发与硬件移植注意事项

### 1. SSD2683 四色屏刷新机制
SSD2683 四色电泳墨水屏（黑/白/红/黄）由于其特有的物理波形，**不支持差分局部刷新**（使用局部刷新会导致严重的画面斜向错位与花屏）。在刷新调度逻辑中，凡是识别为四色面板时，必须强行将 `should_full` 设为 `true` 以执行全屏刷新流程。

### 2. SPI DMA 缓冲区对齐
若需要在局部或全局刷新中传输临时数据（如 `line_buffer`），请务必保证用于 SPI DMA 传输的缓冲区地址是 4 字节对齐的（使用 `__attribute__((aligned(4)))` 属性修饰），否则可能因控制器寻址偏移导致传输数据整行移位或破损。

### 3. ES8311 编解码器 I2C 地址
当使用 Espressif 官方的 `esp_codec_dev` 多媒体外设驱动框架配置 ES8311 编解码芯片时，配置结构体 `.addr` 参数必须填写 **8 位读写地址格式 `0x30`**（即标准的 7 位物理地址 `0x18` 左移 1 位），因为库文件在配置驱动器时，会自动做右移 1 位（`addr >> 1`）的转换以匹配物理端口：
```c
audio_codec_i2c_cfg_t i2c_cfg = {
    .port = 0,
    .addr = 0x30, // 对应物理 I2C 地址 0x18
    .bus_handle = g_board.i2c_bus,
};
```
若在此处错误传入 7 位物理地址 `0x18`，会导致配置设备被总线识别为 `0x0C`，从而引发 `Fail to read/write from dev` 音频链路报错崩溃。
