#ifndef INVERTER_CONFIG_H
#define INVERTER_CONFIG_H

/*
 * 系统配置文件
 * -----------
 * 本文件集中管理所有参数。
 *
 * 章节：
 *   1. 时钟与 PWM（编译期固定）
 *   2. DDS 相位步进（编译期固定）
 *   3. ADC 阈值（编译期固定）
 *   4. 保护时间常数（部分运行时可调）
 *   5. PID 参数（部分运行时可调）
 *   6. GPIO 引脚映射（编译期固定）
 *   7. 运行时可调参数（通过 LCD+按键修改，保存到 Flash）
 *
 * 运行时可调参数通过 g_params 结构体访问，
 * 宏定义自动重定向到 g_params 的对应字段。
 */

#include "inverter_types.h"

/* ==================================================================
 *  第 1 节：时钟与 PWM（编译期固定，不可运行时修改）
 * ================================================================== */

#define SYSCLK_HZ                       170000000UL
#define PWM_TIMER_CLOCK_HZ              SYSCLK_HZ
#define PWM_CARRIER_HZ                  22000UL
#define PWM_PERIOD_TICKS                ((uint16_t)((PWM_TIMER_CLOCK_HZ / PWM_CARRIER_HZ) - 1UL))
#define PWM_MIN_CCR_TICKS               24U

#define SPWM_DEFAULT_MODE               SPWM_MODE_UNIPOLAR
#define SPWM_DEFAULT_FREQ               AC_FREQ_50HZ

/* 继电器延时：输出电压稳定后再吸合，避免带载切换 */
#define SPWM_RELAY_DELAY_MS             800UL

/*
 * 系统电压平台选择（编译开关）
 *
 *   HV (0): 高压母线 — 典型应用 310V DC 母线 → 220V AC 输出
 *            母线分压比大（如 3kΩ+500kΩ），ADC 读数小
 *   LV (1): 低压母线 — 典型应用 12V/24V 电池 → 前级推挽升压 → 220V AC
 *            母线分压比小（如 3kΩ+51kΩ），ADC 读数大
 *
 *  选择后自动切换：ADC 标定系数 (VOLT_RMS_FACTOR 等)
 *                  和母线电压分压系数 (VBUS_FACTOR)
 *
 *  注意：电流标定系数 (CURR_RMS_FACTOR) 取决于电流传感器硬件，
 *        与母线电压无关，LV/HV 可能相同。
 */
#define HV  0
#define LV  1
#define SYSVOLT  (HV)  /* ← 在此切换：0=高压版, 1=低压版 */

/* ==================================================================
 *  ADC → 物理量标定系数
 *
 *  计算公式：物理值 = ADC值 × FACTOR / 4095
 *  刻度量纲：电压 ×100（如 22000 = 220.00V），电流 ×100（如 500 = 5.00A）
 * ================================================================== */
#if (SYSVOLT == LV)
#define VOLT_RMS_FACTOR     22132UL   /* 低压版电压标定: 220V → ADC≈2480 时的系数 */
#define CURR_RMS_FACTOR     19970UL   /* 低压版电流标定 */
#define VBUS_FACTOR         17705UL   /* 低压版母线标定 */
#define POWER_FACTOR        132UL
#else
#define VOLT_RMS_FACTOR     22132UL   /* 高压版电压标定 */
#define CURR_RMS_FACTOR     19970UL   /* 高压版电流标定 */
#define VBUS_FACTOR         88528UL   /* 高压版母线标定 (分压比大) */
#define POWER_FACTOR        132UL
#endif

/* ==================================================================
 *  第 2 节：DDS 相位步进（编译期固定）
 * ================================================================== */

#define PHASE_STEP_50HZ                 9761289UL
#define PHASE_STEP_60HZ                 11713547UL

/* ==================================================================
 *  第 3 节：ADC 阈值（编译期固定，与硬件电路绑定）
 * ================================================================== */

#define ADC_MAX_CODE                    4095U
#define VOUT_ADC_LOW_LOCK               480U
#define VBUS_ADC_MIN_RUN                1040U
#define VBUS_ADC_MAX_RUN                3900U
/*
 * 两级过流保护（同一 ADC 通道，不同延时）
 * 软件短路已移除——真短路由硬件 BKIN + PA4 处理（纳秒级 + 重试计数）
 *
 * Lv1 过载 100%: >3040, 5s → 锁存 FAULT_OVERLOAD
 * Lv2 过流 110%: >3344, 2s → 锁存 FAULT_OVERLOAD
 * Lv3 过流 120%: >3648, 0.5s → 锁存 FAULT_OVERLOAD（逼近短路）
 */
#define IOUT_ADC_OVERLOAD               3040U     /* 100% 基线 */
#define IOUT_ADC_OVERLOAD_LV2_PCT       110U      /* 110% */
#define IOUT_ADC_OVERLOAD_LV3_PCT       120U      /* 120% */
/* 温度阈值（实际 ℃×10，如 600 = 60.0℃），使用 NTC 查表转换 */
#define TEMP_FAN_ON_CELSIUS             400    /* 40.0℃ 开风扇 */
#define TEMP_OVER_CELSIUS               600    /* 60.0℃ 过温保护 */
#define TEMP_RECOVER_CELSIUS            500    /* 50.0℃ 恢复 */

#define ADC_FILTER_SHIFT_FAST           2U
#define ADC_FILTER_SHIFT_SLOW           3U

/* ==================================================================
 *  第 4 节：保护时间常数
 * ================================================================== */

#define STARTUP_CURRENT_BLANK_MS        1000UL
/* OVERLOAD_TRIP_MS → 运行时可调（Lv1基线），见第 7 节 */
/* Lv2/Lv3 延时 = OVERLOAD_TRIP_MS 的百分比 */

#define OVERLOAD_LV1_TRIP_MS            5000UL  /* 100% 过载: 5s */
#define OVERLOAD_LV2_TRIP_MS            2000UL  /* 110% 过流: 2s */
#define OVERLOAD_LV3_TRIP_MS            500UL   /* 120% 过流: 0.5s（逼近短路） */

#define OVERLOAD_RECOVER_MS             500UL
#define SHORT_RETRY_DELAY_MS            2000UL
#define SHORT_RETRY_MAX                 3U
#define UNDER_BUS_TRIP_MS               80UL
#define OUTPUT_LOW_TRIP_MS              700UL

/* ==================================================================
 *  第 5 节：PID 参数
 * ================================================================== */

/* PID_KP_Q10 / PID_KI_Q10 / PID_OUT_START_LIMIT → 运行时可调，见第 7 节 */

#define PID_KD_Q10                      0
#define PID_I_MIN                       (-240000L)
#define PID_I_MAX                       (240000L)
#define PID_OUT_MIN                     0
#define PID_OUT_RUN_LIMIT               930
#define PID_SLEW_PER_MS_START           1
#define PID_SLEW_PER_MS_RUN             5

/* ==================================================================
 *  第 6 节：GPIO 引脚映射（编译期固定）
 * ================================================================== */

#define FAULT_LED_GPIO_Port             GPIOB
#define FAULT_LED_Pin                   GPIO_PIN_8

#define FAN_GPIO_Port                   GPIOB
#define FAN_Pin                         GPIO_PIN_9

#define RELAY_GPIO_Port                 GPIOB
#define RELAY_Pin                       GPIO_PIN_5

/* 50/60 Hz 选择：LQFP32封装无PB1，改用PB3 */
#define FREQ_SEL_GPIO_Port              GPIOB
#define FREQ_SEL_Pin                    GPIO_PIN_3

/* SPWM 模式选择：LQFP32封装无PB2，改用PB4 */
#define MODE_SEL_GPIO_Port              GPIOB
#define MODE_SEL_Pin                    GPIO_PIN_4

/* 短路信号输入：PB4已分配给MODE_SEL，改用PA4 */
#define SHORT_MCU_GPIO_Port             GPIOA
#define SHORT_MCU_Pin                   GPIO_PIN_4

/* SSD1306 OLED (I2C2: PB10=SCL, PB11=SDA) */
#define OLED_I2C_SCL_GPIO_Port          GPIOB
#define OLED_I2C_SCL_Pin                GPIO_PIN_10
#define OLED_I2C_SDA_GPIO_Port          GPIOB
#define OLED_I2C_SDA_Pin                GPIO_PIN_11

/* 按键（数字 GPIO，下拉输入） */
#define BTN_UP_GPIO_Port                GPIOA
#define BTN_UP_Pin                      GPIO_PIN_5
#define BTN_DOWN_GPIO_Port              GPIOA
#define BTN_DOWN_Pin                    GPIO_PIN_11
#define BTN_OK_GPIO_Port                GPIOA
#define BTN_OK_Pin                      GPIO_PIN_12

/* TIM1_PWM 输出引脚（AF6） */
#define PWM_AH_GPIO_Port                GPIOA    /* PA8  TIM1_CH1  A桥臂高侧 */
#define PWM_AH_Pin                      GPIO_PIN_8
#define PWM_AL_GPIO_Port                GPIOA    /* PA7  TIM1_CH1N A桥臂低侧 */
#define PWM_AL_Pin                      GPIO_PIN_7
#define PWM_BH_GPIO_Port                GPIOA    /* PA9  TIM1_CH2  B桥臂高侧 */
#define PWM_BH_Pin                      GPIO_PIN_9
#define PWM_BL_GPIO_Port                GPIOB    /* PB0  TIM1_CH2N B桥臂低侧 */
#define PWM_BL_Pin                      GPIO_PIN_0
#define PWM_BKIN_GPIO_Port              GPIOA    /* PA6  TIM1_BKIN 硬件刹车输入 */
#define PWM_BKIN_Pin                    GPIO_PIN_6

/* ==================================================================
 *  第 7 节：运行时可调参数
 *
 *  以下宏重定向到 g_params 结构体字段。
 *  上电时从 Flash 加载，若无有效数据则使用 param_store.c 中的默认值。
 *  可通过 LCD 菜单 + 按键在运行时修改并保存到 Flash。
 * ================================================================== */

#include "param_store.h"

/* 死区时间 (ns)，默认 1000 */
#define SPWM_DEADTIME_NS                (g_params.deadtime_ns)
/* 软启动时间 (ms)，默认 5000 */
#define SPWM_SOFTSTART_MS               (g_params.softstart_ms)
/* 输出电压 ADC 目标值，默认 2480 */
#define VOUT_ADC_TARGET_220V            (g_params.vout_target_adc)
/* PID Kp (Q10 格式)，默认 260 */
#define PID_KP_Q10                      ((int32_t)(g_params.pid_kp_q10))
/* PID Ki (Q10 格式)，默认 10 */
#define PID_KI_Q10                      ((int32_t)(g_params.pid_ki_q10))
/* 过载保护延时 (ms)，默认 3000 */
#define OVERLOAD_TRIP_MS                (g_params.overload_trip_ms)
/* 软启动调制上限 (千分比 0..1000)，默认 500 */
#define PID_OUT_START_LIMIT             ((int32_t)(g_params.pid_start_limit))

#endif
