#ifndef APP_H
#define APP_H

/*
 * 应用层
 * -----
 * 组合所有模块，实现完整的逆变器应用逻辑。
 *
 * app_init()：系统启动流程
 *   1. 初始化输出、PID、SPWM、保护
 *   2. 启动 ADC DMA
 *   3. 开启 TIM1/TIM6 PWM 和中断
 *   4. 延时 50 ms → 读取频率/模式 IO → 尝试启动
 *
 * app_task_1ms()：1 ms 主调度
 *   1. ADC 采样滤波
 *   2. 更新频率/模式设置
 *   3. 保护检测
 *   4. PID 控制（仅无故障时）
 *   5. 每秒一次串口状态输出
 */

#include "main.h"

void app_init(void);
void app_task_1ms(void);

#endif
