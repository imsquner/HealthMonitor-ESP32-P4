ESP32-P4 端AI 医疗设备 - 重要代码说明

一、压缩包用途
本压缩包用于“作品重要代码”提交，保留本作品最终采用的 ESP32-P4 + ESP32-C6 双芯协同源码。
压缩包已去除废弃 ESP32-S3 代码、编译缓存、二进制产物、报告文档、照片素材和第三方库全集。

二、目录结构
1. P4_Main/sketch_apr26a
   ESP32-P4 主控端工程。负责 MIPI LCD 初始化、LVGL 页面、MAX30102 采集、心率血氧算法、历史统计、按键/触摸交互、与 C6 串口通信。

2. C6_Network/esp32c666
   ESP32-C6 网络协处理工程。负责 WiFi 连接、NTP 时间同步、AI HTTP 请求、企业微信机器人推送、与 P4 的 UART 协议交互。

3. Library_Config
   与本工程相关的库配置文件，例如 lv_conf.h、esp_panel_drivers_conf.h。

4. Project_Config
   工程级辅助配置，例如 arduino-cli.yaml 和原 README。

三、关键源码对应关系
- P4_Main/sketch_apr26a/sketch_apr26a.ino：P4 端主入口、初始化流程和主循环。
- P4_Main/sketch_apr26a/p4_vital_sensor.cpp/.h：MAX30102 传感器采集与 I2C 访问。
- P4_Main/sketch_apr26a/HeartRateCore.cpp/.h：心率峰值检测与 BPM 计算。
- P4_Main/sketch_apr26a/SpO2Core.cpp/.h：血氧估算与信号处理。
- P4_Main/sketch_apr26a/ui_heart_test.cpp/.h：实时检测、历史记录、AI 结果显示和检测流程控制。
- P4_Main/sketch_apr26a/c6_network.cpp/.h：P4 侧串口协议、AI 请求下发、C6 状态接收。
- P4_Main/sketch_apr26a/lvgl_mipilcd_port.cpp/.h、mipilcd.cpp/.h：屏幕与 LVGL 移植层。
- P4_Main/sketch_apr26a/src/ui：SquareLine/LVGL 生成的界面源码、字体和图片资源。
- C6_Network/esp32c666/esp32c666.ino：C6 端主逻辑、WiFi/NTP/UART 状态机。
- C6_Network/esp32c666/volcano_ai.cpp/.h：AI 请求封装与响应解析。
- C6_Network/esp32c666/wechat_push.cpp/.h：企业微信机器人消息推送。

四、安全处理
压缩包中的 WiFi 名称、WiFi 密码、火山方舟 API Key、企业微信 Webhook 已替换为占位符。
如需实机运行，请在 C6_Network/esp32c666 中替换为个人实际配置。
