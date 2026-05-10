#ifndef SPWM_H
#define SPWM_H

/*
 * SPWM 调制模块
 * -----------
 * 使用 TIM1 生成 22 kHz 互补 SPWM 驱动全桥逆变器。
 *
 * 核心算法：DDS 相位累加 + 257 点正弦查表 + 象限反射
 *   与 STM8S 版本算法完全一致，区别在于：
 *   1. PWM 分辨率：7727 步 vs 800 步（12.9 位 vs 9.6 位）
 *   2. 支持单极性/双极性模式可选
 *   3. 死区由 ns 值自动计算 DTG 编码
 *   4. HAL 库驱动，而非直接寄存器操作
 *
 * 输出引脚（TIM1 复用功能）：
 *   CH1  → PA8  (TIM1_CH1)   A 桥臂高侧
 *   CH1N → PA7  (TIM1_CH1N)  A 桥臂低侧
 *   CH2  → PA9  (TIM1_CH2)   B 桥臂高侧
 *   CH2N → PB0  (TIM1_CH2N)  B 桥臂低侧
 *   BKIN → PA6  (TIM1_BKIN)  硬件刹车
 */

#include "main.h"

/* 调制幅度：0..1000（千分比），由 PID 控制器更新 */
extern volatile uint16_t g_spwm_amp;

/* PWM 输出使能标志：true = 正在输出，false = 关闭 */
extern volatile bool g_spwm_enabled;

/* 运行时初始化：复位所有状态变量、设置死区 */
void spwm_init_runtime(void);

/* PWM 输出开关 */
void spwm_outputs_on(void);
void spwm_outputs_off(void);

/* 设置输出频率（50 或 60 Hz），更新 DDS 相位步进 */
void spwm_set_frequency(ac_freq_t freq);

/* 设置调制模式（单极性/双极性） */
void spwm_set_mode(spwm_mode_t mode);

/* 设置死区时间（纳秒），自动转换为 TIM1 BDTR.DTG 编码 */
void spwm_set_deadtime_ns(uint32_t ns);

/* TIM1 更新中断：DDS 累加 → 查正弦表 → 更新 CCR1/CCR2 */
void spwm_tim_update_isr(void);

/* TIM1 刹车中断：硬件 BKIN 触发时调用 */
void spwm_break_isr(void);

/* 刹车状态查询/清除（供主循环的 1 ms 任务轮询） */
bool spwm_break_pending(void);
void spwm_clear_break_pending(void);

#endif
