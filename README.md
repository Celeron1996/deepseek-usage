# deepseek-usage — ESP32-S2 Wi-Fi 扫描小程序（LVGL 9.6）

运行在 ESP32-S2 + 1.54" 240x240 SPI 屏幕硬件上的演示工程：
开机扫描附近 Wi-Fi 热点，用 LVGL 列表组件在屏幕上显示 SSID 和信号强度（dBm）。
按下板上机械按键可立即重新扫描，无操作时每 60 秒自动重新扫描一次。

---

## 1. 硬件资源与引脚映射

| 资源 | 说明 |
| --- | --- |
| MCU | ESP32-S2（实测芯片为 ESP32-S2R2，Xtensa 单核 240 MHz；带 2MB 内置 PSRAM，本工程未启用） |
| Flash | 4MB，DIO 模式，80MHz |
| 屏幕 | 1.54 英寸 240x240 SPI 液晶，控制器 ST7789（无触屏） |
| 按键 | 1 个机械按键，低电平有效（按下接地），未使用外部中断芯片 |

### 屏幕（SPI）引脚

| 信号 | GPIO |
| --- | --- |
| SCLK | GPIO15 |
| MOSI | GPIO9 |
| CS | GPIO11 |
| DC | GPIO13 |
| RST | GPIO16 |
| BLK（背光） | 硬件常亮，未接 GPIO |

> 引脚定义在 `main/display.c` 顶部（`LCD_GPIO_*` 宏），SPI 使用 `SPI2_HOST`，时钟 40 MHz。

### 按键引脚

| 信号 | GPIO |
| --- | --- |
| 按键 | GPIO1（内部上拉使能，下降沿中断，软件消抖 30ms） |

> 定义在 `main/button.c`（`BUTTON_GPIO` 宏）。

---

## 2. 开发环境（本机已部署）

| 项目 | 值 |
| --- | --- |
| ESP-IDF | v5.5.1 |
| IDF 安装路径 | `D:\espressif\esp-idf\v5.5.1\esp-idf` |
| IDF_TOOLS_PATH | `D:\espressif\esp-tools` |
| Python 虚拟环境 | `D:\espressif\esp-tools\python_env\idf5.5_py3.11_env` |
| 工具链 | xtensa-esp-elf 14.2.0（IDF 自带） |
| 串口 | COM14（USB 转串口，烧录与日志共用） |

### PowerShell 中的环境激活（重要）

本机 PowerShell 默认禁止运行脚本，且 `export.ps1` 默认会去找不存在的 py3.13 环境，
因此每次打开新终端都要按下面的顺序执行（缺一不可）：

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
$env:IDF_TOOLS_PATH = 'D:\espressif\esp-tools'
$env:IDF_PYTHON_ENV_PATH = 'D:\espressif\esp-tools\python_env\idf5.5_py3.11_env'
. 'D:\espressif\esp-idf\v5.5.1\esp-idf\export.ps1'
```

看到 `ESP-IDF v5.5.1` 输出后即可使用 `idf.py`。

---

## 3. 编译、烧录与监视

```powershell
# 仅首次或换芯片时需要（会按 sdkconfig.defaults 生成 sdkconfig 并下载依赖组件）
idf.py set-target esp32s2

idf.py build
idf.py -p COM14 flash
idf.py -p COM14 monitor        # 退出监视：Ctrl + ]
```

联网要求：首次 `build` 会从 components.espressif.com 下载 LVGL 等组件到
`managed_components/`，需要网络；之后离线可编译（`dependencies.lock` 已锁定版本）。

烧录参数：460800 波特率，`DIO` 模式，4MB；应用约 1.1MB，位于 3MB 的 factory 分区。

### 预期串口日志（正常启动）

```
I display: init SPI bus (MOSI=9 SCLK=15 CS=11 DC=13 RST=16)
I LVGL: Starting LVGL task
I display: LCD ready (240x240 @ 40 MHz SPI)
I button: button ready on GPIO1 (active low)
I wifi_scan: Wi-Fi station started
I wifi_demo: scanning...
I wifi_scan: scan done, 8 APs found
I wifi_demo: auto rescan
I wifi_scan: scan done, 14 APs found
```

---

## 4. 软件依赖（组件管理器自动处理）

在 `main/idf_component.yml` 中声明：

| 组件 | 版本约束 | 实际解析 |
| --- | --- | --- |
| `idf` | >=5.5 | 5.5.1 |
| `lvgl/lvgl` | ~9.6.0 | 9.6.0~1（当前最新 LVGL） |
| `espressif/esp_lvgl_port` | ^2.9.0 | 2.9.0 |

LVGL 图形库与显示/任务移植层均由组件管理器下载，无需手工拷贝任何组件。

---

## 5. 工程结构与职责

```
deepseek-usage/
├── CMakeLists.txt          # 顶层 CMake，project(deepseek-usage)
├── partitions.csv          # nvs / phy_init / factory(3MB)
├── sdkconfig.defaults      # 目标芯片、Flash、LVGL 关键配置（见下）
├── dependencies.lock       # 组件版本锁定（自动生成，建议保留）
├── managed_components/     # 组件管理器下载的 LVGL / esp_lvgl_port（自动生成）
└── main/
    ├── CMakeLists.txt
    ├── idf_component.yml   # 组件依赖声明
    ├── main.c              # app_main：初始化顺序、扫描任务、按键重扫/定时重扫
    ├── display.c/.h        # SPI 总线 + ST7789(esp_lcd) + esp_lvgl_port 移植
    ├── wifi_scan.c/.h      # Wi-Fi STA 初始化 + 阻塞式全信道扫描
    ├── ui.c/.h             # LVGL 界面：标题栏+扫描动画、AP 列表、状态栏
    └── button.c/.h         # GPIO1 按键中断 + 信号量 + 消抖
```

---

## 6. 关键配置说明（排错必读）

### sdkconfig.defaults 中的重点项

```ini
CONFIG_IDF_TARGET="esp32s2"
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
CONFIG_PARTITION_TABLE_CUSTOM=y            # 使用 partitions.csv
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y

CONFIG_LV_COLOR_DEPTH_16=y
CONFIG_LV_THEME_DEFAULT_DARK=y
CONFIG_LV_FONT_MONTSERRAT_12=y
CONFIG_LV_FONT_MONTSERRAT_14=y

# 关键：LVGL 改用 IDF 堆分配，否则 LVGL 内置分配器的 64KB 静态 DRAM
# 会让 ESP32-S2（无 PSRAM 启用）链接时 dram0_0_seg 溢出
CONFIG_LV_USE_CLIB_MALLOC=y
CONFIG_LV_USE_CLIB_STRING=y
CONFIG_LV_USE_CLIB_SPRINTF=y
```

> 注意：修改 `sdkconfig.defaults` 后必须删除已有的 `sdkconfig` 再重新 `idf.py build`
> （仅在首次生成 sdkconfig 时读取 defaults），或在 `idf.py menuconfig` 里同步修改。

### 屏幕显示异常时的调整点（`main/display.c`）

| 现象 | 修改 |
| --- | --- |
| 画面整体下移约 1/3（80px） | `LCD_Y_GAP` 改为 `0`（当前默认值） |
| 画面整体上移约 1/3，底部有空白 | `LCD_Y_GAP` 改为 `80` |
| 颜色整体反相（黑变白） | 切换 `esp_lcd_panel_invert_color(s_panel, true/false)` |
| 红蓝通道互换 | `LCD_RGB_ELEMENT_ORDER_BGR` 与 `LCD_RGB_ELEMENT_ORDER_RGB` 互换 |
| 画面上下/左右镜像 | `esp_lcd_panel_mirror()` / `esp_lcd_panel_swap_xy()` |

当前配置：ST7789、40MHz、`LCD_X_GAP=0`、`LCD_Y_GAP=0`、INVON 开、BGR、RGB565 字节交换开。

### 内存与显示缓冲

`display.c` 使用双缓冲，每个缓冲 240x40 像素（240*40*2 = 19.2KB），DMA 内存，共约 38.4KB。

---

## 7. 运行逻辑

- `app_main` 顺序：NVS → 屏幕/LVGL 初始化 → 创建界面 → 按键初始化 → 启动扫描任务。
- 扫描任务（`main.c`）：初始化 Wi-Fi（STA 模式，只扫描不连接）→ 显示 "Scanning..." 和转圈动画
  → 阻塞扫描全部信道 → 清空并重建 LVGL 列表（SSID + RSSI dBm，驱动返回结果已按信号强度排序）
  → 等待按键（`button_wait_for_press`）或 60 秒超时（`AUTO_RESCAN_TIMEOUT_MS`）→ 重复。
- UI 是 LVGL 9.6 推荐的 flex 列布局（旧 `lv_list` 组件已被官方标记废弃，本工程未使用）。
- 界面文字为英文：LVGL 默认 Montserrat 字体不含中文字形。如需中文，可在
  `idf.py menuconfig → Component config → LVGL → Font usage` 中启用
  `LV_FONT_SIMSUN_16_CJK`，并在 `ui.c` 中用 `&lv_font_simsun_16_cjk` 替换字体引用。

---

## 8. 常用改动入口

| 想改什么 | 改哪里 |
| --- | --- |
| 屏幕/按键引脚 | `main/display.c` 顶部 `LCD_GPIO_*`；`main/button.c` `BUTTON_GPIO` |
| 自动重扫周期 | `main/main.c` `AUTO_RESCAN_TIMEOUT_MS`（毫秒） |
| SPI 速率 | `main/display.c` `LCD_PIXEL_CLOCK_HZ` |
| 扫描/列表刷新逻辑 | `main/wifi_scan.c`、`main/main.c` |
| 界面样式/布局 | `main/ui.c` |
