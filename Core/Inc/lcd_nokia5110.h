#ifndef LCD_NOKIA5110_H
#define LCD_NOKIA5110_H

/*
 * Nokia 5110 LCD 驱动 (PCD8544)
 * =============================
 * 84×48 像素单色 LCD，SPI 接口（写-only）。
 *
 * 软件 SPI（GPIO 位操作），无需硬件 SPI 外设。
 * 帧缓冲 504 字节 (84×48/8)，分 6 行组（bank）刷新。
 *
 * 每个 1ms 发送一组（84 字节），6ms 完成整屏刷新。
 * 通过 lcd_is_busy() 查询是否正在传输。
 *
 * 引脚：
 *   PB10 = SCE (片选，低有效)
 *   PA12 = RST (复位，低有效)
 *   PA15 = D/C (数据/命令：高=数据，低=命令)
 *   PA11 = DIN (MOSI)
 *   PA5  = CLK (时钟)
 */

#include <stdint.h>
#include <stdbool.h>

/* LCD 尺寸 */
#define LCD_WIDTH   84
#define LCD_HEIGHT  48
#define LCD_BANKS   6       /* 48/8 = 6 行组 */
#define LCD_FB_SIZE 504     /* 84*6 = 504 字节 */

/* ==================================================================
 *  初始化与硬件控制
 * ================================================================== */

/* 配置 GPIO（推挽输出），初始化 PCD8544 */
void lcd_init(void);

/* ==================================================================
 *  帧缓冲操作
 * ================================================================== */

/* 清空帧缓冲（不立即刷新，需调用 lcd_request_refresh） */
void lcd_clear(void);

/* 设置帧缓冲中的单个像素 (0≤x<84, 0≤y<48) */
void lcd_set_pixel(uint8_t x, uint8_t y, bool on);

/* 在 (x,y) 处绘制 5×7 ASCII 字符（像素坐标，非 tile） */
void lcd_draw_char(uint8_t x, uint8_t y, char c);

/* 绘制字符串 */
void lcd_draw_string(uint8_t x, uint8_t y, const char *str);

/* 绘制水平线 */
void lcd_draw_hline(uint8_t x, uint8_t y, uint8_t w, bool on);

/* 用指定字节填充矩形区域 */
void lcd_fill_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, bool on);

/* ==================================================================
 *  刷新控制
 * ================================================================== */

/* 标记帧缓冲脏，启动逐组刷新 */
void lcd_request_refresh(void);

/* 每 1ms 调用：发送下一组数据到 LCD。
 * 返回 true 表示整屏刷新完成（空闲）。 */
bool lcd_refresh_tick_1ms(void);

/* LCD 是否正在传输中 */
bool lcd_is_busy(void);

/* 获取帧缓冲指针（菜单渲染器直接写） */
uint8_t *lcd_get_framebuffer(void);

#endif
