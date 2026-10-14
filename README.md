# deepseek-usage — DeepSeek 余额显示屏（ESP32-S2 + LVGL 9.6）

运行在 ESP32-S2 + 1.54" 240x240 SPI 屏幕硬件上的 DeepSeek 余额显示器：

- 首次开机进入**配网模式**：设备开启免密码热点和配置网页（带 Captive Portal 自动弹窗），
  手机填写 Wi-Fi 名称/密码/DeepSeek API Key 后自动连接；
- 连接成功后屏幕显示 `https://api.deepseek.com/user/balance` 查询到的余额，每 10 秒自动刷新；
- 联网后自动 NTP 对时，屏幕右上角显示时间（东八区）；
- 运行期间配置网页始终保留（显示在屏幕底部），随时可重新配置；Wi-Fi 掉线后自动无限重连。

---

## 1. 硬件资源与引脚映射

| 资源 | 说明 |
| --- | --- |
| MCU | ESP32-S2（实测芯片 ESP32-S2R2，Xtensa 单核 240 MHz；内置 2MB PSRAM，本工程已启用作为额外堆） |
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
- 应用约 2.9MB，位于 3MB 的 factory 分区，剩余约 80KB（其中预转换动画资源约占 1.7MB）。

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
5. **界面 3**：显示余额（如 `CNY 23.43`）、接口状态，右上角时间；时间下方有小型 Wi-Fi 状态图标
   （绿色 Wi-Fi = 已连接；灰色 Wi-Fi + 红色斜杠 = 未连接/重连中）；底部小字显示
   配置网址 `config: http://<设备IP>`。余额每 10 秒刷新，短按按键可立即刷新。
   余额下方是吉祥物动画，按余额变化自动切换：
   - 余额增加 → `deepseek-charge.gif`（收钱动画）
   - 余额减少 → `deepseek-expend.gif`
   - 其余情况（首次获取/无变化）→ `deepseek-normal.gif`

   `normal` 持续循环播放；`charge`/`expend` 为限定场景：每次触发循环播放 **4 遍**（约 4 秒）
   后冻结在末帧，并**立即触发一次余额复查**，由复查结果刷新动画（余额未变化则回到 normal）。
   余额变化时的顺序为：大图（240x240 静态帧）先**由小到大放大出现**，停留约 2 秒，再**由大到小
   缩小消失**；此期间余额字段保持旧值不变。大图消失的瞬间立即开始播放对应的限定场景，
   余额数字则在停顿 500ms 后以**向上平移动画**（增加）或**向下平移动画**（减少）滑入新值，
   最后回到界面 3。

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

# LVGL：英文界面 + 蓝白浅色主题 + 改用 IDF 堆
CONFIG_LV_COLOR_DEPTH_16=y
# CONFIG_LV_THEME_DEFAULT_DARK is not set     # 蓝白风格使用 LVGL 浅色主题
CONFIG_LV_FONT_MONTSERRAT_10/12/14/28=y
CONFIG_LV_USE_CLIB_MALLOC=y                # 必须！LVGL 内置分配器占 64KB 静态 DRAM 会链接失败
CONFIG_LV_USE_CLIB_STRING=y
CONFIG_LV_USE_CLIB_SPRINTF=y

# 内存：启用 2MB 内置 PSRAM，并让 mbedTLS 从通用堆分配
CONFIG_SPIRAM=y                            # ESP32-S2R2 内置 2MB PSRAM，40MHz
CONFIG_SPIRAM_SPEED_40M=y
CONFIG_MBEDTLS_DEFAULT_MEM_ALLOC=y         # 关键！默认 INTERNAL_MEM_ALLOC 会把 mbedTLS 限制在内部 RAM
# CONFIG_MBEDTLS_HARDWARE_MPI is not set   # DeepSeek 链含 RSA-4096 中间证书，用软件 MPI 更稳

# TLS/NTP
CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=8192     # HTTPS 收发缓冲尽量小（ESP32-S2 内存紧张）
CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN=2048
CONFIG_LWIP_SNTP_MAX_SERVERS=2             # ntp.aliyun.com + pool.ntp.org
```

> 修改 `sdkconfig.defaults` 后需删除 `sdkconfig` 重新 `idf.py build`（只在首次生成时读取）。

### 内存注意事项（重要，踩坑记录）

DeepSeek 的 CDN 证书链是 `leaf → TrustAsia(RSA-4096) → DigiCert G2`。在 TLS 握手做
证书验签时，mbedTLS 需要为 RSA-4096 运算分配若干块**连续内存**：

- 若保持 IDF 默认的 `CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC=y`，这些分配只能来自内部
  DRAM，一旦内部堆碎片化就会失败（`MBEDTLS_ERR_RSA_PUBLIC_FAILED + MPI_ALLOC_FAILED`，
  日志表现为 `mbedtls_ssl_handshake returned -0x2700` / `tls err=0x2700 flags=0x8`），
  余额查询会间歇性失败直到完全失败；
- 解决：`CONFIG_SPIRAM=y`（启用 2MB PSRAM）+ `CONFIG_MBEDTLS_DEFAULT_MEM_ALLOC=y`
  （允许 mbedTLS 从 PSRAM 取临时内存）+ 关闭硬件 MPI。

另外 LVGL 绘制缓冲为 **240x24 双缓冲**（`main/display.c` 的 `LCD_DRAW_BUF_LINES`），
TLS 缓冲也保持较小。应用整体约 2.9MB（含约 1.7MB 预转换动画资源），已接近 3MB 的
factory 分区上限，新增资源前请留意剩余空间。

### 屏幕显示异常时的调整点（`main/display.c`）

| 现象 | 修改 |
| --- | --- |
| 画面整体下移约 1/3（80px） | `LCD_Y_GAP` 改为 `0`（当前默认值） |
| 画面整体上移约 1/3，底部空白 | `LCD_Y_GAP` 改为 `80` |
| 颜色整体反相 | `esp_lcd_panel_invert_color(s_panel, true/false)` 切换 |
| 红蓝互换 | `LCD_RGB_ELEMENT_ORDER_BGR` 与 `RGB` 互换 |
| 上下/左右镜像 | `esp_lcd_panel_mirror()` / `esp_lcd_panel_swap_xy()` |

### HTTPS 失败排查顺序

1. 日志出现 `tls err=0x2700 flags=0x8` + `heap: ... largest_internal=...` → mbedTLS
   验签内存不足（检查 PSRAM 与 `MBEDTLS_DEFAULT_MEM_ALLOC` 配置，见上）；
2. 是否出现 `dns ... failed` → DNS 问题（检查路由器 DHCP/DNS）；
3. 是否出现 `tcp probe ... FAILED` → 该网络无外网或防火墙拦截 443；
4. HTTP 401/403 → API Key 错误（界面 3 会显示"API Key 无效或已过期"）；
5. 首次开机可能先显示"正在同步时间..."——TLS 需要正确时钟，NTP 同步后自动继续；
6. 已内置 `deepseek_ca.pem`（TrustAsia 中间证书 + DigiCert G2 根）作为固定 CA。
   若 DeepSeek 更换 CA（中间证书有效期至 2035），需更新该 PEM 并重新编译。

### 硬件历史问题：flash 坏块（已更换硬件，仅作记录）

早期一块开发板的 flash 在 **0x1FD000** 处存在损坏扇区（写入/擦除会挂住串口导致
烧录中断）。经确认这是**个别硬件缺陷**，并非工程或配置问题；更换模块后烧录、运行
均正常，当前应用（约 2.9MB）无需再规避该地址。

若以后遇到类似"烧录中途挂起"的现象，可参考排查：

- 用 `--no-stub`（ROM 加载器）+ **分块/4KB 小块**写入，定位是否有个别坏块；
- 确认为 flash 个体故障后，直接更换模块/芯片即可。

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
- 已配置重启：跳过热点直连，`sta ip: 192.168.1.18` → 显示余额，约 10 秒周期刷新；
- 运行中网页保留：`curl http://192.168.1.18/` 返回 200；
- NTP：`time synced: 2026-10-13 00:33:18`，界面右上角时钟正常走动；
- PSRAM：`Found 2MB PSRAM device / Adding pool of 2048K of PSRAM memory to heap allocator`；
- TLS：`pinned CA parse: ret=0`，余额持续刷新（`balance: CNY 20.26`，此前证书验签
  内存不足的问题已由 PSRAM + mbedTLS 通用堆分配解决，详见第 6 节）。
