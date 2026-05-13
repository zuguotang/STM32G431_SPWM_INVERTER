#ifndef DEBUG_UART_H
#define DEBUG_UART_H

/*
 * 调试串口模块
 * ----------
 * 通过 USART2 (PA2 TX, PA3 RX) 输出运行状态和故障信息。
 *
 * 波特率 115200-8-N-1，无硬件流控。
 * 使用 HAL_UART_Transmit 阻塞发送，超时 20~30 ms。
 * 发送函数在 1 ms 任务上下文中调用，注意不要阻塞过久。
 *
 * 输出格式：
 *   故障: "FAULT <code> <name> retry=<n> vout=... iout=... temp=... vbus=..."
 *   运行: "RUN vout=... iout=... temp=... vbus=... amp=..."
 */

#include "main.h"

/* 发送字符串（阻塞） */
void debug_uart_print(const char *text);

/* 发送故障信息（含 ADC 快照，便于诊断） */
void debug_uart_print_fault(fault_code_t fault, uint8_t short_retry_used);

/* 每秒发送一次运行状态（vout/iout/temp/vbus/amp） */
void debug_uart_print_status(void);

#endif
