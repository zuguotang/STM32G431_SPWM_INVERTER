#ifndef BOARD_H
#define BOARD_H

/*
 * 板级支持层 (BSP)
 * ---------------
 * 封装所有 GPIO 输出的控制：
 *   - 故障 LED：闪烁故障码指示
 *   - 风扇：温度控制启停
 *   - 继电器：交流输出物理隔离
 *
 * 以及输入信号的读取：
 *   - 50/60 Hz 频率选择
 *   - 单/双极性模式选择
 *   - 外部短路信号检测
 */

#include "main.h"

/*
 * 系统毫秒计数器
 * --------------
 * 由 TIM6 周期中断 (1 kHz) 递增。
 * 中断回调 HAL_TIM_PeriodElapsedCallback 中更新，
 * 主循环通过 s_tick_1ms 标志同步调度。
 */
extern volatile uint32_t g_ms;

/* 初始化所有输出为关闭状态（LED 灭、风扇停、继电器断开） */
void board_init_outputs(void);

/* 故障 LED 控制 */
void board_fault_led_set(bool on);

/*
 * 故障 LED 闪烁任务（每 1 ms 调用）
 * -------------------------------
 * 根据故障码闪烁不同次数：
 *   SHORT    → 1 次
 *   OVERLOAD → 2 次
 *   OVER_TEMP→ 3 次
 *   UNDER_BUS→ 4 次
 *   OUTPUT_LOW→5 次
 *   OVER_BUS → 6 次
 *
 * 周期：200 ms 亮灭交替 + 约 600 ms 间隔 = 约 1 秒一个循环。
 */
void board_fault_led_task_1ms(fault_code_t fault);

/* 风扇控制 */
void board_fan_set(bool on);

/* 继电器控制（true = 吸合，false = 断开） */
void board_relay_set(bool on);

/* GPIO 输入读取 */
bool board_frequency_is_60hz(void);      /* 频率选择：true=60Hz, false=50Hz */
spwm_mode_t board_read_spwm_mode(void);  /* 模式选择：BIPOLAR 或 UNIPOLAR */
bool board_short_input_active(void);      /* 外部短路信号：true=短路有效 */

/* 故障码 → 闪烁次数映射 */
uint8_t board_fault_blink_count(fault_code_t fault);

#endif
