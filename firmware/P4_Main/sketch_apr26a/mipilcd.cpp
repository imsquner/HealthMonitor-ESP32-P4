#include "mipilcd.h"
#include "esp_heap_caps.h"

using namespace esp_panel::drivers;

uint16_t mipilcd_width = LCD_WIDTH;
uint16_t mipilcd_height = LCD_HEIGHT;

static LCD *s_lcd = nullptr;
static bool s_lcd_ready = false;
static constexpr uint16_t DRAW_CHUNK_ROWS = 8;
static uint16_t draw_buffer[LCD_WIDTH * DRAW_CHUNK_ROWS];

static LCD *create_lcd()
{
    auto *bus = new BusDSI(
        LCD_DSI_LANE_NUM, LCD_DSI_LANE_RATE_MBPS,
        LCD_DPI_CLK_MHZ, LCD_DPI_COLOR_BITS, LCD_WIDTH, LCD_HEIGHT,
        LCD_DPI_HPW, LCD_DPI_HBP, LCD_DPI_HFP,
        LCD_DPI_VPW, LCD_DPI_VBP, LCD_DPI_VFP,
        LCD_DSI_PHY_LDO_ID
    );

    auto *panel = new LCD_EK79007(bus, LCD_WIDTH, LCD_HEIGHT, LCD_COLOR_BITS, LCD_RST_PIN);
    panel->configVendorCommands(lcd_init_cmd, sizeof(lcd_init_cmd) / sizeof(lcd_init_cmd[0]));

    return panel;
}

static void set_backlight(bool on)
{
    pinMode(LCD_BLK_PIN, OUTPUT);
    digitalWrite(LCD_BLK_PIN, on ? HIGH : LOW);
}

static bool draw_bitmap_wait(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *buffer)
{
    return (s_lcd != nullptr) &&
           s_lcd->drawBitmap(x, y, w, h, reinterpret_cast<const uint8_t *>(buffer), -1);
}

static void print_memory_info()
{
    Serial.printf("[LCD MEM] Heap free: %u bytes\r\n", ESP.getFreeHeap());
    Serial.printf("[LCD MEM] PSRAM total: %u bytes, free: %u bytes\r\n", ESP.getPsramSize(), ESP.getFreePsram());
    Serial.printf("[LCD MEM] DMA-capable heap free: %u bytes\r\n", heap_caps_get_free_size(MALLOC_CAP_DMA));
    Serial.printf("[LCD MEM] SPIRAM heap free: %u bytes\r\n", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

bool mipilcd_init()
{
    Serial.printf("[LCD 00] init start | physical=%dx%d color=%dbit lane=%d lane_rate=%dMbps dpi_clk=%dMHz\r\n",
                  LCD_WIDTH,
                  LCD_HEIGHT,
                  LCD_COLOR_BITS,
                  LCD_DSI_LANE_NUM,
                  LCD_DSI_LANE_RATE_MBPS,
                  LCD_DPI_CLK_MHZ);
    Serial.printf("[LCD 01] pins | rst=%d backlight=%d\r\n", LCD_RST_PIN, LCD_BLK_PIN);
    set_backlight(true);
    Serial.println("[LCD 02] backlight enabled");
    print_memory_info();

    if (ESP.getPsramSize() == 0) {
        Serial.println("[LCD ERR] PSRAM is not enabled. Enable PSRAM in Arduino IDE before using MIPI-DSI framebuffer.");
        return false;
    }

    Serial.println("[LCD 03] create DSI bus and EK79007 panel");
    s_lcd = create_lcd();
    if (s_lcd == nullptr) {
        Serial.println("[LCD ERR] create_lcd returned null");
        return false;
    }

    Serial.println("[LCD 04] panel begin");
    if (!s_lcd->begin()) {
        Serial.println("[LCD ERR] LCD begin failed");
        return false;
    }

    Serial.println("[LCD 05] disable DSI color bar pattern");
    s_lcd->DSI_ColorBarPatternTest(LCD::DSI_ColorBarPattern::NONE);
    s_lcd_ready = true;
    Serial.println("[LCD 06] LCD init done");
    return true;
}

bool mipilcd_is_ready()
{
    return s_lcd_ready;
}

LCD *mipilcd_get_panel()
{
    return s_lcd;
}

bool mipilcd_draw_rgb565(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *pixels)
{
    if ((s_lcd == nullptr) || (pixels == nullptr) || (w == 0) || (h == 0) ||
        (x >= LCD_WIDTH) || (y >= LCD_HEIGHT) ||
        (static_cast<uint32_t>(x) + w > LCD_WIDTH) ||
        (static_cast<uint32_t>(y) + h > LCD_HEIGHT)) {
        return false;
    }

    return draw_bitmap_wait(x, y, w, h, pixels);
}

bool mipilcd_clear(uint16_t color)
{
    return mipilcd_fill(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1, color);
}

bool mipilcd_fill(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye, uint16_t color)
{
    if ((s_lcd == nullptr) || (xs >= LCD_WIDTH) || (ys >= LCD_HEIGHT) || (xe < xs) || (ye < ys)) {
        return false;
    }

    if (xe >= LCD_WIDTH) {
        xe = LCD_WIDTH - 1;
    }
    if (ye >= LCD_HEIGHT) {
        ye = LCD_HEIGHT - 1;
    }

    const uint16_t width = xe - xs + 1;
    const uint16_t height = ye - ys + 1;
    const uint32_t pixels_per_chunk = static_cast<uint32_t>(width) * DRAW_CHUNK_ROWS;

    for (uint32_t i = 0; i < pixels_per_chunk; i++) {
        draw_buffer[i] = color;
    }

    uint16_t drawn_rows = 0;
    while (drawn_rows < height) {
        const uint16_t remaining_rows = height - drawn_rows;
        const uint16_t rows = (remaining_rows > DRAW_CHUNK_ROWS) ? DRAW_CHUNK_ROWS : remaining_rows;

        if (!draw_bitmap_wait(xs, ys + drawn_rows, width, rows, draw_buffer)) {
            Serial.printf("[LCD ERR] drawBitmap failed at x=%u y=%u w=%u h=%u\r\n",
                          xs,
                          ys + drawn_rows,
                          width,
                          rows);
            return false;
        }

        drawn_rows += rows;
    }

    return true;
}

void mipilcd_draw_point(uint16_t x, uint16_t y, uint16_t color)
{
    if ((x >= LCD_WIDTH) || (y >= LCD_HEIGHT)) {
        return;
    }

    draw_bitmap_wait(x, y, 1, 1, &color);
}

void mipilcd_draw_hline(uint16_t x, uint16_t y, uint16_t len, uint16_t color)
{
    if ((len == 0) || (x >= LCD_WIDTH) || (y >= LCD_HEIGHT)) {
        return;
    }

    mipilcd_fill(x, y, x + len - 1, y, color);
}

void mipilcd_draw_line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    int16_t dx = abs(static_cast<int16_t>(x2) - static_cast<int16_t>(x1));
    int16_t sx = (x1 < x2) ? 1 : -1;
    int16_t dy = -abs(static_cast<int16_t>(y2) - static_cast<int16_t>(y1));
    int16_t sy = (y1 < y2) ? 1 : -1;
    int16_t err = dx + dy;

    while (true) {
        mipilcd_draw_point(x1, y1, color);
        if ((x1 == x2) && (y1 == y2)) {
            break;
        }

        int16_t e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x1 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y1 += sy;
        }
    }
}

void mipilcd_draw_rectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    mipilcd_draw_line(x1, y1, x2, y1, color);
    mipilcd_draw_line(x1, y2, x2, y2, color);
    mipilcd_draw_line(x1, y1, x1, y2, color);
    mipilcd_draw_line(x2, y1, x2, y2, color);
}

static bool stroke_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    return mipilcd_fill(x, y, x + w - 1, y + h - 1, color);
}

static bool stroke_step_line(
    int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t thick, uint16_t color
) {
    const int16_t steps = max(abs(x1 - x0), abs(y1 - y0)) / 3 + 1;
    for (int16_t i = 0; i <= steps; i++) {
        const int16_t x = x0 + static_cast<int32_t>(x1 - x0) * i / steps;
        const int16_t y = y0 + static_cast<int32_t>(y1 - y0) * i / steps;
        if (!stroke_rect(x - thick / 2, y - thick / 2, thick, thick, color)) {
            return false;
        }
    }
    return true;
}

static bool draw_li(uint16_t x, uint16_t y, uint16_t color)
{
    const uint16_t t = 8;
    return stroke_rect(x + 12, y + 8, 72, t, color) &&
           stroke_rect(x + 44, y + 0, t, 46, color) &&
           stroke_rect(x + 20, y + 34, 56, t, color) &&
           stroke_step_line(x + 48, y + 34, x + 16, y + 56, t, color) &&
           stroke_step_line(x + 48, y + 34, x + 82, y + 56, t, color) &&
           stroke_rect(x + 18, y + 62, 60, t, color) &&
           stroke_rect(x + 44, y + 62, t, 28, color) &&
           stroke_rect(x + 28, y + 86, 52, t, color);
}

static bool draw_shang(uint16_t x, uint16_t y, uint16_t color)
{
    const uint16_t t = 9;
    return stroke_rect(x + 44, y + 8, t, 70, color) &&
           stroke_rect(x + 44, y + 38, 36, t, color) &&
           stroke_rect(x + 14, y + 82, 72, t, color);
}

static bool draw_qi(uint16_t x, uint16_t y, uint16_t color)
{
    const uint16_t t = 7;
    return stroke_rect(x + 4, y + 10, 34, t, color) &&
           stroke_rect(x + 4, y + 36, 34, t, color) &&
           stroke_rect(x + 4, y + 64, 34, t, color) &&
           stroke_rect(x + 20, y + 10, t, 62, color) &&
           stroke_rect(x + 44, y + 8, 46, t, color) &&
           stroke_rect(x + 64, y + 8, t, 24, color) &&
           stroke_step_line(x + 67, y + 26, x + 48, y + 42, t, color) &&
           stroke_step_line(x + 67, y + 26, x + 88, y + 42, t, color) &&
           stroke_rect(x + 44, y + 48, 48, t, color) &&
           stroke_rect(x + 86, y + 48, t, 42, color) &&
           stroke_rect(x + 50, y + 60, 28, t, color) &&
           stroke_rect(x + 50, y + 60, t, 28, color) &&
           stroke_rect(x + 74, y + 60, t, 28, color) &&
           stroke_rect(x + 50, y + 82, 28, t, color) &&
           stroke_rect(x + 44, y + 92, 46, t, color);
}

bool mipilcd_show_li_shang_qi(uint16_t x, uint16_t y, uint16_t color, uint16_t background)
{
    const uint16_t char_w = 96;
    const uint16_t char_h = 104;
    const uint16_t gap = 18;

    if (!mipilcd_fill(x, y, x + char_w * 3 + gap * 2 - 1, y + char_h - 1, background)) {
        return false;
    }

    return draw_li(x, y, color) &&
           draw_shang(x + char_w + gap, y, color) &&
           draw_qi(x + (char_w + gap) * 2, y, color);
}
