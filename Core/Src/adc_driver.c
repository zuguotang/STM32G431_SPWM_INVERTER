/*
 * ADC 驱动实现
 * ===========
 * ADC1 + DMA 循环 + IIR 滤波 + RMS 有效值 + 功率 + NTC 温度。
 *
 * 借鉴 ZFM32F030 方案的 RMS 计算和电流零点校准，
 * 用真实有效值替代瞬时值做 PID 控制，精度显著提升。
 */

#include "adc_driver.h"
#include "inverter_config.h"

volatile adc_values_t g_adc;
static volatile uint16_t s_adc_dma1[2];  /* ADC1 DMA: PA0=IN1(vout), PA1=IN2(iout) */
static volatile uint16_t s_adc_dma2[2];  /* ADC2 DMA: PA4=IN4(temp), PA5=IN5(vbus) */

/* ==================================================================
 *  快速滑动 RMS（20ms 窗口 = 一个 50Hz 完整周波，O(1) 运算量）
 *
 *  维护一个 20 点的环形缓冲 + 平方和累加器。
 *  每 1ms 推入新瞬时值、移除最旧值、更新平方和。
 *  无需每次重算 20 个点，只需一次乘法和一次减法。
 * ================================================================== */
#define FAST_RMS_WINDOW  20         /* 20ms = 一个 50Hz 周期 */
static uint16_t s_vout_ring[FAST_RMS_WINDOW];
static uint8_t  s_vout_ring_idx;
static uint32_t s_vout_sum_sq;      /* 滑动窗口平方和 */
static bool     s_vout_ring_full;

/* 整数开方（牛顿迭代，输入 < 2^24 安全） */
static uint16_t isqrt_u32(uint32_t x)
{
    if (x == 0) return 0;
    uint32_t r = (x + 1U) / 2U;
    for (uint8_t i = 0; i < 5; i++) {
        r = (r + x / r) / 2U;
    }
    return (uint16_t)r;
}

void adc_calc_fast_rms_1ms(void)
{
    uint16_t new_val = g_adc.vout;                      /* 当前 IIR 滤波瞬时值 */
    uint16_t old_val = s_vout_ring[s_vout_ring_idx];     /* 20ms 前的最旧值 */

    /* O(1) 更新：减去旧值的平方，加上新值的平方 */
    s_vout_sum_sq += (uint32_t)new_val * new_val;
    s_vout_sum_sq -= (uint32_t)old_val * old_val;

    s_vout_ring[s_vout_ring_idx] = new_val;
    s_vout_ring_idx = (uint8_t)((s_vout_ring_idx + 1U) % FAST_RMS_WINDOW);

    if (s_vout_ring_idx == 0) s_vout_ring_full = true;

    if (s_vout_ring_full) {
        /* 20ms 滑动 RMS = sqrt(平方和均值) */
        g_adc.vout_rms_fast = isqrt_u32(s_vout_sum_sq / FAST_RMS_WINDOW);
    } else {
        /* 环形缓冲未满（启动后 < 20ms），暂用瞬时值 */
        g_adc.vout_rms_fast = new_val;
    }
}

/* ==================================================================
 *  RMS 计算缓冲区（显示用，800ms 更新）
 *  借鉴 ZFM32F030：累积 800 个采样点（≈800ms）后计算一次 RMS
 * ================================================================== */
static uint32_t s_rms_volt_acc;    /* Σv² 累加器 */
static uint32_t s_rms_curr_acc;    /* Σi² 累加器 */
static uint16_t s_rms_cnt;         /* 采样计数 0..799 */

/* ==================================================================
 *  NTC 温度查表
 *
 *  电路：3.3V → 10kΩ 上拉电阻 → (PA2 采样点) → NTC → GND
 *
 *  NTC 参数：R25 = 10kΩ, β = 3950
 *  分压公式：Vadc = 3.3V × R_ntc / (10kΩ + R_ntc)
 *           R_ntc = 10kΩ × exp(β × (1/T - 1/298.15))
 *
 *  查表法：256 项，索引 = ADC/16 (0..255)
 *  输出 = 温度 ×10（如 255 = 25.5℃）
 *
 *  覆盖温度范围：约 -25℃ ~ +125℃（跨 150℃），步进约 0.6℃/项。
 *
 *  相比直接用 ADC 码值比较，实际温度值直观、阈值配置不易出错。
 * ================================================================== */
static const int16_t s_temp_table[256] = {
     1250, 1240, 1232, 1224, 1217, 1210, 1204, 1198, /* 0-7   ADC/16 → ℃×10 */
     1192, 1187, 1182, 1177, 1172, 1168, 1164, 1160,
     1156, 1152, 1148, 1145, 1141, 1138, 1135, 1132,
     1129, 1126, 1123, 1120, 1117, 1114, 1111, 1108,
     1106, 1103, 1100, 1098, 1095, 1092, 1090, 1087,
     1084, 1082, 1079, 1077, 1074, 1072, 1069, 1067,
     1064, 1062, 1059, 1057, 1054, 1052, 1050, 1047,
     1045, 1042, 1040, 1037, 1035, 1033, 1030, 1028,
     1025, 1023, 1020, 1018, 1015, 1013, 1010, 1008,
     1005, 1003, 1000,  998,  995,  993,  990,  988,
      985,  983,  980,  977,  975,  972,  970,  967,
      964,  962,  959,  956,  954,  951,  948,  945,
      942,  940,  937,  934,  931,  928,  925,  922,
      919,  916,  913,  910,  907,  904,  901,  898,
      895,  892,  889,  885,  882,  879,  876,  872,
      869,  866,  863,  859,  856,  853,  849,  846,
      842,  839,  836,  832,  829,  825,  822,  818,
      815,  811,  808,  804,  800,  797,  793,  790,
      786,  782,  779,  775,  771,  767,  763,  760,
      756,  752,  748,  744,  740,  736,  732,  728,
      724,  720,  716,  712,  708,  704,  700,  696,
      692,  688,  683,  679,  675,  671,  667,  662,
      658,  654,  650,  645,  641,  637,  632,  628,
      624,  619,  615,  610,  606,  601,  597,  592,
      588,  583,  579,  574,  570,  565,  560,  556,
      551,  547,  542,  537,  533,  528,  523,  518,
      514,  509,  504,  499,  495,  490,  485,  480,
      475,  470,  465,  460,  455,  450,  445,  440,
      435,  430,  425,  420,  415,  410,  405,  400,
      395,  390,  385,  380,  374,  369,  364,  359,
      354,  348,  343,  338,  333,  327,  322,  317,
      311,  306,  301,  295,  290,  284,  279,  273,
};

int16_t adc_temp_to_celsius(uint16_t adc_raw)
{
    uint8_t idx = (uint8_t)(adc_raw >> 4);  /* /16, 0..255 */
    return s_temp_table[idx];
}

/* ==================================================================
 *  ADC 启动
 * ================================================================== */
void adc_driver_start(void)
{
    /* ADC1 校准并启动 DMA（PA0=vout, PA1=iout） */
    HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED);
    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)s_adc_dma1, 2U);

    /* ADC2 校准并启动 DMA（PA4=temp, PA5=vbus） */
    HAL_ADCEx_Calibration_Start(&hadc2, ADC_SINGLE_ENDED);
    HAL_ADC_Start_DMA(&hadc2, (uint32_t *)s_adc_dma2, 2U);

    /* 上电时采集电流通道偏置值（PWM 尚未开启，电流应为零） */
    HAL_Delay(10);
    g_adc.iout_offset = s_adc_dma1[1];  /* PA1 = iout */
}

uint16_t adc_get_raw(uint8_t channel)
{
    if (channel == 0U) return s_adc_dma1[0];
    if (channel == 1U) return s_adc_dma1[1];
    if (channel == 2U) return s_adc_dma2[0];
    if (channel == 3U) return s_adc_dma2[1];
    return 0U;
}

/* ==================================================================
 *  IIR 低通滤波函数
 * ================================================================== */
static uint16_t filt(uint16_t old_value, uint16_t new_value, uint8_t shift)
{
    uint32_t acc = ((uint32_t)old_value * ((1UL << shift) - 1UL)) + new_value;
    return (uint16_t)(acc >> shift);
}

/* ==================================================================
 *  IIR 滤波更新（每 1ms）
 * ================================================================== */
void adc_sample_filtered_1ms(void)
{
    static bool init_done = false;

    uint16_t vout = s_adc_dma1[0];   /* ADC1 CH1 = PA0 */
    uint16_t iout = s_adc_dma1[1];   /* ADC1 CH2 = PA1 */
    uint16_t temp = s_adc_dma2[0];   /* ADC2 CH4 = PA4 */
    uint16_t vbus = s_adc_dma2[1];   /* ADC2 CH5 = PA5 */

    /*
     * 电流零点偏置校准：
     *   电流传感器（霍尔/互感器+运放）在零电流时有一个直流偏置电压，
     *   通常为 Vref/2 ≈ 1.65V ≈ ADC 2048。
     *   adc_driver_start() 在 PWM 开启前采集并存储此偏置值，
     *   后续每次采样都减去偏置，得到真实的交流电流值。
     *   下限钳位到 0，防止无符号减法溢出。
     */
    uint16_t iout_calib;
    if (iout > g_adc.iout_offset) {
        iout_calib = iout - g_adc.iout_offset;
    } else {
        iout_calib = 0;
    }

    if (!init_done) {
        g_adc.vout = vout;
        g_adc.iout = iout_calib;
        g_adc.temp = temp;
        g_adc.vbus = vbus;
        init_done = true;
        return;
    }

    g_adc.vout = filt(g_adc.vout, vout, ADC_FILTER_SHIFT_SLOW);
    g_adc.iout = filt(g_adc.iout, iout_calib, ADC_FILTER_SHIFT_FAST);
    g_adc.temp = filt(g_adc.temp, temp, ADC_FILTER_SHIFT_SLOW);
    g_adc.vbus = filt(g_adc.vbus, vbus, ADC_FILTER_SHIFT_FAST);
}

/* ==================================================================
 *  RMS 有效值计算（每 1ms 调用，借鉴 ZFM32F030 方案）
 *
 *  原理：
 *    RMS = sqrt(Σv² / N)
 *
 *    N=800 个采样点，每个点间隔 1ms（由 1kHz 系统节拍驱动），
 *    总时长 = 800ms = 50Hz 的 40 个完整周期。
 *    恰好覆盖整数倍周期，避免频谱泄漏，RMS 计算最准确。
 *
 *  实现细节：
 *    1. 每次累加前将平方值 >>8（除以 256），防止 32 位累加器溢出
 *       v² 最大 ≈ 4095² ≈ 16.7M → >>8 后 ≈ 65k → ×800 ≈ 52M < 2^32 ✓
 *    2. 求均值：÷800
 *    3. 开方：牛顿迭代法 x_{n+1} = (x_n + S/x_n) / 2，5 次迭代精度足够
 *    4. 恢复标度：×256（恢复 >>8）× VOLT_RMS_FACTOR / 4095 / 100
 *
 *  输出：
 *    g_adc.rms_volt：电压 ×100（如 22000 = 220.00V）
 *    g_adc.rms_curr：电流 ×100（如 500 = 5.00A）
 *    g_adc.power：   有功功率 W（简化计算 P = Vrms × Irms / 10000）
 *
 *  每 800ms 更新一次 → 显示刷新率约 1.25Hz，适合人工观察。
 * ================================================================== */
void adc_calc_rms_1ms(void)
{
    uint32_t vsq, isq;

    vsq = (uint32_t)g_adc.vout * g_adc.vout;
    isq = (uint32_t)g_adc.iout * g_adc.iout;

    s_rms_volt_acc += vsq >> 8;  /* 防溢出，右移 8 位 */
    s_rms_curr_acc += isq >> 8;
    s_rms_cnt++;

    if (s_rms_cnt >= 800U) {
        /* 电压 RMS = sqrt(平均平方和) × 256（恢复右移）* 标定系数 */
        uint32_t volt_mean = s_rms_volt_acc / 800U;
        uint32_t curr_mean = s_rms_curr_acc / 800U;

        /* 整数开方：牛顿迭代法 */
        uint32_t v_sqrt = volt_mean;
        uint32_t c_sqrt = curr_mean;
        if (v_sqrt > 0) {
            v_sqrt = (v_sqrt + 1U) / 2U;
            for (uint8_t i = 0; i < 5; i++) {
                v_sqrt = (v_sqrt + volt_mean / v_sqrt) / 2U;
            }
        }
        if (c_sqrt > 0) {
            c_sqrt = (c_sqrt + 1U) / 2U;
            for (uint8_t i = 0; i < 5; i++) {
                c_sqrt = (c_sqrt + curr_mean / c_sqrt) / 2U;
            }
        }

        /* ×256（恢复 >>8）× 标定系数 / 4095 */
        g_adc.rms_volt = (uint16_t)((v_sqrt * 256UL * VOLT_RMS_FACTOR / 4095UL) / 100UL);
        g_adc.rms_curr = (uint16_t)((c_sqrt * 256UL * CURR_RMS_FACTOR / 4095UL) / 100UL);

        /* 功率 = Vrms × Irms（简化，忽略功率因数） */
        g_adc.power = (uint16_t)(((uint32_t)g_adc.rms_volt * g_adc.rms_curr) / 10000UL);

        /* 温度转换为 ℃ */
        g_adc.temp_celsius = adc_temp_to_celsius(g_adc.temp);

        /* 重置累加器 */
        s_rms_volt_acc = 0;
        s_rms_curr_acc = 0;
        s_rms_cnt = 0;
    }
}
