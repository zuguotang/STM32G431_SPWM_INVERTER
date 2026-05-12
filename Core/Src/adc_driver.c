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
static volatile uint16_t s_adc_dma[4];

/* ==================================================================
 *  RMS 计算缓冲区
 *  借鉴 ZFM32F030：累积 800 个采样点（≈800ms）后计算一次 RMS
 * ================================================================== */
static uint32_t s_rms_volt_acc;    /* Σv² 累加器 */
static uint32_t s_rms_curr_acc;    /* Σi² 累加器 */
static uint16_t s_rms_cnt;         /* 采样计数 0..799 */

/* ==================================================================
 *  NTC 温度查表（β=3950, R25=10kΩ, 上拉 10kΩ → 3.3V）
 *
 * 表索引 = ADC值 / 16，对应温度 ×10（如 255 = 25.5℃）。
 * 共 256 项，覆盖 ADC 0..4080。
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
    HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED);
    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)s_adc_dma, 4U);

    /*
     * 上电时采集电流通道偏置值（PWM 尚未开启，电流应为零）
     * 借鉴 ZFM32F030 的 GetOffsetCurrent() 思路。
     */
    HAL_Delay(10);
    g_adc.iout_offset = s_adc_dma[1];
}

uint16_t adc_get_raw(uint8_t channel)
{
    if (channel >= 4U) return 0U;
    return s_adc_dma[channel];
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

    uint16_t vout = s_adc_dma[0];
    uint16_t iout = s_adc_dma[1];
    uint16_t temp = s_adc_dma[2];
    uint16_t vbus = s_adc_dma[3];

    /* 电流减去上电校准的零点偏置 */
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
 *  RMS 有效值计算（每 1ms 调用，借鉴 ZFM32F030）
 *
 *  RMS = sqrt(Σv²/N) × 标定系数
 *
 *  800 个采样点 ≈ 800ms，覆盖 40 个 50Hz 完整周期。
 *  每 800ms 更新一次 g_adc.rms_volt 和 g_adc.rms_curr。
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
