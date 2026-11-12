# config/ — 工程配置

| 文件 | 用途 | 是否参与编译 |
| --- | --- | --- |
| `lv_conf.h` | LVGL 库配置（色深、字体、内存、控件开关），来自原提交包 `Library_Config/` | 否，需按 [BUILD.md](../docs/BUILD.md) 放到 Arduino 库搜索路径 |
| `esp_panel_drivers_conf.h` | ESP32_Display_Panel 屏幕/触摸驱动配置 | 同上 |
| `secrets.local.h.example` | 本地凭据模板 | 否，仅作模板 |

## 凭据不在这个目录

真实密钥写在：

```
firmware/C6_Network/esp32c666/secrets.local.h
```

该文件被 `.gitignore` 忽略。原因是 Arduino 构建只会把 sketch 目录加入头文件搜索路径，`secrets.h` 必须与 `esp32c666.ino` 同级，否则 `#include "secrets.h"` 找不到。

- `firmware/C6_Network/esp32c666/secrets.h`：**提交版，四个宏全部为空**，会被推送到仓库，禁止填真实值。
- `firmware/C6_Network/esp32c666/secrets.local.h.example`：**模板**，复制为 `secrets.local.h` 后填写。
- `firmware/C6_Network/esp32c666/secrets.local.h`：**本地真值**，不提交。存在时自动覆盖 `secrets.h` 中的空默认值。

四个宏：

| 宏 | 内容 |
| --- | --- |
| `WIFI_SSID` | 2.4GHz Wi-Fi 名称 |
| `WIFI_PASSWORD` | Wi-Fi 密码 |
| `WECHAT_WEBHOOK_URL` | 企业微信机器人完整 Webhook URL（含 `key=`） |
| `ARK_API_KEY` | 火山方舟 API Key，必须含 `Bearer ` 前缀 |

四个值全为空时固件仍可编译并启动，联网、AI 分析、企业微信推送会被跳过（见 `wifi_config_valid()`、`api_key_valid()`、`webhook_url_valid()` 三处校验）。

推送前运行 `python tools/check_secrets.py` 自检。
