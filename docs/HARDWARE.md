# 硬件与连接

> 本文只列出**能从本仓库固件源码直接读出**的参数，并标明来源文件。
> 这些是源码常量，**未与原理图、数据手册逐项核对，不能作为接线或选型依据**。
> 供电电压、电流、屏幕型号、传感器 SDA/SCL 引脚等未在源码中给出，此处不列，需查原理图与数据手册。

## 双芯分工

| 芯片 | 角色 | 源码位置 |
| --- | --- | --- |
| ESP32-P4 | 主控：屏幕驱动、LVGL 界面、MAX30102 采集、心率/血氧算法、历史记录、与 C6 的 UART 协议 | `firmware/P4_Main/sketch_apr26a/` |
| ESP32-C6 | 网络协处理：Wi-Fi 连接、NTP 校时、云端 AI 请求、企业微信推送 | `firmware/C6_Network/esp32c666/` |

## 芯片间 UART

| 项目 | P4 侧 | C6 侧 | 来源 |
| --- | --- | --- | --- |
| RX | GPIO27（U1RXD，接 C6 TX） | GPIO4（LP_UART_RXD，接 P4 TX） | `c6_network.cpp:16`、`esp32c666.ino:12` |
| TX | GPIO26（U1TXD，接 C6 RX） | GPIO5（LP_UART_TXD，接 P4 RX） | `c6_network.cpp:17`、`esp32c666.ino:13` |
| 波特率 | 115200 | 115200 | `c6_network.cpp:18`、`esp32c666.ino:14` |

交叉接法：P4 TX ↔ C6 RX，P4 RX ↔ C6 TX，两端共地。

## 外设相关常量

| 项目 | 值 | 来源 |
| --- | --- | --- |
| MAX30102 I2C 地址 | `0x57` | `p4_vital_sensor.cpp:12` |
| 触摸屏 I2C 地址 | `0x14` | `lvgl_mipilcd_port.h:42` |
| LCD 复位引脚 | GPIO52 | `mipilcd.h:44` |
| LCD 背光引脚 | GPIO53 | `mipilcd.h:45` |
| 分区表 | `partitions.csv`（自定义） | `firmware/P4_Main/sketch_apr26a/partitions.csv` |

## P4 编译目标参数

来自原工程构建记录（`.codex-build/build.options.json`，非本仓库提交内容）：

```
esp32:esp32:esp32p4:FlashSize=16M,PartitionScheme=custom,PSRAM=enabled
```

## 依赖库

本仓库**不附带第三方库源码**。原提交包 `依赖库说明.txt` 列出：

- ESP32 Arduino Core（需支持 P4 与 C6）
- LVGL
- ESP32_Display_Panel
- ESP32_IO_Exander / esp-lib-utils / bb_captouch（屏幕、触摸、扩展 IO）
- ArduinoJson

LVGL 与显示驱动的配置头文件在 `config/lv_conf.h`、`config/esp_panel_drivers_conf.h`，放置方式见 [BUILD.md](BUILD.md)。

## 待核对

- 传感器与屏幕的实际接线（SDA/SCL、MIPI 排线序）、供电与电平：源码未给出。
- 板卡具体型号、屏幕分辨率与触摸芯片型号：源码未给出。
- 上述引脚常量是否与完赛硬件版本一致：项目记录显示网页上传/部署代码尚未与提交包对应，硬件版本亦未单独核验。
