#ifndef ADC_DRIVER_H
#define ADC_DRIVER_H

/*
 * ADC 驱动层
 * ---------
 * ADC1 + DMA 四通道连续扫描 + RMS 有效值 + 功率计算 + NTC 温度。
 *
 * DMA 通道映射：
 *   [0] CH1 → PA0 → 输出电压瞬时值
 *   [1] CH2 → PA1 → 输出电流瞬时值
 *   [2] CH3 → PA2 → NTC 温度传感器
 *   [3] CH4 → PA3 → 母线电压
 */

#include "main.h"

typedef struct {
    /* 瞬时值（IIR 滤波后） */
    uint16_t vout;           /* 输出电压瞬时 ADC 值 */
    uint16_t iout;           /* 输出电流瞬时 ADC 值 */
    uint16_t temp;           /* NTC ADC 原始值 */
    uint16_t vbus;           /* 母线电压 ADC 值 */

    /* 有效值 */
    uint16_t rms_volt;       /* 电压真有效值（×100，如 22000 = 220.00V） */
    uint16_t rms_curr;       /* 电流真有效值（×100，如 500 = 5.00A） */
    uint16_t power;          /* 有功功率（W） */

    /* 温度 */
    int16_t  temp_celsius;   /* 温度 ℃（×10，如 255 = 25.5℃） */

    /* 电流零点偏置（上电自动校准） */
    uint16_t iout_offset;
} adc_values_t;

extern volatile adc_values_t g_adc;

void adc_driver_start(void);
void adc_sample_filtered_1ms(void);
uint16_t adc_get_raw(uint8_t channel);

/* RMS 有效值计算（每 1ms 调用，内部累积 800 个采样点后更新） */
void adc_calc_rms_1ms(void);

/* 转换为实际温度 ℃ */
int16_t adc_temp_to_celsius(uint16_t adc_raw);

#endif
