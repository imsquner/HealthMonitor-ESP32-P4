# HealthMonitor 工程整理说明

这个文件夹是当前整理后的主工程：

`E:\esp32p4\arduino\project\HealthMonitor`

以后 Arduino IDE 只打开下面三个位置烧录：

| 模块 | 打开的 ino | 主要职责 |
| --- | --- | --- |
| P4 主控 | `E:\esp32p4\arduino\project\HealthMonitor\sketch_apr26a\sketch_apr26a.ino` | 屏幕 UI、主控逻辑、心率/血氧算法计算 |
| ESP32-S3 | `E:\esp32p4\arduino\project\HealthMonitor\esp32s3\esp32s3.ino` | 读取 MAX3010x 原始红光/红外数据并发给 P4 |
| ESP32-C6 | `E:\esp32p4\arduino\project\HealthMonitor\esp32c666\esp32c666.ino` | 原有 C6 通信/辅助逻辑 |

建议 Arduino IDE 的 Sketchbook location 设置为：

`E:\esp32p4\arduino\project\HealthMonitor`

烧录板卡建议：

| 模块 | Board |
| --- | --- |
| P4 | ESP32P4 Dev Module，Flash Size 16MB，PSRAM Enabled，Partition Scheme Custom |
| S3 | ESP32S3 Dev Module |
| C6 | ESP32C6 Dev Module |

当前数据链路：

- S3 发给 P4 的新协议是：`R finger red ir sampleMs`
- P4 端仍兼容旧协议：`S finger ir hr spo2`
- 心率/血氧算法已经从 S3 移到 P4，项目主控以 P4 为准。

旧目录已经保留到 project 根目录下的 `_archive_...` 归档文件夹里，不需要再从旧目录打开工程。
