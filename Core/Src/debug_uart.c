/*
 * 调试串口实现
 * ===========
 * 通过 USART2 输出故障信息和运行状态。
 *
 * 发送函数使用 HAL_UART_Transmit（阻塞模式），
 * 在 115200 波特率下发送一条状态行 (~50 字节) 约 4.3 ms。
 * 从 1 ms 任务中调用时需注意累积延迟。
 * 因此仅在故障时（低频）和每秒一次状态输出时调用。
 */

#include "debug_uart.h"
#include "adc_driver.h"
#include "spwm.h"
#include <stdio.h>
#include <string.h>

/* ==================================================================
 *  故障名称映射
 * ================================================================== */
static const char *fault_name(fault_code_t fault)
{
    switch (fault) {
    case FAULT_SHORT:      return "SHORT";
    case FAULT_OVERLOAD:   return "OVERLOAD";
    case FAULT_OVER_TEMP:  return "OVER_TEMP";
    case FAULT_UNDER_BUS:  return "UNDER_BUS";
    case FAULT_OUTPUT_LOW: return "OUTPUT_LOW";
    case FAULT_OVER_BUS:   return "OVER_BUS";
    default:               return "NONE";
    }
}

/* ==================================================================
 *  字符串发送
 * ================================================================== */
void debug_uart_print(const char *text)
{
    /*
     * HAL_UART_Transmit 为阻塞模式。
     * 超时 20 ms（足够发送约 230 字节 @115200）。
     * 如果串口未连接，超时后继续运行。
     */
    HAL_UART_Transmit(&huart2, (uint8_t *)text, (uint16_t)strlen(text), 20U);
}

/* ==================================================================
 *  故障信息输出
 * ================================================================== */
void debug_uart_print_fault(fault_code_t fault, uint8_t short_retry_used)
{
    /*
     * 输出格式：
     *   FAULT <code> <name> retry=<n> vout=<val> iout=<val> temp=<val> vbus=<val>
     *
     * 包含触发时刻的 ADC 快照，方便事后诊断。
     * 例如：FAULT 1 SHORT retry=0 vout=2500 iout=3800 temp=1600 vbus=3200
     */
    char line[96];
    int n = snprintf(line, sizeof(line),
                     "FAULT %u %s retry=%u vout=%u iout=%u temp=%u vbus=%u\r\n",
                     (unsigned)fault, fault_name(fault), (unsigned)short_retry_used,
                     (unsigned)g_adc.vout, (unsigned)g_adc.iout,
                     (unsigned)g_adc.temp, (unsigned)g_adc.vbus);
    if (n > 0) {
        HAL_UART_Transmit(&huart2, (uint8_t *)line, (uint16_t)n, 30U);
    }
}

/* ==================================================================
 *  运行状态输出
 * ================================================================== */
void debug_uart_print_status(void)
{
    /*
     * 输出格式：
     *   RUN vout=<val> iout=<val> temp=<val> vbus=<val> amp=<val>
     *
     * amp 为当前调制幅度 (0..1000)，反映 PI 控制输出。
     * 正常运行时每秒输出一次。
     */
    char line[80];
    int n = snprintf(line, sizeof(line),
                     "RUN vout=%u iout=%u temp=%u vbus=%u amp=%u\r\n",
                     (unsigned)g_adc.vout, (unsigned)g_adc.iout,
                     (unsigned)g_adc.temp, (unsigned)g_adc.vbus,
                     (unsigned)g_spwm_amp);
    if (n > 0) {
        HAL_UART_Transmit(&huart2, (uint8_t *)line, (uint16_t)n, 20U);
    }
}
