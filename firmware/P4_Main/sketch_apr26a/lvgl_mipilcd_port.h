#ifndef LVGL_MIPILCD_PORT_H
#define LVGL_MIPILCD_PORT_H

#include <Arduino.h>

#define MIPILCD_UI_HOR_RES 1280
#define MIPILCD_UI_VER_RES 720

#ifndef MIPILCD_LVGL_BUFFER_ROWS
#define MIPILCD_LVGL_BUFFER_ROWS 20
#endif

/*
 * The panel scans as 720x1280 portrait. The SquareLine UI is 1280x720.
 * Use 90 for clockwise landscape, or 270 if the image/touch is upside down.
 */
#ifndef MIPILCD_UI_ROTATION
#define MIPILCD_UI_ROTATION 90
#endif

#ifndef MIPILCD_TOUCH_ENABLED
#define MIPILCD_TOUCH_ENABLED 1
#endif

#ifndef MIPILCD_TOUCH_SDA
#define MIPILCD_TOUCH_SDA 33
#endif

#ifndef MIPILCD_TOUCH_SCL
#define MIPILCD_TOUCH_SCL 32
#endif

#ifndef MIPILCD_TOUCH_RST
#define MIPILCD_TOUCH_RST 45
#endif

#ifndef MIPILCD_TOUCH_INT
#define MIPILCD_TOUCH_INT 21
#endif

#ifndef MIPILCD_TOUCH_ADDRESS
#define MIPILCD_TOUCH_ADDRESS 0x14
#endif

#ifndef MIPILCD_TOUCH_I2C_FREQ
#define MIPILCD_TOUCH_I2C_FREQ 400000
#endif

bool lvgl_mipilcd_port_init();
bool lvgl_mipilcd_port_lock(int timeout_ms);
void lvgl_mipilcd_port_unlock();
bool lvgl_mipilcd_touch_is_ready();

#endif
