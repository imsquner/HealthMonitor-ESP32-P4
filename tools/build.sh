#!/usr/bin/env bash
# HealthMonitor 双固件编译脚本
# 用法: bash tools/build.sh [p4|c6|all]
#
# 可用环境变量覆盖默认路径（本机默认值为 2026-11 实测路径）：
#   ARDUINO_CLI   arduino-cli 可执行文件
#   ARDUINO_DATA  Arduino15 数据目录（含 packages/esp32）
#   ARDUINO_LIBS  原始第三方库目录（含 lvgl / ESP32_Display_Panel / ArduinoJson）
#   HM_BUILD_DIR  构建产物目录，必须是纯 ASCII 路径
#   HM_CLEAN_LIBS 已清理好的库目录；设置后跳过自动清理
#
# 旧工程库目录常把 SquareLine 生成的 UI 副本塞进 lvgl/src/test.ui，
# 与 sketch 自带 src/ui 重复定义导致链接失败。脚本会生成剔除该目录的干净副本。
#
# 重要：Windows 版 ESP32 工具链的 assembler 无法在含中文的路径下创建 .o，
# 因此构建产物与 -I 头文件目录都放在仓库外的纯 ASCII 目录，不能放在仓库内。

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CLI="${ARDUINO_CLI:-D:/arduino/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe}"
DATA="${ARDUINO_DATA:-D:/AppDataMoved/Arduino15}"
RAW_LIBS="${ARDUINO_LIBS:-D:/arduino_project/P4 and c6/libraries}"
BUILD_ROOT="${HM_BUILD_DIR:-C:/Temp/HealthMonitor-build}"
CLEAN_LIBS="${HM_CLEAN_LIBS:-$BUILD_ROOT/libs}"
INCLUDE_DIR="$BUILD_ROOT/include"

export ARDUINO_DIRECTORIES_DATA="$DATA"
export ARDUINO_DIRECTORIES_DOWNLOADS="${ARDUINO_DOWNLOADS:-$DATA/staging}"
export ARDUINO_DIRECTORIES_USER="${ARDUINO_SKETCHBOOK:-$BUILD_ROOT/sketchbook}"

TARGET="${1:-all}"
P4_FQBN="esp32:esp32:esp32p4:FlashSize=16M,PartitionScheme=custom,PSRAM=enabled"
C6_FQBN="esp32:esp32:esp32c6"
P4_SKETCH="$ROOT/firmware/P4_Main/sketch_apr26a"
C6_SKETCH="$ROOT/firmware/C6_Network/esp32c666"

mkdir -p "$BUILD_ROOT" "$INCLUDE_DIR" "$ARDUINO_DIRECTORIES_USER"
# LVGL / 显示驱动配置需要出现在 include 路径中，且路径必须是 ASCII
cp "$ROOT/config/lv_conf.h" "$ROOT/config/esp_panel_drivers_conf.h" "$INCLUDE_DIR/"

# 生成不含 lvgl/src/test.ui 的干净库副本（一次性缓存）
if [ -z "${HM_CLEAN_LIBS:-}" ] && [ ! -d "$CLEAN_LIBS/lvgl" ]; then
    echo "正在生成干净库副本：$CLEAN_LIBS"
    python - "$RAW_LIBS" "$CLEAN_LIBS" <<'PYEOF'
import shutil, sys
from pathlib import Path
src, dst = Path(sys.argv[1]), Path(sys.argv[2])
dst.mkdir(parents=True, exist_ok=True)
for d in sorted(src.iterdir()):
    if d.is_dir() and not (dst / d.name).exists():
        shutil.copytree(d, dst / d.name, ignore=shutil.ignore_patterns("test.ui"))
PYEOF
fi

if ! "$CLI" version >/dev/null 2>&1; then
    echo "[ERROR] 找不到可用的 arduino-cli：$CLI" >&2
    echo "        请设置 ARDUINO_CLI=/path/to/arduino-cli" >&2
    exit 2
fi

echo "CLI   : $("$CLI" version | head -1)"
echo "CORE  : esp32:esp32 3.1.1-cn (需已安装)"
echo "LIBS  : $CLEAN_LIBS (clean copy)"
echo "BUILD : $BUILD_ROOT (ASCII only)"
echo

case "$TARGET" in
    c6) ;;
    p4) ;;
    all) ;;
    *) echo "用法: bash tools/build.sh [p4|c6|all]" >&2; exit 2 ;;
esac

if [ "$TARGET" = "c6" ] || [ "$TARGET" = "all" ]; then
    echo "===== [1/2] 编译 ESP32-C6 网络端 ====="
    "$CLI" compile \
        --fqbn "$C6_FQBN" \
        --libraries "$CLEAN_LIBS" \
        --build-path "$BUILD_ROOT/c6" \
        "$C6_SKETCH"
    echo "===== C6 完成 ====="
    echo
fi

if [ "$TARGET" = "p4" ] || [ "$TARGET" = "all" ]; then
    echo "===== [2/2] 编译 ESP32-P4 主控端 ====="
    "$CLI" compile \
        --fqbn "$P4_FQBN" \
        --libraries "$CLEAN_LIBS" \
        --build-path "$BUILD_ROOT/p4" \
        --build-property "compiler.cpp.extra_flags=-I$INCLUDE_DIR" \
        --build-property "compiler.c.extra_flags=-I$INCLUDE_DIR" \
        "$P4_SKETCH"
    echo "===== P4 完成 ====="
    echo
fi

echo "产物目录：$BUILD_ROOT/"
echo "提示：脚本只做编译，不烧录、不改动源码；产物不进入版本库。"
