#ifndef MAIN_H
#define MAIN_H

/*
 * 主头文件
 * -------
 * 声明所有 HAL 外设句柄和 Error_Handler。
 * 每个需要访问外设的模块都包含此头文件即可获得外设句柄的 extern 声明。
 *
 * 外设句柄由 main.c 定义，HAL 库通过句柄管理外设状态。
 * 句柄在 MX_xxx_Init() 中配置，在 main() 中依次调用初始化。
 */

#include "stm32g4xx_hal.h"
#include "inverter_config.h"

/* ADC1 + DMA 句柄：PA0/PA1(IN1/IN2) */
extern ADC_HandleTypeDef hadc1;
extern DMA_HandleTypeDef hdma_adc1;

/* ADC2 + DMA 句柄：PA4/PA5(IN4/IN5) */
extern ADC_HandleTypeDef hadc2;
extern DMA_HandleTypeDef hdma_adc2;

/* TIM1 句柄：22 kHz 互补 SPWM + BKIN 硬件刹车 */
extern TIM_HandleTypeDef htim1;

/* TIM6 句柄：1 ms 系统时基（170 MHz / 170 = 1 MHz，ARR=999 → 1 kHz） */
extern TIM_HandleTypeDef htim6;

/* USART2 句柄：调试串口 115200-8-N-1 */
extern UART_HandleTypeDef huart2;

/* I2C1 句柄：SSD1306 OLED */
extern I2C_HandleTypeDef hi2c1;

/* HAL 初始化异常时调用，禁用全局中断并死循环 */
void Error_Handler(void);

#endif
