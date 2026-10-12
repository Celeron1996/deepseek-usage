# deepseek-usage — DeepSeek 余额显示屏（ESP32-S2 + LVGL 9.6）

运行在 ESP32-S2 + 1.54" 240x240 SPI 屏幕硬件上的 DeepSeek 余额显示器：

- 首次开机进入**配网模式**：设备开启免密码热点和配置网页（带 Captive Portal 自动弹窗），
  手机填写 Wi-Fi 名称/密码/DeepSeek API Key 后自动连接；
- 连接成功后屏幕显示 `https://api.deepseek.com/user/balance` 查询到的余额，每 60 秒自动刷新；
- 联网后自动 NTP 对时，屏幕右上角显示时间（东八区）；
- 运行期间配置网页始终保留（显示在屏幕底部），随时可重新配置；Wi-Fi 掉线后自动无限重连。

---

## 1. 硬件资源与引脚映射

| 资源 | 说明 |
| --- | --- |
| MCU | ESP32-S2（实测芯片 ESP32-S2R2，Xtensa 单核 240 MHz；带 2MB 内置 PSRAM，本工程未启用） |
| Flash | 4MB，DIO 模式，80MHz |
| 屏幕 | 1.54 英寸 240x240 SPI 液晶，控制器 ST7789（无触屏） |
| 按键 | 1 个机械按键，低电平有效（按下接地） |

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

| 信号 | GPIO | 功能 |
| --- | --- | --- |
| 按键 | GPIO1（内部上拉，下降沿中断） | 短按：立即刷新余额；长按 5 秒：清除全部配置并重启（恢复出厂，重新进入配网模式） |

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

### PowerShell 环境激活（每次新开终端必做）

本机 PowerShell 默认禁止运行脚本，且 `export.ps1` 默认会去找不存在的 py3.13 环境：

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
$env:IDF_TOOLS_PATH = 'D:\espressif\esp-tools'
$env:IDF_PYTHON_ENV_PATH = 'D:\espressif\esp-tools\python_env\idf5.5_py3.11_env'
. 'D:\espressif\esp-idf\v5.5.1\esp-idf\export.ps1'
```

---

## 3. 编译、烧录与监视

```powershell
# 仅首次或换芯片时需要（按 sdkconfig.defaults 生成 sdkconfig 并下载组件）
idf.py set-target esp32s2

idf.py build
idf.py -p COM14 flash
idf.py -p COM14 monitor        # 退出监视：Ctrl + ]
```

- 组件依赖：`lvgl/lvgl ~9.6.0`、`espressif/esp_lvgl_port ^2.9.0`，首次编译需联网从
  components.espressif.com 下载（缓存于 `managed_components/`）。
- 应用约 1.6MB，位于 3MB 的 factory 分区，剩余约 49%。

---

## 4. 首次使用（配网流程）

1. 上电后设备检测到未配置 Wi-Fi，进入**界面 1（配网引导）**：
   开启免密码热点 `DeepSeek-XXXX`（XXXX 为 MAC 后两字节），屏幕显示热点名、
   配置网址 `http://192.168.4.1` 和操作说明。
2. 手机连接该热点。由于内置 DNS 劫持（Captive Portal），连接后一般会自动弹出配置页；
   若未弹出，手动用浏览器打开 `http://192.168.4.1`。
3. 网页上填写 **Wi-Fi 名称、Wi-Fi 密码、DeepSeek API Key**，点"确认并连接"：
   - 服务端会先校验格式（SSID 1-32 字节；密码留空或 8-63 位；API Key 非空且无空格）；
   - 校验失败：网页显示原因，同时设备屏幕切到**界面 2** 显示"格式有误"约 3 秒后回到界面 1；
4. 校验通过后设备进入**界面 2（连接中）**：环形加载动画 + 实时状态
   （正在连接/重试次数等）。最多尝试 3 次，每次超时 15 秒：
   - 成功：退出热点，进入**界面 3（余额界面）**；
   - 失败：显示"无法连接，请重新配置"，3 秒后返回界面 1（热点保持开启，可直接重试）。
5. **界面 3**：显示余额（如 `CNY 23.43`）、Wi-Fi 状态、接口状态，右上角时间；底部小字显示
   配置网址 `config: http://<设备IP>`。余额每 60 秒刷新，短按按键可立即刷新。

已配网设备重新上电：跳过界面 1/热点，直接进入界面 2 连接；若多次连接失败，
**仍进入界面 3** 并持续后台重连（界面显示 Wi-Fi 异常），不会丢失配置。

---

## 5. 运行逻辑（状态机）

```
上电
 ├─ NVS 无 Wi-Fi 配置 → 界面1(AP+网页) → 收到配置 → 界面2 ──成功→ 界面3
 │                                   ↑            └─失败/格式错误→ 回界面1
 └─ NVS 有 Wi-Fi 配置 → 界面2 ──成功→ 界面3
                            └─失败→ 界面3(显示 Wi-Fi 异常，持续重连)

界面3：保持 web 服务；断网/掉线 → 每 5 秒重连一次，直到成功；
       Wi-Fi 恢复后自动重启 NTP 与余额刷新；点击网页"确认" → 回界面2 重配。
```

- 界面 2/3 下网页始终可用（配网时经热点 `192.168.4.1`，联网后经设备 STA IP）。
- 余额状态区分：正常余额、`API Key 无效或已过期`（HTTP 401/403）、
  `网络异常，正在重试`（连接/TLS 失败）、`接口异常 (HTTP xxx)`、`数据解析失败`。
- Wi-Fi 恢复后自动补发 NTP 同步和余额刷新（无需重启）。

### NVS 存储（掉电保存）

命名空间 `dsk`：`ssid`、`pass`、`key`（API Key）。长按按键 5 秒清空该命名空间并重启。

### DeepSeek 接口

```
GET https://api.deepseek.com/user/balance
Header: Authorization: Bearer <API Key>
响应示例:
  {"is_available":true,"balance_infos":[{"currency":"CNY","total_balance":"23.43",
   "granted_balance":"0.00","topped_up_balance":"23.43"}]}
```

使用 `esp_http_client` + `esp_crt_bundle`（内置根证书）做 TLS 校验，`cJSON` 解析，
`balance_infos[0]` 的 `currency`/`total_balance` 显示为 `CNY 23.43` 形式。

---

## 6. 关键配置说明（排错必读）

`sdkconfig.defaults` 中的重点项：

```ini
CONFIG_IDF_TARGET="esp32s2"
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
CONFIG_PARTITION_TABLE_CUSTOM=y            # partitions.csv，factory 3MB

# LVGL：英文界面 + 备用中文字库 + 改用 IDF 堆
CONFIG_LV_COLOR_DEPTH_16=y
CONFIG_LV_THEME_DEFAULT_DARK=y
CONFIG_LV_FONT_SOURCE_HAN_SANS_SC_16_CJK=y # 仅用于显示可能含中文的 Wi-Fi SSID，界面文字全英文
CONFIG_LV_FONT_MONTSERRAT_10/12/14/28=y
CONFIG_LV_USE_CLIB_MALLOC=y                # 必须！LVGL 内置分配器占 64KB 静态 DRAM 会链接失败
CONFIG_LV_USE_CLIB_STRING=y
CONFIG_LV_USE_CLIB_SPRINTF=y

# TLS/NTP
CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=8192     # HTTPS 收发缓冲尽量小（ESP32-S2 内存紧张）
CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN=2048
CONFIG_LWIP_SNTP_MAX_SERVERS=2             # ntp.aliyun.com + pool.ntp.org
```

> 修改 `sdkconfig.defaults` 后需删除 `sdkconfig` 重新 `idf.py build`（只在首次生成时读取）。

### 内存注意事项（重要）

ESP32-S2 无 PSRAM 启用时 DRAM 很紧。LVGL 绘制缓冲已从 240x40 降到 **240x24 双缓冲**
（`main/display.c` 的 `LCD_DRAW_BUF_LINES`），TLS 缓冲也调小，为 mbedtls 握手腾出内存；
否则会出现 `mbedtls_ssl_setup returned -0x7F00`（MBEDTLS_ERR_SSL_ALLOC_FAILED），
表现为余额一直"网络异常"，而 DNS/TCP 探测正常。若后续功能增加导致 HTTPS 再次失败，
优先检查此处内存占用。

### 屏幕显示异常时的调整点（`main/display.c`）

| 现象 | 修改 |
| --- | --- |
| 画面整体下移约 1/3（80px） | `LCD_Y_GAP` 改为 `0`（当前默认值） |
| 画面整体上移约 1/3，底部空白 | `LCD_Y_GAP` 改为 `80` |
| 颜色整体反相 | `esp_lcd_panel_invert_color(s_panel, true/false)` 切换 |
| 红蓝互换 | `LCD_RGB_ELEMENT_ORDER_BGR` 与 `RGB` 互换 |
| 上下/左右镜像 | `esp_lcd_panel_mirror()` / `esp_lcd_panel_swap_xy()` |

### HTTPS 失败排查顺序

1. 串口是否出现 `mbedtls_ssl_setup ... -0x7F00` → 内存不足（见上）；
2. 是否出现 `dns ... failed` → DNS 问题（检查路由器 DHCP/DNS）；
3. 是否出现 `tcp probe ... FAILED` → 该网络无外网或防火墙拦截 443；
4. HTTP 401/403 → API Key 错误（界面 3 会显示"API Key 无效或已过期"）；
5. 首次开机可能先显示"正在同步时间..."——TLS 需要正确时钟，NTP 同步后自动继续。

---

## 7. 工程结构与职责

```
deepseek-usage/
├── CMakeLists.txt          # 顶层 CMake，project(deepseek-usage)
├── partitions.csv          # nvs / phy_init / factory(3MB)
├── sdkconfig.defaults      # 目标芯片、Flash、LVGL/TLS/NTP 关键配置
├── dependencies.lock       # 组件版本锁定（自动生成，建议保留）
├── managed_components/     # LVGL / esp_lvgl_port（组件管理器自动下载，不提交）
└── main/
    ├── CMakeLists.txt
    ├── idf_component.yml   # 组件依赖声明
    ├── app.h               # 事件类型与应用队列定义
    ├── main.c              # app_main、三界面状态机、按键任务、余额任务
    ├── display.c/.h        # SPI 总线 + ST7789(esp_lcd) + esp_lvgl_port
    ├── ui.c/.h             # 界面1/2/3 与时钟刷新（LVGL 9 flex 布局，界面文字全英文）
    ├── wifi_mgr.c/.h       # STA/APSTA 管理、事件上报、IP 查询
    ├── webserver.c/.h      # 配置网页、表单解析校验、Captive Portal DNS 劫持
    ├── config_store.c/.h   # NVS 读写、格式校验、恢复出厂
    ├── balance.c/.h        # DeepSeek 余额 HTTPS 请求 + cJSON 解析 + 网络诊断
    ├── ntp_time.c/.h       # SNTP（东八区）+ 时钟有效性判断
    └── button.c/.h         # GPIO1 按键：短按/长按检测
```

---

## 8. 实测验证记录（本机 COM14）

- 首次上电：`softap configured: ssid=DeepSeek-XXXX (open)`，网页 `GET /` 返回 200；
- Captive Portal：`nslookup example.com 192.168.4.1` 返回 192.168.4.1（DNS 劫持生效）；
- 表单校验：密码过短返回错误页并在设备屏幕提示；合法配置保存后自动连接；
- 已配置重启：跳过热点直连，`sta ip: 192.168.1.18` → 显示余额 `CNY 23.43`，60 秒周期刷新；
- 运行中网页保留：`curl http://192.168.1.18/` 返回 200；
- NTP：`time synced: 2026-10-13 00:33:18`，界面右上角时钟正常走动。
