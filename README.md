# 心氧智护 HealthMonitor

![心氧智护系统全景：健康监护终端、本地触控界面、网页端与远程提醒](docs/images/system-overview.jpg)

**ESP32-P4 + ESP32-C6 双芯协同的边缘健康监护固件。**

本仓库只整理并公开**两块芯片的固件源码与工程配置**，不包含演示视频、PPT、网页端代码、第三方库源码，也不包含任何真实密钥。它源自竞赛「作品重要代码」提交包，按可编译、可配置、可推送到代码仓库的标准重新组织。

---

## 目录

- [它能做什么 / 不能做什么](#它能做什么--不能做什么)
- [系统架构](#系统架构)
- [硬件与界面](#硬件与界面)
- [快速开始](#快速开始)
- [目录结构](#目录结构)
- [验证状态](#验证状态)
- [证据边界与已知限制](#证据边界与已知限制)
- [推送前检查](#推送前检查)

---

## 它能做什么 / 不能做什么

| 已在本仓库源码中实现 | 不在本仓库范围内 |
| --- | --- |
| MAX30102 红光/红外采集，心率与血氧的本地估算（`HeartRateCore` / `SpO2Core`） | 网页数据管理平台与云端上传/部署代码 |
| LVGL 界面、登录、实时检测、历史记录、AI 结果展示、状态栏、时间设置 | 演示视频、答辩 PPT、宣传素材 |
| P4 ↔ C6 的 UART 协议、状态轮询与时间同步 | 第三方库源码（LVGL、ESP32_Display_Panel、ArduinoJson 等） |
| C6 侧 Wi-Fi、NTP、云端 AI 请求、企业微信机器人推送 | 真实 Wi-Fi 密码、API Key、Webhook |
| 凭据缺失时的安全降级：跳过联网与推送，本地检测仍可用 | 本地模型推理、LittleFS 断电持久化（演示材料提及，固件源码未体现） |

一句话：**本地检测闭环在固件里；联网、AI 与推送需要你自己填 key；网页端不在本仓库。**

---

## 系统架构

![HealthMonitor 双芯系统架构图](docs/images/architecture.svg)

数据链路：

1. MAX30102 经 I2C 接入 ESP32-P4，`p4_vital_sensor.cpp` 采集原始信号，`HeartRateCore` / `SpO2Core` 在本地算出心率与血氧。
2. P4 用 LVGL 渲染界面并把近期记录保存在**内存数组**中；用户在历史页按需触发 AI 分析。
3. P4 通过 UART（115200，GPIO26/27 ↔ GPIO4/5）把请求交给 ESP32-C6。
4. C6 走 HTTPS 请求火山方舟 `doubao-seed-1.6-flash`，把摘要回传 P4，同时向企业微信机器人推送结果。
5. 四个外部凭据由 C6 端的 `secrets.h` 提供；全部留空时第 3、4 步的联网动作会被跳过，第 1、2 步照常工作。

图中虚线框（网页端）为演示材料中出现、但源码未随本仓库提交的部分，详见[证据边界](#证据边界与已知限制)。

---

## 硬件与界面

![硬件组件展示：ESP32-P4 主控板、ESP32-C6 联网模组、5.5 寸触摸显示模组、MAX30102 传感器、电源板与电池座](docs/images/hardware.jpg)

| 芯片 | 职责 | 关键源码 |
| --- | --- | --- |
| ESP32-P4 | 主控 / 显示 / 算法 / 交互 | `firmware/P4_Main/sketch_apr26a/` |
| ESP32-C6 | 联网 / AI 请求 / 消息推送 | `firmware/C6_Network/esp32c666/` |

![设备界面功能总览：设备与网络设置、健康监测中心、实时检测、历史记录与数据分析](docs/images/ui-screens.jpg)

> 引脚、I2C 地址、分区参数等只列出源码中可确认的常量，且**未与原理图核对**，接线请以实际硬件为准。详见 [docs/HARDWARE.md](docs/HARDWARE.md)。

---

## 快速开始

### 1. 准备环境

| 组件 | 要求 |
| --- | --- |
| arduino-cli | 1.5.1（或用 Arduino IDE 2.x 打开 `.ino`） |
| ESP32 Arduino Core | `esp32:esp32 3.1.1`（需支持 P4 与 C6） |
| 第三方库 | LVGL、ESP32_Display_Panel、ESP32_IO_Expander、esp-lib-utils、bb_captouch、ArduinoJson |

本仓库不附带第三方库。若本机已有 Arduino IDE 与旧工程库目录，可直接复用；否则按 [docs/BUILD.md](docs/BUILD.md) 安装。

### 2. 填写密钥（唯一需要改的地方）

仓库里有一个**专门的空 key 文件**：

```
firmware/C6_Network/esp32c666/secrets.h
```

它随仓库公开，**四个宏全部为空，禁止填真实值**。本地运行请在同一目录创建被忽略的本地文件：

```bash
cd firmware/C6_Network/esp32c666
cp secrets.local.h.example secrets.local.h
```

编辑 `secrets.local.h`：

| 宏 | 填什么 |
| --- | --- |
| `WIFI_SSID` | 2.4GHz Wi-Fi 名称 |
| `WIFI_PASSWORD` | Wi-Fi 密码 |
| `WECHAT_WEBHOOK_URL` | 企业微信机器人完整 Webhook URL（含 `key=`） |
| `ARK_API_KEY` | 火山方舟 API Key，**必须带 `Bearer ` 前缀** |

`secrets.h` 会自动包含存在的 `secrets.local.h` 并用本地值覆盖空默认值；`secrets.local.h` 已被 `.gitignore` 忽略。Arduino 构建只把 sketch 目录加入头文件搜索路径，所以这两个文件必须和 `esp32c666.ino` 同级。

> 四个值留空也能编译和启动，此时 `wifi_config_valid()`、`api_key_valid()`、`webhook_url_valid()` 会跳过联网、AI 与推送。详见 [config/README.md](config/README.md)。

### 3. 编译

```bash
bash tools/build.sh all          # P4 + C6
bash tools/build.sh p4           # 只编 P4
bash tools/build.sh c6           # 只编 C6
```

脚本默认读取本机的 CLI、core 与库路径，可用 `ARDUINO_CLI` / `ARDUINO_DATA` / `ARDUINO_LIBS` / `HM_BUILD_DIR` 覆盖。首次运行会用 Python 3 生成不含 `lvgl/src/test.ui` 的干净库副本（约 109 MiB，缓存在构建目录），需要本机有 `python`。完整手动命令、板卡参数与常见问题见 [docs/BUILD.md](docs/BUILD.md)。

### 4. 烧录与运行

两块芯片分别烧录，串口波特率 115200：

```bash
arduino-cli upload --fqbn esp32:esp32:esp32c6 --port COMx firmware/C6_Network/esp32c666
arduino-cli upload --fqbn "esp32:esp32:esp32p4:FlashSize=16M,PartitionScheme=custom,PSRAM=enabled" --port COMx firmware/P4_Main/sketch_apr26a
```

P4 需选 16MB Flash / PSRAM Enabled / Custom 分区（分区表 `partitions.csv` 在 sketch 目录内）。上电后先看串口 `[BOOT]` 日志，再检查 Wi-Fi 与 C6 状态；未填 key 时界面应仍能完成本地检测。

### 5. 推送前自检

```bash
python tools/check_secrets.py
```

脚本扫描将被提交的文件，发现疑似真实密钥时以非零码退出，只报告文件名与行号、不打印明文。

---

## 目录结构

```
HealthMonitor-ESP32-P4/
├── README.md                       # 本文件
├── LICENSE                         # MIT（团队另有约定可替换）
├── .gitignore / .gitattributes
├── firmware/
│   ├── P4_Main/sketch_apr26a/      # P4 主控：算法、LVGL UI、UART 协议、分区表
│   └── C6_Network/esp32c666/       # C6 联网：WiFi/NTP、AI、企微推送
│       ├── secrets.h               # 空 key 文件（提交）
│       ├── secrets.local.h.example # 本地模板（提交）
│       └── secrets.local.h         # 本地真值（不提交）
├── config/
│   ├── lv_conf.h                   # LVGL 配置
│   ├── esp_panel_drivers_conf.h    # 显示驱动配置
│   └── README.md                   # 配置说明
├── docs/
│   ├── BUILD.md                    # 编译、烧录、排错
│   ├── HARDWARE.md                 # 引脚与硬件常量（标注来源）
│   ├── images/                     # README 配图与架构图
│   └── legacy/                     # 原提交包的说明文件与文件清单
└── tools/
    ├── build.sh                    # 一键编译两个固件
    └── check_secrets.py            # 凭据自检
```

---

## 验证状态

2026-11-12 在本机（Windows / arduino-cli 1.5.1 / `esp32:esp32 3.1.1-cn`）对两个固件分别执行**真实编译**：

| 目标 | FQBN | 结果 | Flash 占用 | RAM 占用 |
| --- | --- | --- | --- | --- |
| ESP32-C6 | `esp32:esp32:esp32c6` | 通过（exit 0） | 1,114,929 B / 1,310,720 B（85%） | 43,712 B / 327,680 B（13%） |
| ESP32-P4 | `esp32:esp32:esp32p4:FlashSize=16M,PartitionScheme=custom,PSRAM=enabled` | 通过（exit 0） | 3,328,699 B / 16,777,216 B（19%） | 51,012 B / 327,680 B（15%） |

日志位于仓库外 `C:/Temp/hm-build/logs/`（`c6-clean.log`、`p4-clean.log`），不进入版本库。复现命令：`bash tools/build.sh all`。

验证中排除的两个问题，**均非本仓库源码缺陷**：

1. 构建产物放在含中文的仓库路径下时，Windows 工具链 assembler 报 `can't create ... .o`；改用纯 ASCII 构建目录后通过。
2. 旧工程库目录 `lvgl/src/test.ui/` 含 63 个 UI 源码副本，与 sketch 自带 `src/ui/` 同名函数在链接期重复定义；剔除该目录后通过。脚本已自动处理，详见 [docs/BUILD.md](docs/BUILD.md)。

**未验证**：未烧录实机、未接传感器、未调用云端 AI / 企业微信接口；`secrets.local.h` 未创建，四个凭据保持为空。编译通过不等于功能正确或性能指标成立。

---

## 证据边界与已知限制

1. **源码来源**：本仓库由竞赛提交包 `ESP32-P4端AI医疗设备_重要代码`（118 个文件、41MB）整理而来。对照 `docs/legacy/文件清单.txt` 的 117 条 SHA256：111 条逐字节一致，3 条（`esp32c666.ino`、`volcano_ai.cpp`、`wechat_push.cpp`）为凭据抽离的预期改动，另 3 条根目录说明文件归档到 `docs/legacy/` 后内容未变。改动仅 3 个 C6 源文件共 8 处（删除 4 处硬编码常量与 1 处失效注释、新增 2 处 `#include`、调整 1 处占位检查），**其余源码字节未改动**。
2. **编译不等于可用**：编译通过只说明语法与链接在当前工具链下成立；本仓库未做实机烧录、传感器在环验证或云端 API 实调。
3. **版本对应缺口**：项目过程记录（`总任务总纲.md` / `问题留存.md`）明确记载「网页上传与部署代码尚未和当前提交包对应」。本仓库不包含网页端，**不能据此复现演示中的云端同步与三端同记录**。
4. **能力边界**：近期历史为内存数组（断电不保存）；断网可用性、跨端同记录、测量误差缺原始测试记录；宣传图中「本地模型推理、LittleFS 持久化」等能力未在本仓库固件中体现，不作为本仓库的能力声明。
5. **硬件参数**：`HARDWARE.md` 中的引脚与地址来自源码常量，未与原理图、数据手册核对；供电、电流、屏幕型号等未在源码中给出，故不列。
6. **配图来源**：`docs/images/` 中的三张 JPG 来自项目演示素材，压缩后用于文档说明；`architecture.svg` 依据本仓库源码绘制。图片版权归原作者，第三方库各自遵守其原始许可证。

---

## 推送前检查

- [ ] `python tools/check_secrets.py` 返回 `[OK]`
- [ ] `secrets.local.h` 已被忽略；构建产物默认在仓库外的 `C:/Temp/HealthMonitor-build/`，不出现在 `git status`
- [ ] 确认 `LICENSE` 与团队约定一致（赛题 4.3 对开源协议不限）
- [ ] 确认 Git 提交身份：本机全局配置为学校邮箱，公开提交会带上该地址
- [ ] 确认配图与文档中不含学校名称、指导老师信息（赛事对报告/视频有此要求）
- [ ] 仓库可见性、名称、远程地址由你确认后再执行 `git init` / `git push`；**本仓库目前尚未初始化 Git，也未推送**

## 许可

[MIT](LICENSE)。第三方库不在本仓库内，各自遵守原许可证。
