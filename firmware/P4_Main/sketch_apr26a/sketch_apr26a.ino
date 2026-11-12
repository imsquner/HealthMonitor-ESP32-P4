#include <Arduino.h>

#include "board_keys.h"
#include "c6_network.h"
#include "lvgl_mipilcd_port.h"
#include "mipilcd.h"
#include "p4_vital_sensor.h"
#include "serial_time_set.h"
#include "ui/ui.h"
#include "ui_heart_test.h"
#include "ui_login.h"
#include "ui_time_picker.h"
#include "ui_time_sync.h"

HardwareSerial S3Serial(2);

static void boot_log(const char *step, const char *message)
{
    Serial.printf("[BOOT %s] %lu ms | %s\r\n", step, (unsigned long)millis(), message);
    Serial0.printf("[BOOT %s] %lu ms | %s\r\n", step, (unsigned long)millis(), message);
}

void setup()
{
    Serial.begin(115200);
    Serial0.begin(115200);
    delay(1000);

    // ---- 烧录成功标志:放在最早期,即使后面 LCD 初始化失败也能看到 ----
    // 每次重新烧录把下面的编号 #1 改成 #2、#3... 就能确认串口里跑的是不是最新固件
    const char *flash_banner = ">>> ESP32-P4 FLASH OK | build " __DATE__ " " __TIME__ " | unified-status-v2 key-controls-v1 <<<";
    for (int i = 0; i < 3; i++) {
        Serial.println(flash_banner);
        Serial0.println(flash_banner);
    }
    Serial.println(">>> ESP32-P4 UART PROBE ENABLED | unified-status-v2 key-controls-v1 <<<");
    Serial0.println(">>> ESP32-P4 UART PROBE ENABLED | unified-status-v2 key-controls-v1 <<<");
    boot_log("00", "Serial ready");
#ifdef LED_BUILTIN
    pinMode(LED_BUILTIN, OUTPUT);
    boot_log("01", "Blink test start");
    for (int i = 0; i < 6; i++) {   // 上电闪灯3次,不依赖串口也能确认新固件在跑
        digitalWrite(LED_BUILTIN, (i % 2) ? LOW : HIGH);
        delay(500);
    }
    digitalWrite(LED_BUILTIN, HIGH);
    boot_log("02", "Blink test done");
#endif

    boot_log("03", "Init C6 UART");
    c6_network_init();
    boot_log("03-S3", "Init S3 UART");
    S3Serial.begin(115200, SERIAL_8N1, 3, 23); // RX=3, TX=23
    boot_log("04", "Pre-LCD C6 UART probe");
    c6_network_probe_uart();
    boot_log("05", "Before LCD init");

    if (!mipilcd_init()) {
        boot_log("ERR", "Stop: LCD init failed");
        return;
    }
    boot_log("06", "LCD init done");

    boot_log("07", "LCD visible blue fill test");
    if (!mipilcd_fill(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1, BLUE)) {
        boot_log("ERR", "LCD blue fill failed");
        return;
    }
    delay(300);

    boot_log("08", "Clear LCD to black");
    if (!mipilcd_clear(BLACK)) {
        boot_log("ERR", "LCD clear failed");
        return;
    }
    boot_log("09", "LCD clear done");

    boot_log("10", "Before LVGL port init");
    if (!lvgl_mipilcd_port_init()) {
        boot_log("ERR", "Stop: LVGL init failed");
        return;
    }
    boot_log("11", "LVGL port init done");

    boot_log("11-SENSOR", "Probe P4 MAX30102");
    p4_vital_sensor_begin();

    boot_log("12", "Before C6 handshake");
    c6_network_pre_lcd_handshake();
    boot_log("13", "C6 handshake stage done");

    boot_log("14", "Before ui_init");
    if (lvgl_mipilcd_port_lock(-1)) {
        boot_log("15", "LVGL lock acquired");
        ui_init();
        boot_log("16", "SquareLine ui_init done");
        ui_time_sync_init();
        boot_log("17", "Time sync UI init done");
        ui_time_picker_init();
        boot_log("18", "Time picker UI init done");
        ui_heart_test_init();
        boot_log("19", "Heart test UI init done");
        c6_network_bind_wifi_detail_ui();
        boot_log("20", "C6 detail UI bind done");
        board_keys_init();
        boot_log("20K", "Board keys init done");
        ui_login_show();
        boot_log("20L", "Login screen shown");
        lvgl_mipilcd_port_unlock();
        boot_log("21", "LVGL lock released");
    } else {
        boot_log("ERR", "LVGL lock failed before UI init");
        return;
    }
    boot_log("22", "UI init done");

    Serial.println(lvgl_mipilcd_touch_is_ready() ? "[BOOT OK] SquareLine UI is running with touch" :
                                                   "[BOOT OK] SquareLine UI is running without touch");
    Serial0.println(lvgl_mipilcd_touch_is_ready() ? "[BOOT OK] SquareLine UI is running with touch" :
                                                    "[BOOT OK] SquareLine UI is running without touch");
}

void loop()
{
    c6_network_poll();
    serial_time_set_poll();
    ui_heart_test_poll();
    board_keys_poll();
#ifdef LED_BUILTIN
    static uint32_t last_led_toggle_ms = 0;
    static bool led_state = true;
    if (ui_heart_test_has_danger_alert()) {
        uint32_t now = millis();
        if (now - last_led_toggle_ms >= 500) {
            last_led_toggle_ms = now;
            led_state = !led_state;
            digitalWrite(LED_BUILTIN, led_state ? HIGH : LOW);
        }
    } else if (!led_state) {
        led_state = true;
        digitalWrite(LED_BUILTIN, HIGH);
    }
#endif
    delay(10);
}
