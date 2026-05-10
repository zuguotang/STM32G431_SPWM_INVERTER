#ifndef ADC_DRIVER_H
#define ADC_DRIVER_H

/*
 * ADC 驱动层
 * ---------
 * 使用 ADC1 + DMA 实现四通道连续扫描采样：
 *   ADC_CH1 (PA0) → 输出电压
 *   ADC_CH2 (PA1) → 输出电流（电流互感器/采样电阻）
 *   ADC_CH3 (PA2) → NTC 温度
 *   ADC_CH4 (PA3) → 母线电压
 *
 * DMA 循环模式将 ADC 结果自动传输到 s_adc_dma[4] 数组，
 * CPU 零干预，1 ms 任务只需读取数组并做低通滤波。
 *
 * 相比 STM8S 的手动轮询 ADC，DMA 模式下 CPU 开销极低。
 */

#include "main.h"

/*
 * ADC 采样值结构体（12 位，0..4095）
 * 存储滤波后的值，供 PID 控制和保护检测使用。
 */
typedef struct {
    uint16_t vout;   /* 输出电压，慢速滤波 */
    uint16_t iout;   /* 输出电流，快速滤波 */
    uint16_t temp;   /* NTC 温度，慢速滤波 */
    uint16_t vbus;   /* 母线电压，快速滤波 */
} adc_values_t;

/* 全局 ADC 滤波值 */
extern volatile adc_values_t g_adc;

/*
 * 启动 ADC + DMA 连续采样
 * ----------------------
 * 先执行 ADC 校准（STM32G4 要求），
 * 再以 DMA 循环模式启动 4 通道连续扫描。
 * 必须在所有外设初始化完成后调用。
 */
void adc_driver_start(void);

/*
 * 1 ms 滤波更新任务
 * ---------------
 * 从 DMA 缓冲区读取原始值，执行一阶低通滤波。
 * 电流和母线电压用快速滤波 (α=1/4)，
 * 电压和温度用慢速滤波 (α=1/8)。
 * 首次调用时直接赋值，跳过初始化瞬态。
 */
void adc_sample_filtered_1ms(void);

/* 读取单通道原始 ADC 值（直接读 DMA 缓冲，不滤波） */
uint16_t adc_get_raw(uint8_t channel);

#endif
