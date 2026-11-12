# 编译与烧录

## 1. 环境要求

| 组件 | 已验证版本 / 路径 | 说明 |
| --- | --- | --- |
| arduino-cli | 1.5.1 | 本机为 Arduino IDE 内置：`D:/arduino/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe` |
| ESP32 Arduino Core | `esp32:esp32 3.1.1-cn` | 位于 `D:/AppDataMoved/Arduino15/packages/esp32/hardware/esp32/3.1.1-cn` |
| 第三方库 | LVGL / ESP32_Display_Panel / ESP32_IO_Expander / esp-lib-utils / bb_captouch / ArduinoJson | 本机位于 `D:/arduino_project/P4 and c6/libraries`；本仓库**不附带**这些库 |

`D:\Users\...\AppData\Local\Arduino15` 是符号链接，arduino-cli 直接跟随会失败。

旧工程库目录还有一处坑：`lvgl/src/test.ui/` 存放了 63 个 SquareLine 生成的 UI 源文件副本，与 sketch 自带的 `src/ui/` 同名函数会在链接期冲突（`multiple definition of ui_*`）。`tools/build.sh` 会自动生成剔除该目录的干净库副本（可用 `HM_CLEAN_LIBS` 指向已有副本）；手动命令请使用干净副本，不要直接用旧工程库目录。因此脚本显式设置 `ARDUINO_DIRECTORIES_DATA` 指向真实目录 `D:/AppDataMoved/Arduino15`。换机器时按实际路径设置 `ARDUINO_DATA` 与 `ARDUINO_LIBS`。

## 2. 配置头文件

`config/lv_conf.h` 与 `config/esp_panel_drivers_conf.h` 是原提交包附带的库配置，需要能被 LVGL 与显示驱动找到：

- **命令行编译**：`tools/build.sh` 先把 `config/*.h` 复制到纯 ASCII 构建目录的 `include/`，再注入 `-I<build>/include`。
- **Arduino IDE 编译**：把这两个文件复制到 sketchbook 的 `libraries/` 目录（与 `lvgl/`、`ESP32_Display_Panel/` 同级），或复制到对应库目录，再打开 `.ino` 编译。

## 3. 编译

> **Windows 路径限制**：ESP32 工具链的 assembler 无法在含中文的路径下生成 `.o`（实测报 `Fatal error: can't create ...`）。因此 `tools/build.sh` 把产物与 `-I` 头文件目录放在仓库外的纯 ASCII 目录 `C:/Temp/HealthMonitor-build/`，可用 `HM_BUILD_DIR` 覆盖；不要改成仓库内的 `build/`。

```bash
# 两个固件一起编译
bash tools/build.sh all

# 单独编译
bash tools/build.sh p4
bash tools/build.sh c6

# 保存日志
bash tools/build.sh all 2>&1 | tee C:/Temp/HealthMonitor-build/build.log
```

等价的手动命令：

```bash
CLI="D:/arduino/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe"
export ARDUINO_DIRECTORIES_DATA="D:/AppDataMoved/Arduino15"
B="C:/Temp/HealthMonitor-build"        # 必须是纯 ASCII 路径
LIBS="$B/libs"                         # 不含 lvgl/src/test.ui 的干净库副本
mkdir -p "$B/include" && cp config/*.h "$B/include/"

"$CLI" compile \
  --fqbn esp32:esp32:esp32c6 \
  --libraries "$LIBS" \
  --build-path "$B/c6" \
  firmware/C6_Network/esp32c666

"$CLI" compile \
  --fqbn "esp32:esp32:esp32p4:FlashSize=16M,PartitionScheme=custom,PSRAM=enabled" \
  --libraries "$LIBS" \
  --build-path "$B/p4" \
  --build-property "compiler.cpp.extra_flags=-I$B/include" \
  --build-property "compiler.c.extra_flags=-I$B/include" \
  firmware/P4_Main/sketch_apr26a
```

板卡参数说明：

| 目标 | FQBN | 关键选项 |
| --- | --- | --- |
| P4 | `esp32:esp32:esp32p4` | Flash 16MB、PSRAM enabled、自定义分区（`partitions.csv` 在 sketch 目录内） |
| C6 | `esp32:esp32:esp32c6` | 默认选项 |

P4 参数来自原工程构建记录 `build.options.json`，与 `README_代码说明.txt` 中的烧录建议一致。

## 4. 配置密钥（烧录前）

```bash
cd firmware/C6_Network/esp32c666
cp secrets.local.h.example secrets.local.h
# 编辑 secrets.local.h，填入 WIFI_SSID / WIFI_PASSWORD / WECHAT_WEBHOOK_URL / ARK_API_KEY
```

`secrets.local.h` 已被 `.gitignore` 忽略。四个值留空时固件仍可编译启动，但 Wi-Fi、云端 AI、企业微信推送会被跳过。详见 [config/README.md](../config/README.md)。

## 5. 烧录

两块芯片分别烧录，串口监视器波特率 115200。用 Arduino IDE 打开对应 `.ino` 后选择板卡（P4 需选 16MB Flash / PSRAM Enabled / Custom 分区），或用 arduino-cli：

```bash
"$CLI" upload --fqbn "esp32:esp32:esp32c6" --port COMx firmware/C6_Network/esp32c666
"$CLI" upload --fqbn "esp32:esp32:esp32p4:FlashSize=16M,PartitionScheme=custom,PSRAM=enabled" --port COMx firmware/P4_Main/sketch_apr26a
```

> COM 口号、烧录接线与波特率以实际硬件为准；本仓库未做实机烧录验证。

## 6. 常见问题

| 现象 | 处理 |
| --- | --- |
| `Error following symlink ... Arduino15\packages` | 设置 `ARDUINO_DIRECTORIES_DATA` 到真实目录，脚本已处理 |
| `lv_conf.h: No such file or directory` | 检查 `-I.../include` 是否传入、配置文件是否已复制；IDE 用户按第 2 节放置 |
| `Assembler messages: Fatal error: can't create ... No such file or directory` | 构建路径含中文；把 `HM_BUILD_DIR` 设为纯 ASCII 目录 |
| `multiple libraries found for ...` | `--libraries` 只指向一套库目录，避免与 sketchbook/libraries 重复 |
| `multiple definition of ui_*` / `collect2.exe: ld returned 1` | 库目录含 `lvgl/src/test.ui` 副本；改用干净库副本（脚本自动处理） |
| `PartitionScheme=custom` 报分区表缺失 | 确认 `partitions.csv` 与 `sketch_apr26a.ino` 在同一目录 |
| 编译通过但设备无网络功能 | 检查 `secrets.local.h` 是否存在、四个宏是否填写 |

## 7. 验证记录

见仓库根目录 README 的「验证状态」一节；产物与日志保存在仓库外的 `C:/Temp/HealthMonitor-build/`，不进入版本库。
