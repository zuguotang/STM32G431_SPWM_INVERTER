/*
 * 中断服务程序
 * ===========
 * STM32G431 中断向量表（CMSIS 标准命名）。
 *
 * ISR 设计原则：
 *   - 默认的 Fault Handler (NMI/HardFault/MemManage/BusFault/UsageFault)
 *     直接死循环，调试时可在此设断点。
 *   - SysTick 用于 HAL_Delay (1 ms 时基)。
 *   - TIM1/TIM6/DMA/ADC 中断路由到 HAL 库的 IRQ Handler，
 *     由 HAL 回调函数 (Callback) 处理实际逻辑。
 *
 * 中断优先级分配（NVIC_PRIORITYGROUP_4 = 全部抢占）：
 *   0,0  TIM1_UP      → SPWM 更新 (最高优先级，22 kHz)
 *   0,1  TIM1_BRK     → 硬件刹车
 *   1,0  TIM6_DAC     → 系统时基 1 kHz
 *   2,0  DMA1_Ch1     → ADC DMA 传输完成
 *   2,1  ADC1         → ADC 序列转换完成
 */

#include "main.h"
#include "spwm.h"

/* ==================================================================
 *  Fault Handler：发生硬件异常时在此死循环
 *
 * 调试技巧：
 *   - 在对应的 Handler 中设断点
 *   - 查看调用栈 (Call Stack) 中的返回地址
 *   - 查看 SCB->CFSR (Configurable Fault Status Register) 判断原因
 * ================================================================== */

void NMI_Handler(void)
{
    while (1) { }  /* 不可屏蔽中断：通常为时钟安全或振荡器失效 */
}

void HardFault_Handler(void)
{
    while (1) { }  /* 硬件错误：非法内存访问、未定义指令等 */
}

void MemManage_Handler(void)
{
    while (1) { }  /* MPU 内存管理错误 */
}

void BusFault_Handler(void)
{
    while (1) { }  /* 总线错误：预取失败、数据访问越界 */
}

void UsageFault_Handler(void)
{
    while (1) { }  /* 用法错误：未定义指令、不对齐访问等 */
}

/* ==================================================================
 *  SysTick：HAL 库 1 ms 时基
 * ================================================================== */
void SysTick_Handler(void)
{
    /*
     * HAL_IncTick() 递增全局变量 uwTick，
     * 供 HAL_Delay() 和 HAL_GetTick() 使用。
     * SysTick 配置在 HAL_Init() 中完成。
     */
    HAL_IncTick();
}

/* ==================================================================
 *  DMA/ADC 中断 → HAL 库处理
 * ================================================================== */
void DMA1_Channel1_IRQHandler(void)
{
    /*
     * ADC1 DMA 传输完成/半完成/错误中断。
     * HAL_DMA_IRQHandler 根据事件调用对应的 HAL_ADC 回调。
     * 本工程使用 DMA 循环模式，不需要在回调中做任何操作。
     */
    HAL_DMA_IRQHandler(&hdma_adc1);
}

void ADC1_2_IRQHandler(void)
{
    /* ADC1 转换完成/溢出中断 → HAL_ADC_IRQHandler */
    HAL_ADC_IRQHandler(&hadc1);
}

/* ==================================================================
 *  TIM1 中断 → HAL 库处理 → 回调 spwm_tim_update_isr / break
 * ================================================================== */
void TIM1_UP_TIM16_IRQHandler(void)
{
    /*
     * TIM1 更新中断 (22 kHz)。
     * HAL_TIM_IRQHandler → HAL_TIM_PeriodElapsedCallback → spwm_tim_update_isr()
     */
    HAL_TIM_IRQHandler(&htim1);
}

void TIM1_BRK_TIM15_IRQHandler(void)
{
    /*
     * TIM1 刹车中断。
     * HAL_TIM_IRQHandler → HAL_TIMEx_BreakCallback → spwm_break_isr()
     */
    HAL_TIM_IRQHandler(&htim1);
}

/* ==================================================================
 *  TIM6 中断 → 系统时基
 * ================================================================== */
void TIM6_DAC_IRQHandler(void)
{
    /*
     * TIM6 更新中断 (1 kHz)。
     * HAL_TIM_IRQHandler → HAL_TIM_PeriodElapsedCallback → g_ms++ + tick flag
     */
    HAL_TIM_IRQHandler(&htim6);
}
