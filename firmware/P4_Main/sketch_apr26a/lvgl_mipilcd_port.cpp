#include "lvgl_mipilcd_port.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <esp_display_panel.hpp>
#include <lvgl.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mipilcd.h"

using namespace esp_panel::drivers;

#ifndef ARDUINO_RUNNING_CORE
#define ARDUINO_RUNNING_CORE tskNO_AFFINITY
#endif

static SemaphoreHandle_t s_lvgl_mutex = nullptr;
static TaskHandle_t s_lvgl_task = nullptr;
#if !LV_TICK_CUSTOM
static esp_timer_handle_t s_lvgl_tick_timer = nullptr;
#endif
static lv_disp_draw_buf_t s_disp_buf;
static lv_disp_drv_t s_disp_drv;
static lv_indev_drv_t s_touch_drv;
static lv_color_t *s_buf_1 = nullptr;
static lv_color_t *s_buf_2 = nullptr;
static uint16_t *s_flush_buf = nullptr;
static Touch *s_touch = nullptr;
static bool s_touch_ready = false;
static uint32_t s_last_touch_log_ms = 0;

static constexpr uint32_t LVGL_TICK_MS = 2;
static constexpr uint32_t LVGL_TASK_MIN_DELAY_MS = 2;
static constexpr uint32_t LVGL_TASK_MAX_DELAY_MS = 50;
static constexpr uint32_t LVGL_TASK_STACK = 8192;
static constexpr uint32_t LVGL_TASK_PRIORITY = 2;
static constexpr uint32_t DRAW_BUFFER_PIXELS = MIPILCD_UI_HOR_RES * MIPILCD_LVGL_BUFFER_ROWS;

static uint16_t color_to_rgb565(lv_color_t color)
{
#if LV_COLOR_DEPTH == 16
    return color.full;
#else
    const uint8_t r = color.ch.red;
    const uint8_t g = color.ch.green;
    const uint8_t b = color.ch.blue;
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
#endif
}

static void flush_rotated_90(const lv_area_t *area, const lv_color_t *color_map, int32_t src_stride)
{
    const int32_t src_w = area->x2 - area->x1 + 1;
    const int32_t src_h = area->y2 - area->y1 + 1;
    const uint16_t dst_x = LCD_WIDTH - 1 - area->y2;
    const uint16_t dst_y = area->x1;

    for (int32_t y = 0; y < src_h; y++) {
        for (int32_t x = 0; x < src_w; x++) {
            s_flush_buf[x * src_h + (src_h - 1 - y)] = color_to_rgb565(color_map[y * src_stride + x]);
        }
    }

    mipilcd_draw_rgb565(dst_x, dst_y, src_h, src_w, s_flush_buf);
}

static void flush_rotated_270(const lv_area_t *area, const lv_color_t *color_map, int32_t src_stride)
{
    const int32_t src_w = area->x2 - area->x1 + 1;
    const int32_t src_h = area->y2 - area->y1 + 1;
    const uint16_t dst_x = area->y1;
    const uint16_t dst_y = LCD_HEIGHT - 1 - area->x2;

    for (int32_t y = 0; y < src_h; y++) {
        for (int32_t x = 0; x < src_w; x++) {
            s_flush_buf[(src_w - 1 - x) * src_h + y] = color_to_rgb565(color_map[y * src_stride + x]);
        }
    }

    mipilcd_draw_rgb565(dst_x, dst_y, src_h, src_w, s_flush_buf);
}

static void flush_callback(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    if ((area->x2 < 0) || (area->y2 < 0) ||
        (area->x1 >= MIPILCD_UI_HOR_RES) || (area->y1 >= MIPILCD_UI_VER_RES)) {
        lv_disp_flush_ready(drv);
        return;
    }

    lv_area_t clipped = *area;
    clipped.x1 = (clipped.x1 < 0) ? 0 : clipped.x1;
    clipped.y1 = (clipped.y1 < 0) ? 0 : clipped.y1;
    clipped.x2 = (clipped.x2 >= MIPILCD_UI_HOR_RES) ? (MIPILCD_UI_HOR_RES - 1) : clipped.x2;
    clipped.y2 = (clipped.y2 >= MIPILCD_UI_VER_RES) ? (MIPILCD_UI_VER_RES - 1) : clipped.y2;

    const uint32_t pixel_count =
        static_cast<uint32_t>(clipped.x2 - clipped.x1 + 1) * static_cast<uint32_t>(clipped.y2 - clipped.y1 + 1);
    if (pixel_count > DRAW_BUFFER_PIXELS) {
        Serial.printf("[LVGL ERR] flush area too large: area=(%d,%d)-(%d,%d) pixels=%lu buffer=%lu\r\n",
                      area->x1,
                      area->y1,
                      area->x2,
                      area->y2,
                      (unsigned long)pixel_count,
                      (unsigned long)DRAW_BUFFER_PIXELS);
        lv_disp_flush_ready(drv);
        return;
    }

#if MIPILCD_UI_ROTATION == 90
    const int32_t src_stride = area->x2 - area->x1 + 1;
    const lv_color_t *src = color_map + (clipped.y1 - area->y1) * src_stride + (clipped.x1 - area->x1);
    flush_rotated_90(&clipped, src, src_stride);
#elif MIPILCD_UI_ROTATION == 270
    const int32_t src_stride = area->x2 - area->x1 + 1;
    const lv_color_t *src = color_map + (clipped.y1 - area->y1) * src_stride + (clipped.x1 - area->x1);
    flush_rotated_270(&clipped, src, src_stride);
#else
#error "MIPILCD_UI_ROTATION must be 90 or 270 for a 1280x720 UI on this 720x1280 panel"
#endif

    lv_disp_flush_ready(drv);
}

static void touch_read_callback(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;

    data->state = LV_INDEV_STATE_RELEASED;

    if (!s_touch_ready || (s_touch == nullptr)) {
        return;
    }

    TouchPoint point;
    const int point_count = s_touch->readPoints(&point, 1, 0);
    if (point_count <= 0) {
        return;
    }

#if MIPILCD_UI_ROTATION == 90
    const int16_t logical_x = point.y;
    const int16_t logical_y = LCD_WIDTH - 1 - point.x;
#elif MIPILCD_UI_ROTATION == 270
    const int16_t logical_x = LCD_HEIGHT - 1 - point.y;
    const int16_t logical_y = point.x;
#else
#error "MIPILCD_UI_ROTATION must be 90 or 270 for a 1280x720 UI on this 720x1280 panel"
#endif

    data->point.x = constrain(logical_x, 0, MIPILCD_UI_HOR_RES - 1);
    data->point.y = constrain(logical_y, 0, MIPILCD_UI_VER_RES - 1);
    data->state = LV_INDEV_STATE_PRESSED;

    const uint32_t now = millis();
    if ((now - s_last_touch_log_ms) > 250) {
        s_last_touch_log_ms = now;
        Serial.printf("Touch point: raw=%d,%d -> lvgl=%d,%d count=%d\r\n",
                      point.x,
                      point.y,
                      data->point.x,
                      data->point.y,
                      point_count);
    }
}

#if !LV_TICK_CUSTOM
static void lvgl_tick(void *arg)
{
    (void)arg;
    lv_tick_inc(LVGL_TICK_MS);
}
#endif

static void lvgl_task(void *arg)
{
    (void)arg;

    while (true) {
        uint32_t delay_ms = LVGL_TASK_MAX_DELAY_MS;
        if (lvgl_mipilcd_port_lock(-1)) {
            delay_ms = lv_timer_handler();
            lvgl_mipilcd_port_unlock();
        }

        if (delay_ms < LVGL_TASK_MIN_DELAY_MS) {
            delay_ms = LVGL_TASK_MIN_DELAY_MS;
        } else if (delay_ms > LVGL_TASK_MAX_DELAY_MS) {
            delay_ms = LVGL_TASK_MAX_DELAY_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

static void *malloc_prefer_psram(size_t size)
{
    void *ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr == nullptr) {
        Serial.printf("[LVGL MEM] PSRAM allocation failed for %u bytes, fallback to internal heap\r\n",
                      static_cast<unsigned>(size));
        ptr = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    }
    return ptr;
}

static bool init_display()
{
    const size_t lvgl_buffer_size = DRAW_BUFFER_PIXELS * sizeof(lv_color_t);
    const size_t flush_buffer_size = DRAW_BUFFER_PIXELS * sizeof(uint16_t);

    Serial.printf("[LVGL 03] display buffer plan | logical=%dx%d rows=%d pixels=%lu draw=%u bytes x2 rotate=%u bytes\r\n",
                  MIPILCD_UI_HOR_RES,
                  MIPILCD_UI_VER_RES,
                  MIPILCD_LVGL_BUFFER_ROWS,
                  (unsigned long)DRAW_BUFFER_PIXELS,
                  static_cast<unsigned>(lvgl_buffer_size),
                  static_cast<unsigned>(flush_buffer_size));

    s_buf_1 = static_cast<lv_color_t *>(malloc_prefer_psram(lvgl_buffer_size));
    s_buf_2 = static_cast<lv_color_t *>(malloc_prefer_psram(lvgl_buffer_size));
    s_flush_buf = static_cast<uint16_t *>(heap_caps_malloc(flush_buffer_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (s_flush_buf == nullptr) {
        Serial.printf("[LVGL MEM] DMA/internal rotate buffer allocation failed for %u bytes, fallback to 8-bit heap\r\n",
                      static_cast<unsigned>(flush_buffer_size));
        s_flush_buf = static_cast<uint16_t *>(heap_caps_malloc(flush_buffer_size, MALLOC_CAP_8BIT));
    }

    if ((s_buf_1 == nullptr) || (s_buf_2 == nullptr) || (s_flush_buf == nullptr)) {
        Serial.printf("[LVGL ERR] Failed to allocate buffers | buf1=%p buf2=%p rotate=%p\r\n",
                      s_buf_1,
                      s_buf_2,
                      s_flush_buf);
        return false;
    }

    Serial.printf("[LVGL 04] buffers allocated | buf1=%p buf2=%p rotate=%p\r\n",
                  s_buf_1,
                  s_buf_2,
                  s_flush_buf);

    lv_disp_draw_buf_init(&s_disp_buf, s_buf_1, s_buf_2, DRAW_BUFFER_PIXELS);
    Serial.println("[LVGL 05] draw buffer init done");
    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res = MIPILCD_UI_HOR_RES;
    s_disp_drv.ver_res = MIPILCD_UI_VER_RES;
    s_disp_drv.flush_cb = flush_callback;
    s_disp_drv.draw_buf = &s_disp_buf;

    lv_disp_t *disp = lv_disp_drv_register(&s_disp_drv);
    if (disp == nullptr) {
        Serial.println("[LVGL ERR] display driver register failed");
        return false;
    }

    Serial.println("[LVGL 06] display driver registered");
    return true;
}

static void init_touch()
{
#if MIPILCD_TOUCH_ENABLED
    Serial.printf("Touch init: SDA=%d SCL=%d RST=%d INT=%d addr=0x%02X freq=%d\r\n",
                  MIPILCD_TOUCH_SDA,
                  MIPILCD_TOUCH_SCL,
                  MIPILCD_TOUCH_RST,
                  MIPILCD_TOUCH_INT,
                  MIPILCD_TOUCH_ADDRESS,
                  MIPILCD_TOUCH_I2C_FREQ);

    BusI2C *touch_bus = new BusI2C(
        MIPILCD_TOUCH_SCL,
        MIPILCD_TOUCH_SDA,
        (BusI2C::ControlPanelFullConfig)ESP_PANEL_TOUCH_I2C_CONTROL_PANEL_CONFIG_WITH_ADDR(GT911, MIPILCD_TOUCH_ADDRESS)
    );
    touch_bus->configI2C_FreqHz(MIPILCD_TOUCH_I2C_FREQ);
    touch_bus->configI2C_PullupEnable(true, true);

    s_touch = new TouchGT911(touch_bus, LCD_WIDTH, LCD_HEIGHT, MIPILCD_TOUCH_RST, MIPILCD_TOUCH_INT);
    s_touch->configResetActiveLevel(0);

    if (!s_touch->begin()) {
        Serial.println("[TOUCH WARN] GT911 touch begin failed; UI will run without touch input");
        return;
    }

    s_touch_ready = true;
    Serial.printf("GT911 touch init done, raw=%dx%d, rotation=%d, logical=%dx%d\r\n",
                  LCD_WIDTH,
                  LCD_HEIGHT,
                  MIPILCD_UI_ROTATION,
                  MIPILCD_UI_HOR_RES,
                  MIPILCD_UI_VER_RES);

    lv_indev_drv_init(&s_touch_drv);
    s_touch_drv.type = LV_INDEV_TYPE_POINTER;
    s_touch_drv.read_cb = touch_read_callback;
    lv_indev_drv_register(&s_touch_drv);
#endif
}

bool lvgl_mipilcd_port_init()
{
    if (!mipilcd_is_ready()) {
        Serial.println("[LVGL ERR] LCD is not ready; call mipilcd_init() before LVGL init");
        return false;
    }

    Serial.printf("[LVGL 00] port init | logical=%dx%d physical=%dx%d rotation=%d tick=%dms task_delay=%d-%dms\r\n",
                  MIPILCD_UI_HOR_RES,
                  MIPILCD_UI_VER_RES,
                  LCD_WIDTH,
                  LCD_HEIGHT,
                  MIPILCD_UI_ROTATION,
                  LVGL_TICK_MS,
                  LVGL_TASK_MIN_DELAY_MS,
                  LVGL_TASK_MAX_DELAY_MS);

    lv_init();
    Serial.println("[LVGL 01] lv_init done");

#if !LV_TICK_CUSTOM
    const esp_timer_create_args_t tick_args = {
        .callback = lvgl_tick,
        .name = "lvgl_tick",
    };
    if (esp_timer_create(&tick_args, &s_lvgl_tick_timer) != ESP_OK ||
        esp_timer_start_periodic(s_lvgl_tick_timer, LVGL_TICK_MS * 1000) != ESP_OK) {
        Serial.println("[LVGL ERR] Failed to start LVGL tick timer");
        return false;
    }
    Serial.println("[LVGL 02] tick timer started");
#endif

    if (!init_display()) {
        return false;
    }
    Serial.println("[LVGL 07] init touch");
    init_touch();
    Serial.println(s_touch_ready ? "[LVGL 08] touch ready" : "[LVGL 08] touch not ready; continue without touch");

    s_lvgl_mutex = xSemaphoreCreateRecursiveMutex();
    if (s_lvgl_mutex == nullptr) {
        Serial.println("[LVGL ERR] Failed to create LVGL mutex");
        return false;
    }
    Serial.println("[LVGL 09] mutex created");

    if (xTaskCreatePinnedToCore(lvgl_task, "lvgl", LVGL_TASK_STACK, nullptr, LVGL_TASK_PRIORITY, &s_lvgl_task,
                                ARDUINO_RUNNING_CORE) != pdPASS) {
        Serial.println("[LVGL ERR] Failed to create LVGL task");
        return false;
    }

    Serial.printf("[LVGL 10] task started | stack=%lu priority=%lu core=%d\r\n",
                  (unsigned long)LVGL_TASK_STACK,
                  (unsigned long)LVGL_TASK_PRIORITY,
                  ARDUINO_RUNNING_CORE);
    return true;
}

bool lvgl_mipilcd_port_lock(int timeout_ms)
{
    if (s_lvgl_mutex == nullptr) {
        return false;
    }

    const TickType_t timeout = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(s_lvgl_mutex, timeout) == pdTRUE;
}

void lvgl_mipilcd_port_unlock()
{
    if (s_lvgl_mutex != nullptr) {
        xSemaphoreGiveRecursive(s_lvgl_mutex);
    }
}

bool lvgl_mipilcd_touch_is_ready()
{
    return s_touch_ready;
}
