#pragma once

/*
 * secrets.h — ESP32-C6 联网凭据（提交版，全部为空）
 *
 * 本文件随仓库公开，禁止在此写入任何真实值。
 * 本地运行请复制 secrets.local.h.example 为 secrets.local.h 并填写真实值：
 * secrets.local.h 已被 .gitignore 忽略，不会被提交。
 *
 * 存在 secrets.local.h 时，其中的宏定义会覆盖下面的空默认值。
 * Arduino 构建只会把 sketch 目录加入头文件搜索路径，因此本文件必须与
 * esp32c666.ino 位于同一目录，请勿移动到仓库根目录或 include/。
 */

#if defined(__has_include)
#  if __has_include("secrets.local.h")
#    include "secrets.local.h"
#  endif
#endif

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

#ifndef WECHAT_WEBHOOK_URL
#define WECHAT_WEBHOOK_URL ""
#endif

#ifndef ARK_API_KEY
#define ARK_API_KEY ""
#endif
