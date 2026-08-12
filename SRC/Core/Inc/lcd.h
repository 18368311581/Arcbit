#ifndef __LCD_H__
#define __LCD_H__
#ifdef __cplusplus
extern "C" {
#endif
#include "basic.h"

/* ILI9341 采用 4 线 SPI，CS、RST 低有效，背光 PWM 高电平有效。 */
#ifndef LCD_SPI_MODE
#define LCD_SPI_MODE            3U
#endif
#ifndef LCD_SPI_PRESCALER
#define LCD_SPI_PRESCALER       SPI_BAUDRATEPRESCALER_8
#endif
#define LCD_SPI_SPEED_3_125MHZ        3125000UL
#define LCD_SPI_SPEED_6_25MHZ         6250000UL
#define LCD_SPI_SPEED_12_5MHZ         12500000UL
#define LCD_SPI_SPEED_25MHZ           25000000UL
#define LCD_SPI_SPEED_50MHZ           50000000UL
#define LCD_SPI_SPEED_NORMAL_HZ       LCD_SPI_SPEED_12_5MHZ
#define LCD_SPI_SPEED_HIGH_HZ         LCD_SPI_SPEED_25MHZ
#define LCD_SPI_SPEED_EXPERIMENTAL_HZ LCD_SPI_SPEED_50MHZ
#define LCD_CS_Pin              GPIO_PIN_4
#define LCD_CS_GPIO_Port        GPIOA
#define LCD_SCK_Pin             GPIO_PIN_5
#define LCD_SCK_GPIO_Port       GPIOA
#define LCD_DC_Pin              GPIO_PIN_6
#define LCD_DC_GPIO_Port        GPIOA
#define LCD_MOSI_Pin            GPIO_PIN_7
#define LCD_MOSI_GPIO_Port      GPIOA
#define LCD_BL_PWM_Pin          GPIO_PIN_8
#define LCD_BL_PWM_GPIO_Port    GPIOA
#define LCD_RST_Pin             GPIO_PIN_12
#define LCD_RST_GPIO_Port       GPIOA
#define LCD_WIDTH               320U
#define LCD_HEIGHT              240U
#define LCD_COLOR_BLACK         0x0000U
#define LCD_COLOR_WHITE         0xFFFFU
#define LCD_COLOR_RED           0xF800U
#define LCD_COLOR_GREEN         0x07E0U
#define LCD_COLOR_BLUE          0x001FU

/* 初始化 GPIO、SPI1、DMA2 Stream3 和背光 PWM。返回 0 成功，非零失败。 */
uint8_t Lcd_init(void);
/* 运行时切换 3.125/6.25/12.5/25/50 MHz；正式运行使用 12.5/25 MHz，50 MHz 仅用于诊断测试。 */
uint8_t Lcd_set_spi_speed(uint32_t speed_hz);
uint32_t Lcd_get_spi_speed(void);
/* 设置背光亮度，percent 范围为 0～100。 */
void Lcd_set_backlight(uint8_t percent);
/* 使用 RGB565 颜色清屏。返回 0 成功，非零失败。 */
uint8_t Lcd_clear(uint16_t color);
/* 填充矩形，超出屏幕的部分自动裁剪。 */
uint8_t Lcd_fill_rect(uint16_t x,uint16_t y,uint16_t width,uint16_t height,uint16_t color);
/* 绘制像素、直线、矩形和圆，坐标均为横屏坐标。 */
uint8_t Lcd_draw_pixel(uint16_t x,uint16_t y,uint16_t color);
uint8_t Lcd_draw_line(int16_t x0,int16_t y0,int16_t x1,int16_t y1,uint16_t color);
uint8_t Lcd_draw_rect(uint16_t x,uint16_t y,uint16_t width,uint16_t height,uint16_t color);
uint8_t Lcd_draw_circle(int16_t x0,int16_t y0,uint16_t radius,uint16_t color);
/* 绘制本机 uint16_t RGB565 图片，驱动内部负责转换字节序。 */
uint8_t Lcd_draw_image(uint16_t x,uint16_t y,uint16_t width,uint16_t height,const uint16_t* image);
/* 绘制高字节在前的 RGB565 字节流，可直接用于外部 Flash 图片资源。 */
uint8_t Lcd_draw_image_bytes(uint16_t x,uint16_t y,uint16_t width,uint16_t height,const uint8_t* image);
/*
 * 统一绘制 ASCII 与 GBK 文本。ASCII 支持 12、16、24、32 点阵；
 * GBK 汉字支持 16、24 点阵；GBK 汉字在 12、32 点阵时绘制为空白并继续。
 * 输入字符串使用 GBK 字节序列。
 */
uint8_t Lcd_draw_text(uint16_t x,uint16_t y,const char* text,uint16_t color,uint16_t background,uint8_t font_size);
/* 透明绘制只写前景像素，输入和点阵尺寸规则同 Lcd_draw_text。 */
uint8_t Lcd_draw_text_transparent(uint16_t x,uint16_t y,const char* text,uint16_t color,uint8_t font_size);
/* LCD 休眠会关闭背光；唤醒后恢复休眠前的背光亮度。 */
uint8_t Lcd_sleep(void);
uint8_t Lcd_wake(void);

#ifdef __cplusplus
}
#endif
#endif
