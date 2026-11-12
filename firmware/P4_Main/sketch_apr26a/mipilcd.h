/**
 ****************************************************************************************************
 * @file        mipilcd.h
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2025-01-01
 * @brief       mipilcd驱动代码
 * @license     Copyright (c) 2020-2032, 广州市星翼电子科技有限公司
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:正点原子 ESP32-P4 开发板
 * 在线视频:www.yuanzige.com
 * 技术论坛:www.openedv.com
 * 公司网址:www.alientek.com
 * 购买地址:openedv.taobao.com
 *
 ****************************************************************************************************
 */

#ifndef __MIPILCD_H
#define __MIPILCD_H

#include <Arduino.h>
#include <esp_display_panel.hpp>

#define LCD_NAME                EK79007
#define LCD_WIDTH               (720)
#define LCD_HEIGHT              (1280)
#define LCD_COLOR_BITS          (16)
#define LCD_DSI_PHY_LDO_ID      (3)     
#define LCD_DSI_LANE_NUM        (2)     
#define LCD_DSI_LANE_RATE_MBPS  (750)  
#define LCD_DPI_CLK_MHZ         (60)
#define LCD_DPI_COLOR_BITS      (16)

#define LCD_DPI_HPW             (8)
#define LCD_DPI_HBP             (52)
#define LCD_DPI_HFP             (48)
#define LCD_DPI_VPW             (5)
#define LCD_DPI_VBP             (15)
#define LCD_DPI_VFP             (16)

#define LCD_RST_PIN             (52)
#define LCD_BLK_PIN             (53)

/* 画笔颜色 */
#define WHITE               0xFFFF
#define BLACK               0x0000
#define BLUE                0x001F  
#define BRED                0XF81F
#define GRED                0XFFE0
#define GBLUE               0X07FF
#define RED                 0xF800
#define MAGENTA             0xF81F
#define GREEN               0x07E0
#define CYAN                0x7FFF
#define YELLOW              0xFFE0
#define BROWN               0XBC40      /* 棕色 */
#define BRRED               0XFC07      /* 棕红色 */
#define GRAY                0X8430      /* 灰色 */
#define DARKBLUE            0X01CF      /* 深蓝色 */
#define LIGHTBLUE           0X7D7C      /* 浅蓝色 */
#define GRAYBLUE            0X5458      /* 灰蓝色 */
/* 以上三色为PANEL的颜色  */
#define LIGHTGREEN          0X841F      /* 浅绿色 */
#define LGRAY               0XC618      /* 浅灰色(PANNEL),窗体背景色 */

#define LGRAYBLUE           0XA651      /* 浅灰蓝色(中间层颜色) */
#define LBBLUE              0X2B12      /* 浅棕蓝色(选择条目的反色) */

const esp_lcd_panel_vendor_init_cmd_t lcd_init_cmd[] = {
    ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0, 0x36, {0x01}),
    ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0, 0xB9, {0xFF, 0x83, 0x94}),
    ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0, 0xBA, {0x61, 0x03, 0x68, 0x6B, 0xB2, 0xC0}),
    ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0, 0xB1, {0x48, 0x12, 0x72, 0x09, 0x32, 0x54, 0x71, 0x71, 0x57, 0x47}),
    ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0, 0xB2, {0x00, 0x80, 0x64, 0x0C, 0x0D, 0x2F}),
    ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0, 0xB4, {0x73, 0x74, 0x73, 0x74, 0x73, 0x74, 0x01, 0x0C, 0x86, 0x75, 0x00, 0x3F, 0x73, 0x74, 0x73, 0x74, 0x73, 0x74, 0x01, 0x0C, 0x86}),
    ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0, 0xD3, {0x00, 0x00, 0x07, 0x07, 0x40, 0x07, 0x0C, 0x00, 0x08, 0x10, 0x08, 0x00, 0x08, 0x54, 0x15, 0x0A, 0x05, 0x0A, 0x02, 0x15, 0x06, 0x05, 0x06, 0x47, 0x44, 0x0A, 0x0A, 0x4B, 0x10, 0x07, 0x07, 0x0C, 0x40}),
    ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0, 0xD5, {0x1C, 0x1C, 0x1D, 0x1D, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x24, 0x25, 0x18, 0x18, 0x26, 0x27, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x20, 0x21, 0x18, 0x18, 0x18, 0x18}),
    ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0, 0xD6, {0x1C, 0x1C, 0x1D, 0x1D, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00, 0x0B, 0x0A, 0x09, 0x08, 0x21, 0x20, 0x18, 0x18, 0x27, 0x26, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x25, 0x24, 0x18, 0x18, 0x18, 0x18}),
    ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0, 0xB6, {0x6E, 0x6E}),
    ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0, 0xE0, {0x00, 0x0A, 0x15, 0x1B, 0x1E, 0x21, 0x24, 0x22, 0x47, 0x56, 0x65, 0x66, 0x6E, 0x82, 0x88, 0x8B, 0x9A, 0x9D, 0x98, 0xA8, 0xB9, 0x5D, 0x5C, 0x61, 0x66, 0x6A, 0x6F, 0x7F, 0x7F, 0x00, 0x0A, 0x15, 0x1B, 0x1E, 0x21, 0x24, 0x22, 0x47, 0x56, 0x65, 0x65, 0x6E, 0x81, 0x87, 0x8B, 0x98, 0x9D, 0x99, 0xA8, 0xBA, 0x5D, 0x5D, 0x62, 0x67, 0x6B, 0x72, 0x7F, 0x7F}),  
    ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0, 0xC0, {0x1F, 0x31}),
    ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0, 0xCC, {0x03}),
    ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0, 0xD4, {0x02}),
    ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0, 0xBD, {0x02}),
    ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0, 0xD8, {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}),
    ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0, 0xBD, {0x00}),
    ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0, 0xBD, {0x01}),
    ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0, 0xB1, {0x00}),
    ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0, 0xBD, {0x00}),
    ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0, 0xBF, {0x40, 0x81, 0x50, 0x00, 0x1A, 0xFC, 0x01}),
    ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0, 0xC6, {0xED}),

    ESP_PANEL_LCD_CMD_WITH_NONE_PARAM(120, 0x11),
    ESP_PANEL_LCD_CMD_WITH_NONE_PARAM(20, 0x29),
};

/* LCD的宽和高定义 */
extern uint16_t mipilcd_width;
extern uint16_t mipilcd_height;

/* 定义LCD启用的字体 */
#define FONT_12             1
#define FONT_16             1
#define FONT_24             1
#define FONT_32             1

/* 默认启用12号字体 */
#if ((FONT_12 == 0) && (FONT_16 == 0) && (FONT_24 == 0) && (FONT_32 == 0))
#undef FONT_12
#define FONT_12 1
#endif

/* LCD显示字体枚举 */
typedef enum
{
#if (FONT_12 != 0)
  LCD_FONT_12,             /* 12号字体 */
#endif
#if (FONT_16 != 0)
  LCD_FONT_16,             /* 16号字体 */
#endif
#if (FONT_24 != 0)
  LCD_FONT_24,             /* 24号字体 */
#endif
#if (FONT_32 != 0)
  LCD_FONT_32,             /* 32号字体 */
#endif
} lcd_font_t;

/* LCD显示数字模式枚举 */
typedef enum
{
  NUM_SHOW_NOZERO = 0x00,  /* 数字高位0不显示 */
  NUM_SHOW_ZERO,           /* 数字高位0显示 */
} num_mode_t;


bool mipilcd_init();
bool mipilcd_is_ready();
esp_panel::drivers::LCD *mipilcd_get_panel();
bool mipilcd_clear(uint16_t color);                                                           /* lcd清屏函数 */
bool mipilcd_fill(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye, uint16_t color);        /* lcd区域填充 */
bool mipilcd_draw_rgb565(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *pixels);

/* lcd功能函数 */
void mipilcd_draw_point(uint16_t x, uint16_t y, uint16_t color);                                  /* lcd画点 */
void mipilcd_draw_line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);       /* lcd画线段 */
void mipilcd_draw_hline(uint16_t x, uint16_t y, uint16_t len, uint16_t color);                    /* lcd画水平线 */
void mipilcd_draw_rectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);  /* lcd画矩形框 */
bool mipilcd_show_li_shang_qi(uint16_t x, uint16_t y, uint16_t color, uint16_t background);
void mipilcd_draw_circle(uint16_t x, uint16_t y, uint16_t r, uint16_t color);                     /* lcd画圆形框 */
void mipilcd_fill_circle(uint16_t x, uint16_t y, uint16_t r, uint16_t color);                     /* 填充实心圆 */

void mipilcd_show_char(uint16_t x, uint16_t y, char ch, lcd_font_t font, uint8_t mode, uint16_t color);                         /* lcd显示1个字符 */
void mipilcd_show_string(uint16_t x, uint16_t y, uint16_t width, uint16_t height, lcd_font_t font, char *str,  uint16_t color); /* lcd显示字符串 */
void mipilcd_show_xnum(uint16_t x, uint16_t y, uint32_t num, uint8_t len,  lcd_font_t font, num_mode_t mode, uint16_t color);   /* lcd显示数字，可控制显示高位0 */
void mipilcd_show_num(uint16_t x, uint16_t y, uint32_t num, uint8_t len, lcd_font_t font, uint16_t color);                      /* lcd显示数字，不显示高位0 */

#endif
