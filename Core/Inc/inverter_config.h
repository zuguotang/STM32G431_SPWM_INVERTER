#ifndef INVERTER_CONFIG_H
#define INVERTER_CONFIG_H

/*
 * 系统配置文件
 * -----------
 * 本文件集中管理所有可调参数，适配实际硬件时只需修改此文件。
 *
 * 章节：
 *   1. 时钟与 PWM
 *   2. DDS 相位步进
 *   3. ADC 阈值
 *   4. 保护时间常数
 *   5. PID 参数
 *   6. 引脚映射
 */

#include "inverter_types.h"

/* ==================================================================
 *  第 1 节：时钟与 PWM
 * ================================================================== */

/*
 * 系统时钟：170 MHz
 * ---------------
 * HSI 16 MHz → PLL ×85 /4 = 340 MHz → PLLP÷2 = 170 MHz SYSCLK
 *
 * STM32G431 最高 170 MHz，需配置 FLASH_LATENCY_8 + 升压模式。
 */
#define SYSCLK_HZ                       170000000UL

/*
 * TIM1 时钟 = SYSCLK = 170 MHz（APB2 定时器时钟不分频）
 * PWM 载波频率 22 kHz：人耳听觉上限以上，比 STM8S 的 20 kHz 略高。
 *
 * PWM_PERIOD_TICKS = 170M / 22k - 1 ≈ 7726
 * 中心值 3863 对应 50% 占空比。
 *
 * 分辨率：7727 步 ≈ 12.9 位，远超 STM8S 的 800 步 (9.6 位)。
 */
#define PWM_TIMER_CLOCK_HZ              SYSCLK_HZ
#define PWM_CARRIER_HZ                  22000UL
#define PWM_PERIOD_TICKS                ((uint16_t)((PWM_TIMER_CLOCK_HZ / PWM_CARRIER_HZ) - 1UL))

/*
 * 最小 CCR 值：防止占空比过于接近 0% 或 100%，
 * MOSFET 栅极驱动需要最短导通时间来给自举电容充电。
 * 170 MHz 下 24 ticks ≈ 141 ns，对大多数栅极驱动足够。
 */
#define PWM_MIN_CCR_TICKS               24U

/* 默认调制模式：单极性（开关损耗更低） */
#define SPWM_DEFAULT_MODE               SPWM_MODE_UNIPOLAR

/* 默认输出频率：50 Hz */
#define SPWM_DEFAULT_FREQ               AC_FREQ_50HZ

/*
 * 死区时间：1000 ns = 1.0 us
 * --------------------------
 * STM32G4 TIM1 死区由 BDTR.DTG 控制，支持 4 段分辨率。
 * spwm_set_deadtime_ns() 将 ns 值转换为最佳 DTG 编码。
 *
 * 1 us 适合大多数 MOSFET 半桥 + 栅极驱动器组合。
 * IGBT 建议 1.5 ~ 3.0 us。
 */
#define SPWM_DEADTIME_NS                1000UL

/* 软启动时间：5 秒内调制幅度限制在 PID_OUT_START_LIMIT (50%)，感性负载需要更长励磁时间 */
#define SPWM_SOFTSTART_MS               5000UL

/* 继电器延时：输出电压稳定后再吸合，避免带载切换 */
#define SPWM_RELAY_DELAY_MS             800UL

/* ==================================================================
 *  第 2 节：DDS 相位步进
 * ==================================================================
 *
 * 32 位相位累加器，每个 PWM 周期 (22 kHz) 累加一次。
 *
 *   step = round(2^32 × Fout / Fpwm)
 *
 *   50 Hz: round(2^32 × 50 / 22000)  = 9,761,289
 *   60 Hz: round(2^32 × 60 / 22000)  = 11,713,547
 *
 * 相位累加器 bit31..30 → 象限 (0..3)
 *           bit29..22 → 0°~90° 正弦表索引 (0..255)
 */
#define PHASE_STEP_50HZ                 9761289UL
#define PHASE_STEP_60HZ                 11713547UL

/* ==================================================================
 *  第 3 节：ADC 阈值 (12 位，0 .. 4095)
 * ==================================================================
 *
 * 所有阈值均为 12 位右对齐 ADC 码值。
 * 必须在实板上根据分压比、电流传感器灵敏度、NTC B 值进行校准。
 */

#define ADC_MAX_CODE                    4095U

/* 220V 输出目标：实测整流滤波后的 ADC 值 */
#define VOUT_ADC_TARGET_220V            2480U

/* 输出欠压锁定：低于此值认为负载短路或逆变异常 */
#define VOUT_ADC_LOW_LOCK               480U

/* 母线欠压/过压阈值 */
#define VBUS_ADC_MIN_RUN                1040U
#define VBUS_ADC_MAX_RUN                3900U

/* 过载电流阈值：超过此值累计延时后触发保护 */
#define IOUT_ADC_OVERLOAD               3040U

/* 软件短路阈值：接近满量程，配合硬件 BKIN 双重保护 */
#define IOUT_ADC_SHORT_SW               3720U

/* 风扇/过温/恢复温度阈值（含回差防抖） */
#define TEMP_ADC_FAN_ON                 2480U
#define TEMP_ADC_OVER                   3040U
#define TEMP_ADC_RECOVER                2760U

/*
 * ADC 一阶低通滤波系数
 * -------------------
 * new = (old × (2^n - 1) + raw) / 2^n
 *
 * FAST (n=2, α=1/4)：电流、母线电压等需要快速响应的通道
 *   fc ≈ α × Fs / 2π ≈ 0.25 × 1000 / 6.28 ≈ 40 Hz
 *
 * SLOW (n=3, α=1/8)：输出电压、温度等变化缓慢的通道
 *   fc ≈ 0.125 × 1000 / 6.28 ≈ 20 Hz
 */
#define ADC_FILTER_SHIFT_FAST           2U
#define ADC_FILTER_SHIFT_SLOW           3U

/* ==================================================================
 *  第 4 节：保护时间常数 (单位：ms，除非特别说明)
 * ================================================================== */

/* 启动电流屏蔽：启动后 1000 ms 内不检测过载（感性负载励磁浪涌持续时间更长） */
#define STARTUP_CURRENT_BLANK_MS        1000UL

/* 过载延时：连续过载 3 秒后保护，给感性负载足够的电流稳定时间 */
#define OVERLOAD_TRIP_MS                3000UL

/* 过载恢复：电流回落 500 ms 后清零过载计时器 */
#define OVERLOAD_RECOVER_MS             500UL

/* 软件短路延时：2 ms 确认（快于 STM8S 的 3 ms，因为有硬件 BKIN） */
#define SHORT_SW_TRIP_MS                2UL

/* 短路重试间隔与次数：2 秒后自动重试，最多 3 次 */
#define SHORT_RETRY_DELAY_MS            2000UL
#define SHORT_RETRY_MAX                 3U

/* 母线欠压确认：持续 80 ms 后触发（滤除瞬时跌落） */
#define UNDER_BUS_TRIP_MS               80UL

/* 输出欠压确认：持续 700 ms 后触发 */
#define OUTPUT_LOW_TRIP_MS              700UL

/* ==================================================================
 *  第 5 节：PID 参数（Q10 定点格式）
 * ==================================================================
 *
 * Q10 格式：实际值 = 整数值 / 1024
 *   PID_KP_Q10 = 260  → 实际 Kp ≈ 0.254
 *   PID_KI_Q10 = 10   → 实际 Ki ≈ 0.010
 *   PID_KD_Q10 = 0    → 微分项禁用（测量微分，非误差微分）
 *
 * 使用测量微分（Derivative on Measurement）而非误差微分，
 * 避免设定值突变引起的微分冲击。
 *
 * 积分限幅：防止积分饱和 (Integral Windup)
 * 输出限幅：千分比 0..1000 映射到 PID_OUT_MIN..PID_OUT_RUN_LIMIT
 *
 * 调参方法：
 *   1. KI=KD=0，逐步增大 KP 至临界振荡
 *   2. KP 减半
 *   3. 逐步增大 KI 消除稳态误差
 *   4. 必要时加 KD 抑制超调
 *   5. 低压母线 + 阻性假负载下完成初调，额定条件下精调
 */
#define PID_KP_Q10                      260
#define PID_KI_Q10                      10
#define PID_KD_Q10                      0
#define PID_I_MIN                       (-240000L)
#define PID_I_MAX                       (240000L)
#define PID_OUT_MIN                     0
#define PID_OUT_START_LIMIT             500     /* 50%：感性负载从低电压起步，减少励磁冲击 */
#define PID_OUT_RUN_LIMIT               930     /* 93%：正常运行调制上限 */

/* PID 输出斜率限制：防止电压突变 */
#define PID_SLEW_PER_MS_START           1       /* 软启动：每 ms 最多 +1（感性负载慢爬升） */
#define PID_SLEW_PER_MS_RUN             5       /* 正常运行：每 ms 最多 +5 */

/* ==================================================================
 *  第 6 节：GPIO 引脚映射
 * ==================================================================
 *
 * 以下映射基于 STM32G431KBT6 32 脚封装。
 * 换用其他封装或重新分配引脚时，必须用 CubeMX 验证复用功能冲突。
 */

/* 故障 LED (PB8)：推挽输出，低电平有效 */
#define FAULT_LED_GPIO_Port             GPIOB
#define FAULT_LED_Pin                   GPIO_PIN_8

/* 散热风扇 (PB9)：推挽输出 */
#define FAN_GPIO_Port                   GPIOB
#define FAN_Pin                         GPIO_PIN_9

/* 继电器 (PB5)：推挽输出，高电平吸合 */
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

#endif
