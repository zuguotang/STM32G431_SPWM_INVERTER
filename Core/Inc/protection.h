#ifndef PROTECTION_H
#define PROTECTION_H

/*
 * 保护模块
 * -------
 * 完整的故障检测、响应和恢复状态机。
 *
 * 检测优先级（从高到低）：
 *   1. 硬件刹车 (BKIN)     → 微秒级，直接硬件关断 PWM
 *   2. 外部短路信号 (PB4)   → 比较器输出，送 MCU 软件确认
 *   3. 母线过压             → 立即锁存
 *   4. 过温                 → 立即锁存
 *   5. 软件短路 (ADC 阈值)  → 2 ms 确认后按硬件短路处理
 *   6. 过载 (ADC 阈值)      → 1.5 秒延时确认
 *   7. 母线欠压             → 80 ms 确认
 *   8. 输出欠压             → 700 ms 确认
 *
 * 短路特殊处理：
 *   - 累计 3 次后永久锁定 (s_short_lockout)
 *   - 每次重试等待 2 秒
 *   - 重试条件：母线正常、温度正常、外部短路信号已解除
 *
 * 其他故障：等待条件恢复后自动清除。
 */

#include "main.h"

/* 当前故障状态，主循环和调试串口读取 */
extern volatile fault_code_t g_fault;

/* 初始化保护状态（上电时调用） */
void protection_init(void);

/* 复位保护计数器（启动/恢复时调用） */
void protection_reset_for_start(void);

/* 保护检测 1 ms 任务：fan + break + short_retry + 所有故障扫描 */
void protection_task_1ms(void);

/* 锁存故障：记录故障码 → 关 PWM → 串口打印 */
void protection_latch_fault(fault_code_t fault);

/* 是否可以运行 PID 控制（无故障且未永久锁定短路） */
bool protection_can_run(void);

/* 硬件短路事件处理（BKIN ISR 或外部短路 GPIO 触发） */
void protection_short_hw_event(void);

#endif
