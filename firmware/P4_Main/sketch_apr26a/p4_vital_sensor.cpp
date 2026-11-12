#include "p4_vital_sensor.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "lvgl_mipilcd_port.h"

static const i2c_port_t SENSOR_I2C_PORT = I2C_NUM_0;
static const uint8_t MAX30102_ADDR = 0x57;
static const uint8_t MAX30102_EXPECTED_PART_ID = 0x15;

static const uint8_t REG_INT_STATUS_1 = 0x00;
static const uint8_t REG_INT_STATUS_2 = 0x01;
static const uint8_t REG_FIFO_WR_PTR = 0x04;
static const uint8_t REG_OVF_COUNTER = 0x05;
static const uint8_t REG_FIFO_RD_PTR = 0x06;
static const uint8_t REG_FIFO_DATA = 0x07;
static const uint8_t REG_FIFO_CONFIG = 0x08;
static const uint8_t REG_MODE_CONFIG = 0x09;
static const uint8_t REG_SPO2_CONFIG = 0x0A;
static const uint8_t REG_LED1_PA = 0x0C;
static const uint8_t REG_LED2_PA = 0x0D;
static const uint8_t REG_LED3_PA = 0x0E;
static const uint8_t REG_PROX_PA = 0x10;
static const uint8_t REG_MULTI_LED_1 = 0x11;
static const uint8_t REG_MULTI_LED_2 = 0x12;
static const uint8_t REG_PART_ID = 0xFF;

static const uint8_t MODE_RESET = 0x40;
static const uint8_t MODE_RED_IR = 0x03;
static const uint8_t FIFO_SAMPLE_AVG_4 = 0x40;
static const uint8_t FIFO_ROLLOVER_ENABLE = 0x10;
static const uint8_t SPO2_ADC_RANGE_4096 = 0x20;
static const uint8_t SPO2_SAMPLE_RATE_100 = 0x04;
static const uint8_t SPO2_PULSE_WIDTH_411 = 0x03;
static const uint8_t SLOT_RED = 0x01;
static const uint8_t SLOT_IR = 0x02;

static const uint8_t RED_LED_BRIGHTNESS = 0x1F;
static const uint8_t IR_LED_BRIGHTNESS = 0x1F;
static const uint32_t I2C_TIMEOUT_MS = 12;
static const uint32_t INIT_RETRY_MS = 1000;
static const uint32_t MISSING_LOG_INTERVAL_MS = 5000;
static const uint32_t SAMPLE_PERIOD_MS = 10;

static bool s_sensor_ready = false;
static uint32_t s_last_init_attempt_ms = 0;
static uint32_t s_last_missing_log_ms = 0;
static uint8_t s_i2c_error_count = 0;

static void sensor_log(const char *text)
{
    Serial.println(text);
    Serial0.println(text);
}

static void sensor_logf(const char *format, ...)
{
    char buffer[180];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    Serial.print(buffer);
    Serial0.print(buffer);
}

static bool i2c_read_bytes(uint8_t reg, uint8_t *data, size_t len, bool *bus_busy = nullptr)
{
    if (bus_busy != nullptr) {
        *bus_busy = false;
    }
    if (!lvgl_mipilcd_port_lock(10)) {
        if (bus_busy != nullptr) {
            *bus_busy = true;
        }
        return false;
    }

    esp_err_t ret = i2c_master_write_read_device(SENSOR_I2C_PORT,
                                                 MAX30102_ADDR,
                                                 &reg,
                                                 1,
                                                 data,
                                                 len,
                                                 pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    lvgl_mipilcd_port_unlock();
    return ret == ESP_OK;
}

static bool i2c_read_register(uint8_t reg, uint8_t *value, bool *bus_busy = nullptr)
{
    return i2c_read_bytes(reg, value, 1, bus_busy);
}

static bool i2c_write_register(uint8_t reg, uint8_t value, bool *bus_busy = nullptr)
{
    if (bus_busy != nullptr) {
        *bus_busy = false;
    }
    if (!lvgl_mipilcd_port_lock(10)) {
        if (bus_busy != nullptr) {
            *bus_busy = true;
        }
        return false;
    }

    uint8_t data[2] = { reg, value };
    esp_err_t ret = i2c_master_write_to_device(SENSOR_I2C_PORT,
                                               MAX30102_ADDR,
                                               data,
                                               sizeof(data),
                                               pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    lvgl_mipilcd_port_unlock();
    return ret == ESP_OK;
}

static void note_i2c_result(bool ok, bool bus_busy)
{
    if (ok) {
        s_i2c_error_count = 0;
        return;
    }
    if (bus_busy) {
        return;
    }
    if (s_i2c_error_count < 255) {
        s_i2c_error_count++;
    }
    if (s_i2c_error_count >= 8 && s_sensor_ready) {
        s_sensor_ready = false;
        sensor_log("[P4 SENSOR] MAX30102 I2C lost, will retry");
    }
}

static bool clear_fifo_impl(void)
{
    bool ok = true;
    bool busy = false;
    ok = i2c_write_register(REG_FIFO_WR_PTR, 0x00, &busy) && ok;
    note_i2c_result(ok, busy);
    ok = i2c_write_register(REG_OVF_COUNTER, 0x00, &busy) && ok;
    note_i2c_result(ok, busy);
    ok = i2c_write_register(REG_FIFO_RD_PTR, 0x00, &busy) && ok;
    note_i2c_result(ok, busy);
    return ok;
}

static bool configure_sensor(void)
{
    bool busy = false;
    if (!i2c_write_register(REG_MODE_CONFIG, MODE_RESET, &busy)) {
        note_i2c_result(false, busy);
        return false;
    }

    uint32_t start_ms = millis();
    while (millis() - start_ms < 100) {
        uint8_t mode = 0;
        if (!i2c_read_register(REG_MODE_CONFIG, &mode, &busy)) {
            note_i2c_result(false, busy);
            return false;
        }
        if ((mode & MODE_RESET) == 0) {
            break;
        }
        delay(1);
    }

    bool ok = true;
    ok = i2c_write_register(REG_FIFO_CONFIG, FIFO_SAMPLE_AVG_4 | FIFO_ROLLOVER_ENABLE, &busy) && ok;
    note_i2c_result(ok, busy);
    ok = i2c_write_register(REG_MODE_CONFIG, MODE_RED_IR, &busy) && ok;
    note_i2c_result(ok, busy);
    ok = i2c_write_register(REG_SPO2_CONFIG,
                            SPO2_ADC_RANGE_4096 | SPO2_SAMPLE_RATE_100 | SPO2_PULSE_WIDTH_411,
                            &busy) && ok;
    note_i2c_result(ok, busy);
    ok = i2c_write_register(REG_LED1_PA, RED_LED_BRIGHTNESS, &busy) && ok;
    note_i2c_result(ok, busy);
    ok = i2c_write_register(REG_LED2_PA, IR_LED_BRIGHTNESS, &busy) && ok;
    note_i2c_result(ok, busy);
    ok = i2c_write_register(REG_LED3_PA, 0x00, &busy) && ok;
    note_i2c_result(ok, busy);
    ok = i2c_write_register(REG_PROX_PA, 0x00, &busy) && ok;
    note_i2c_result(ok, busy);
    ok = i2c_write_register(REG_MULTI_LED_1, (uint8_t)((SLOT_IR << 4) | SLOT_RED), &busy) && ok;
    note_i2c_result(ok, busy);
    ok = i2c_write_register(REG_MULTI_LED_2, 0x00, &busy) && ok;
    note_i2c_result(ok, busy);
    ok = clear_fifo_impl() && ok;

    uint8_t ignored = 0;
    (void)i2c_read_register(REG_INT_STATUS_1, &ignored);
    (void)i2c_read_register(REG_INT_STATUS_2, &ignored);
    return ok;
}

bool p4_vital_sensor_begin(void)
{
    if (s_sensor_ready) {
        return true;
    }

    uint32_t now_ms = millis();
    if (s_last_init_attempt_ms != 0 && now_ms - s_last_init_attempt_ms < INIT_RETRY_MS) {
        return false;
    }
    s_last_init_attempt_ms = now_ms;

    bool busy = false;
    uint8_t part_id = 0;
    if (!i2c_read_register(REG_PART_ID, &part_id, &busy)) {
        if (!busy && now_ms - s_last_missing_log_ms >= MISSING_LOG_INTERVAL_MS) {
            s_last_missing_log_ms = now_ms;
            sensor_log("[P4 SENSOR] MAX30102 not reachable at I2C address 0x57");
        }
        return false;
    }

    if (part_id != MAX30102_EXPECTED_PART_ID) {
        if (now_ms - s_last_missing_log_ms >= MISSING_LOG_INTERVAL_MS) {
            s_last_missing_log_ms = now_ms;
            sensor_logf("[P4 SENSOR] unexpected part id=0x%02X at address 0x57\r\n", part_id);
        }
        return false;
    }

    if (!configure_sensor()) {
        if (now_ms - s_last_missing_log_ms >= MISSING_LOG_INTERVAL_MS) {
            s_last_missing_log_ms = now_ms;
            sensor_log("[P4 SENSOR] MAX30102 config failed");
        }
        return false;
    }

    s_i2c_error_count = 0;
    s_sensor_ready = true;
    sensor_log("[P4 SENSOR] MAX30102 ready on P4 I2C0 GPIO32/33");
    return true;
}

bool p4_vital_sensor_is_available(void)
{
    return s_sensor_ready;
}

bool p4_vital_sensor_clear_fifo(void)
{
    if (!p4_vital_sensor_begin()) {
        return false;
    }
    return clear_fifo_impl();
}

bool p4_vital_sensor_read_next_sample(long &red_value, long &ir_value,
                                      uint32_t &sample_time_ms)
{
    if (!p4_vital_sensor_begin()) {
        return false;
    }

    bool busy = false;
    uint8_t write_ptr = 0;
    uint8_t read_ptr = 0;
    if (!i2c_read_register(REG_FIFO_WR_PTR, &write_ptr, &busy)) {
        note_i2c_result(false, busy);
        return false;
    }
    if (!i2c_read_register(REG_FIFO_RD_PTR, &read_ptr, &busy)) {
        note_i2c_result(false, busy);
        return false;
    }

    uint8_t available_samples = 0;
    write_ptr &= 0x1F;
    read_ptr &= 0x1F;
    if (write_ptr >= read_ptr) {
        available_samples = write_ptr - read_ptr;
    } else {
        available_samples = (uint8_t)(32 + write_ptr - read_ptr);
    }
    if (available_samples == 0) {
        return false;
    }

    uint8_t data[6] = { 0 };
    if (!i2c_read_bytes(REG_FIFO_DATA, data, sizeof(data), &busy)) {
        note_i2c_result(false, busy);
        return false;
    }
    note_i2c_result(true, false);

    red_value = (long)(((uint32_t)data[0] << 16 | (uint32_t)data[1] << 8 | data[2]) & 0x3FFFF);
    ir_value = (long)(((uint32_t)data[3] << 16 | (uint32_t)data[4] << 8 | data[5]) & 0x3FFFF);
    sample_time_ms = millis() - ((uint32_t)(available_samples - 1) * SAMPLE_PERIOD_MS);
    return true;
}
