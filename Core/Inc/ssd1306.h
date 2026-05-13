#ifndef SSD1306_H
#define SSD1306_H

/*
 * SSD1306 OLED 驱动 (I2C)
 * =======================
 * 128×64 像素，I2C 地址 0x3C。
 *
 * PB6 = SCL (I2C1), PB7 = SDA (I2C1)
 * 仅需 2 脚 + VCC/GND。
 *
 * 帧缓冲 1024 字节 (128×64/8)，分 8 页（page）非阻塞刷新。
 * 每 1ms 发送一页（128 字节），8ms 完成整屏刷新。
 */

#include <stdint.h>
#include <stdbool.h>

/* 屏幕尺寸 */
#define SSD1306_WIDTH   128
#define SSD1306_HEIGHT  64
#define SSD1306_PAGES   8       /* 64/8 */
#define SSD1306_FB_SIZE 1024    /* 128*8 */

/* I2C 地址 (7-bit: 0x3C, shifted for HAL: 0x78) */
#define SSD1306_I2C_ADDR  (0x3C << 1)

/* ==================================================================
 *  初始化
 * ================================================================== */

/* 需在 I2C1 初始化后调用 */
void ssd1306_init(void);

/* ==================================================================
 *  帧缓冲操作
 * ================================================================== */

void ssd1306_clear(void);
void ssd1306_set_pixel(uint8_t x, uint8_t y, bool on);
void ssd1306_draw_char(uint8_t x, uint8_t y, char c);
void ssd1306_draw_string(uint8_t x, uint8_t y, const char *str);
void ssd1306_draw_hline(uint8_t x, uint8_t y, uint8_t w, bool on);
void ssd1306_fill_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, bool on);

/* ==================================================================
 *  刷新控制
 * ================================================================== */

void ssd1306_request_refresh(void);
bool ssd1306_refresh_tick_1ms(void);
bool ssd1306_is_busy(void);
uint8_t *ssd1306_get_framebuffer(void);

#endif
