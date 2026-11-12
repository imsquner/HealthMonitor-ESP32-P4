#include "board_keys.h"

#include <Arduino.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "lvgl_mipilcd_port.h"
#include "ui/ui.h"
#include "ui_heart_test.h"
#include "ui_login.h"
#include "ui_screen_nav.h"

static const uint8_t BOOT_GPIO = 35;
static const uint16_t EXIO_KEY0_MASK = (uint16_t)(1U << 8);
static const uint16_t EXIO_KEY1_MASK = (uint16_t)(1U << 9);
static const uint16_t EXIO_KEY2_MASK = (uint16_t)(1U << 10);
static const uint16_t EXIO_KEYS_MASK = EXIO_KEY0_MASK | EXIO_KEY1_MASK | EXIO_KEY2_MASK;
static const i2c_port_t KEY_I2C_PORT = I2C_NUM_0;
static const uint8_t XL9555_INPUT_REG = 0x00;
static const uint8_t XL9555_CONFIG_REG = 0x06;
static const uint32_t KEY_POLL_INTERVAL_MS = 20;
static const uint32_t KEY_DEBOUNCE_MS = 45;
static const uint32_t BOOT_LONG_PRESS_MS = 2000;
static const uint32_t I2C_TIMEOUT_MS = 10;

struct KeyState {
    const char *name;
    bool stable_pressed;
    bool raw_pressed;
    bool long_sent;
    uint32_t raw_changed_ms;
    uint32_t pressed_ms;
};

static KeyState s_boot_key = { "BOOT", false, false, false, 0, 0 };
static KeyState s_key0 = { "KEY0", false, false, false, 0, 0 };
static KeyState s_key1 = { "KEY1", false, false, false, 0, 0 };
static KeyState s_key2 = { "KEY2", false, false, false, 0, 0 };

static bool s_expander_ready = false;
static uint8_t s_expander_addr = 0;
static uint16_t s_last_expander_input = 0xFFFF;
static uint32_t s_last_poll_ms = 0;

static void key_log(const char *text)
{
    Serial.println(text);
    Serial0.println(text);
}

static void key_logf(const char *format, ...)
{
    char buffer[180];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    Serial.print(buffer);
    Serial0.print(buffer);
}

static bool i2c_read_word(uint8_t addr, uint8_t reg, uint16_t *value)
{
    uint8_t data[2] = { 0, 0 };
    esp_err_t ret = i2c_master_write_read_device(KEY_I2C_PORT,
                                                 addr,
                                                 &reg,
                                                 1,
                                                 data,
                                                 sizeof(data),
                                                 pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    if (ret != ESP_OK) {
        return false;
    }
    *value = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
    return true;
}

static bool i2c_write_word(uint8_t addr, uint8_t reg, uint16_t value)
{
    uint8_t data[3] = {
        reg,
        (uint8_t)(value & 0xFF),
        (uint8_t)((value >> 8) & 0xFF),
    };
    esp_err_t ret = i2c_master_write_to_device(KEY_I2C_PORT,
                                               addr,
                                               data,
                                               sizeof(data),
                                               pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    return ret == ESP_OK;
}

static bool expander_try_addr(uint8_t addr)
{
    uint16_t input = 0;
    if (!i2c_read_word(addr, XL9555_INPUT_REG, &input)) {
        return false;
    }

    uint16_t direction = 0;
    if (i2c_read_word(addr, XL9555_CONFIG_REG, &direction)) {
        uint16_t wanted = direction | EXIO_KEYS_MASK;
        if (wanted != direction && !i2c_write_word(addr, XL9555_CONFIG_REG, wanted)) {
            key_logf("[KEY] warn: config write failed addr=0x%02X dir=0x%04X\r\n", addr, direction);
        }
    } else {
        key_logf("[KEY] warn: config read failed addr=0x%02X, keep default input\r\n", addr);
    }

    s_expander_addr = addr;
    s_last_expander_input = input;
    s_expander_ready = true;
    key_logf("[KEY] expander ready addr=0x%02X input=0x%04X pins KEY0=EXIO8 KEY1=EXIO9 KEY2=EXIO10\r\n",
             s_expander_addr,
             s_last_expander_input);
    return true;
}

static void init_expander(void)
{
    s_expander_ready = false;
    s_expander_addr = 0;

    for (uint8_t addr = 0x20; addr <= 0x27; addr++) {
        if (expander_try_addr(addr)) {
            return;
        }
    }

    key_log("[KEY] expander not found on I2C0 addr 0x20-0x27; KEY0/KEY1/KEY2 disabled");
}

static void load_page_index(int index)
{
    if (lvgl_mipilcd_port_lock(100)) {
        if (!ui_login_is_authenticated()) {
            ui_login_show();
            lvgl_mipilcd_port_unlock();
            key_log("[KEY] page change blocked: login required");
            return;
        }
        ui_screen_nav_load(index);
        lvgl_mipilcd_port_unlock();
        key_logf("[KEY] page index -> %d\r\n", index);
    } else {
        key_log("[KEY] page change skipped: LVGL busy");
    }
}

static void step_page(int delta)
{
    if (lvgl_mipilcd_port_lock(100)) {
        if (!ui_login_is_authenticated()) {
            ui_login_show();
            lvgl_mipilcd_port_unlock();
            key_log("[KEY] page step blocked: login required");
            return;
        }
        if (delta > 0) {
            ui_screen_nav_next();
        } else {
            ui_screen_nav_prev();
        }
        lvgl_mipilcd_port_unlock();
    } else {
        key_log("[KEY] page change skipped: LVGL busy");
    }
}

static void key0_toggle_detection(void)
{
    if (!ui_login_is_authenticated()) {
        if (lvgl_mipilcd_port_lock(100)) {
            ui_login_show();
            lvgl_mipilcd_port_unlock();
        }
        key_log("[KEY] detection blocked: login required");
        return;
    }
    if (!ui_heart_test_is_collecting()) {
        load_page_index(1);
    }
    ui_heart_test_debug_toggle();
}

static void boot_long_press(void)
{
    key_log("[KEY] BOOT long press -> home");
    load_page_index(0);
}

static void boot_short_press(void)
{
    key_log("[KEY] BOOT short press -> home");
    load_page_index(0);
}

static void handle_key_transition(KeyState *key, bool pressed, uint32_t now_ms)
{
    if (pressed != key->raw_pressed) {
        key->raw_pressed = pressed;
        key->raw_changed_ms = now_ms;
        return;
    }

    if ((now_ms - key->raw_changed_ms) < KEY_DEBOUNCE_MS || pressed == key->stable_pressed) {
        return;
    }

    key->stable_pressed = pressed;
    if (pressed) {
        key->pressed_ms = now_ms;
        key->long_sent = false;
        key_logf("[KEY] %s pressed\r\n", key->name);

        if (strcmp(key->name, "KEY1") == 0) {
            key_log("[KEY] KEY1 next page");
            step_page(1);
        } else if (strcmp(key->name, "KEY2") == 0) {
            key_log("[KEY] KEY2 prev page");
            step_page(-1);
        } else if (strcmp(key->name, "KEY0") == 0) {
            key_log("[KEY] KEY0 toggle detection");
            key0_toggle_detection();
        }
    } else {
        uint32_t held_ms = now_ms - key->pressed_ms;
        key_logf("[KEY] %s released held=%lums\r\n", key->name, (unsigned long)held_ms);
        if (strcmp(key->name, "BOOT") == 0 && !key->long_sent) {
            boot_short_press();
        }
    }
}

static void poll_boot(uint32_t now_ms)
{
    bool pressed = (digitalRead(BOOT_GPIO) == LOW);
    handle_key_transition(&s_boot_key, pressed, now_ms);
    if (s_boot_key.stable_pressed && !s_boot_key.long_sent &&
        (now_ms - s_boot_key.pressed_ms) >= BOOT_LONG_PRESS_MS) {
        s_boot_key.long_sent = true;
        boot_long_press();
    }
}

static void poll_expander(uint32_t now_ms)
{
    if (!s_expander_ready) {
        return;
    }

    uint16_t input = 0xFFFF;
    bool read_ok = false;
    if (lvgl_mipilcd_port_lock(20)) {
        read_ok = i2c_read_word(s_expander_addr, XL9555_INPUT_REG, &input);
        lvgl_mipilcd_port_unlock();
    }

    if (!read_ok) {
        static uint32_t last_warn_ms = 0;
        if ((now_ms - last_warn_ms) > 3000) {
            last_warn_ms = now_ms;
            key_logf("[KEY] expander read failed addr=0x%02X\r\n", s_expander_addr);
        }
        return;
    }

    s_last_expander_input = input;
    handle_key_transition(&s_key0, (input & EXIO_KEY0_MASK) == 0, now_ms);
    handle_key_transition(&s_key1, (input & EXIO_KEY1_MASK) == 0, now_ms);
    handle_key_transition(&s_key2, (input & EXIO_KEY2_MASK) == 0, now_ms);
}

void board_keys_init(void)
{
    pinMode(BOOT_GPIO, INPUT_PULLUP);
    delay(2);

    s_boot_key.raw_pressed = (digitalRead(BOOT_GPIO) == LOW);
    s_boot_key.stable_pressed = s_boot_key.raw_pressed;
    s_boot_key.raw_changed_ms = millis();
    s_boot_key.pressed_ms = s_boot_key.raw_pressed ? millis() : 0;
    s_boot_key.long_sent = false;

    key_log("[KEY] init boot=GPIO35 active-low short=home long=home");
    init_expander();

    bool key0_pressed = false;
    bool key1_pressed = false;
    bool key2_pressed = false;
    if (s_expander_ready) {
        key0_pressed = (s_last_expander_input & EXIO_KEY0_MASK) == 0;
        key1_pressed = (s_last_expander_input & EXIO_KEY1_MASK) == 0;
        key2_pressed = (s_last_expander_input & EXIO_KEY2_MASK) == 0;
    }

    KeyState *keys[] = { &s_key0, &s_key1, &s_key2 };
    bool pressed[] = { key0_pressed, key1_pressed, key2_pressed };
    for (uint8_t i = 0; i < 3; i++) {
        keys[i]->raw_pressed = pressed[i];
        keys[i]->stable_pressed = pressed[i];
        keys[i]->raw_changed_ms = millis();
        keys[i]->pressed_ms = pressed[i] ? millis() : 0;
        keys[i]->long_sent = false;
    }

    key_logf("[KEY] initial boot=%d key0=%d key1=%d key2=%d\r\n",
             s_boot_key.stable_pressed ? 1 : 0,
             s_key0.stable_pressed ? 1 : 0,
             s_key1.stable_pressed ? 1 : 0,
             s_key2.stable_pressed ? 1 : 0);
}

void board_keys_poll(void)
{
    uint32_t now_ms = millis();
    if ((now_ms - s_last_poll_ms) < KEY_POLL_INTERVAL_MS) {
        return;
    }
    s_last_poll_ms = now_ms;

    poll_boot(now_ms);
    poll_expander(now_ms);
}
