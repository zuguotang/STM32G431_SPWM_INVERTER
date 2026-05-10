/*
 * ADC 驱动实现
 * ===========
 * ADC1 + DMA 循环模式，四通道连续采样。
 *
 * DMA 缓冲 s_adc_dma[4] 由硬件自动更新，映射为：
 *   [0] → ADC_CH1 (AIN1, PA0) → 输出电压
 *   [1] → ADC_CH2 (AIN2, PA1) → 输出电流
 *   [2] → ADC_CH3 (AIN3, PA2) → NTC 温度
 *   [3] → ADC_CH4 (AIN4, PA3) → 母线电压
 *
 * 一阶 IIR 低通滤波：
 *   new = (old × (2^shift - 1) + raw) / 2^shift
 *
 * 电流/母线电压用快速滤波 (shift=2, α=1/4)，
 * 电压/温度用慢速滤波 (shift=3, α=1/8)。
 */

#include "adc_driver.h"

volatile adc_values_t g_adc;           /* 全局滤波值 */
static volatile uint16_t s_adc_dma[4]; /* DMA 循环缓冲 */

/* ==================================================================
 *  ADC 启动
 * ================================================================== */
void adc_driver_start(void)
{
    /*
     * STM32G4 的 ADC 在首次使用前必须校准。
     * 校准补偿内部采样电容的制造偏差。
     * HAL_ADCEx_Calibration_Start 内部会等待校准完成。
     */
    HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED);

    /*
     * 启动 ADC + DMA 连续转换
     * ----------------------
     * DMA 循环模式：ADC 每完成 4 个通道的一轮扫描，
     * 自动将结果写入 s_adc_dma[0..3]，然后重新开始。
     * CPU 只需在 1 ms 任务中读取滤波即可。
     */
    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)s_adc_dma, 4U);
}

/* ==================================================================
 *  单通道原始读取
 * ================================================================== */
uint16_t adc_get_raw(uint8_t channel)
{
    if (channel >= 4U) {
        return 0U;
    }
    return s_adc_dma[channel];
}

/* ==================================================================
 *  一阶低通滤波函数
 * ================================================================== */
static uint16_t filt(uint16_t old_value, uint16_t new_value, uint8_t shift)
{
    /*
     * IIR 滤波：new_val = old_val × (1-α) + raw × α
     *
     * 使用移位代替 α 的浮点乘法：
     *   α = 1/2^shift
     *   new_val = (old_val × (2^shift - 1) + raw) / 2^shift
     *
     * shift=2 (α=1/4): (old×3 + new) / 4  → 截止频率约 40 Hz
     * shift=3 (α=1/8): (old×7 + new) / 8  → 截止频率约 20 Hz
     */
    uint32_t acc = ((uint32_t)old_value * ((1UL << shift) - 1UL)) + new_value;
    return (uint16_t)(acc >> shift);
}

/* ==================================================================
 *  1 ms 滤波更新
 * ================================================================== */
void adc_sample_filtered_1ms(void)
{
    static bool init_done = false;

    /* 从 DMA 缓冲读取原始值快照 */
    uint16_t vout = s_adc_dma[0];
    uint16_t iout = s_adc_dma[1];
    uint16_t temp = s_adc_dma[2];
    uint16_t vbus = s_adc_dma[3];

    /*
     * 首次调用：直接赋值（跳过滤波）
     * 避免启动时 g_adc 初值为 0 导致 PID 误判
     * （例如误以为输出电压严重偏低而瞬间满调制输出）。
     */
    if (!init_done) {
        g_adc.vout = vout;
        g_adc.iout = iout;
        g_adc.temp = temp;
        g_adc.vbus = vbus;
        init_done = true;
        return;
    }

    /*
     * 运行阶段：一阶 IIR 低通滤波
     *
     * 电压/温度 → 慢速滤波 (α=1/8)：变化缓慢，需要高噪声抑制
     * 电流/母线 → 快速滤波 (α=1/4)：需要快速响应保护
     */
    g_adc.vout = filt(g_adc.vout, vout, ADC_FILTER_SHIFT_SLOW);
    g_adc.iout = filt(g_adc.iout, iout, ADC_FILTER_SHIFT_FAST);
    g_adc.temp = filt(g_adc.temp, temp, ADC_FILTER_SHIFT_SLOW);
    g_adc.vbus = filt(g_adc.vbus, vbus, ADC_FILTER_SHIFT_FAST);
}
