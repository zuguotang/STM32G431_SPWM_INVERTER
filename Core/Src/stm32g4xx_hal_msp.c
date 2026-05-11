/*
 * HAL MSP (MCU Support Package) 实现
 * =================================
 *
 * MSP 函数由 HAL 外设初始化函数自动调用：
 *   HAL_ADC_Init()     → HAL_ADC_MspInit()
 *   HAL_TIM_Base_Init()→ HAL_TIM_Base_MspInit()
 *   HAL_UART_Init()    → HAL_UART_MspInit()
 *
 * MSP 负责：
 *   1. 使能外设时钟 (RCC)
 *   2. 配置 GPIO 复用功能
 *   3. 配置 DMA 通道
 *   4. 配置 NVIC 中断优先级和使能
 *
 * HAL 外设反初始化时调用对应的 MspDeInit 函数，
 * 本工程不需要反初始化，故省略。
 */

#include "main.h"

/* ==================================================================
 *  全局 MSP 初始化
 * ================================================================== */
void HAL_MspInit(void)
{
    /*
     * 使能系统配置控制器和电源接口时钟。
     * SYSCFG 用于中断优先级分组、外部中断配置等。
     * PWR   用于电压调节器配置（升压模式）。
     *
     * 中断优先级分组 NVIC_PRIORITYGROUP_4：
     *   4 位全部分配给抢占优先级 → 0..15 级抢占，无子优先级。
     *   高优先级中断可以抢占低优先级中断。
     */
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_4);
}

/* ==================================================================
 *  ADC MSP 初始化
 * ================================================================== */
void HAL_ADC_MspInit(ADC_HandleTypeDef *hadc)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    if (hadc->Instance != ADC1) {
        return;
    }

    /* 使能 ADC12 时钟（ADC1 和 ADC2 共享同一个时钟门控） */
    __HAL_RCC_ADC12_CLK_ENABLE();

    /* 使能 GPIOA 时钟（ADC 模拟输入引脚） */
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* 使能 DMA1 时钟（ADC DMA 传输） */
    __HAL_RCC_DMA1_CLK_ENABLE();

    /*
     * 配置 ADC 模拟输入引脚：PA0 / PA1 / PA2 / PA3
     *   模拟模式、无上下拉（高阻态）。
     *   不需要配置速度（模拟引脚不涉及数字切换）。
     */
    /* PA0~PA3 = ADC IN1~IN4 (电压/电流/温度/母线) */
    GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /*
     * 配置 DMA1_Channel1：ADC1 → 内存
     * --------------------------------
     * 方向：外设 → 内存
     * 外设地址：固定 (ADC1 DR 寄存器)
     * 内存地址：递增 (s_adc_dma[0..3])
     * 数据宽度：半字 (16 位，ADC 12 位右对齐)
     * 模式：循环模式（连续刷新 4 通道缓冲）
     * 优先级：高（ADC 数据必须及时搬移，防止溢出）
     */
    hdma_adc1.Instance = DMA1_Channel1;
    hdma_adc1.Init.Request = DMA_REQUEST_ADC1;
    hdma_adc1.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_adc1.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_adc1.Init.MemInc = DMA_MINC_ENABLE;
    hdma_adc1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_adc1.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    hdma_adc1.Init.Mode = DMA_CIRCULAR;
    hdma_adc1.Init.Priority = DMA_PRIORITY_HIGH;
    if (HAL_DMA_Init(&hdma_adc1) != HAL_OK) {
        Error_Handler();
    }

    /* 将 DMA 句柄链接到 ADC 句柄（HAL 内部使用） */
    __HAL_LINKDMA(hadc, DMA_Handle, hdma_adc1);

    /*
     * NVIC 中断优先级配置
     * ------------------
     *   DMA1_Ch1: 抢占优先级 2，子优先级 0
     *   ADC1     : 抢占优先级 2，子优先级 1（略低于 DMA）
     *
     * 子优先级仅在抢占优先级相同时生效，
     * 用于排序同时挂起的中断的响应顺序。
     */
    HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);
    HAL_NVIC_SetPriority(ADC1_2_IRQn, 2, 1);
    HAL_NVIC_EnableIRQ(ADC1_2_IRQn);
}

/* ==================================================================
 *  TIM PWM MSP 初始化
 * ================================================================== */
void HAL_TIM_PWM_MspInit(TIM_HandleTypeDef *htim)
{
    /* 仅处理 TIM1（本工程只用 TIM1 做 PWM） */
    if (htim->Instance == TIM1) {
        /* 使能 TIM1 时钟（挂在 APB2） */
        __HAL_RCC_TIM1_CLK_ENABLE();

        /*
         * TIM1 中断优先级（最高）：
         *   TIM1_UP:  0,0 → SPWM 更新 (22 kHz)，绝不容忍延迟
         *   TIM1_BRK: 0,1 → 硬件刹车，略低于更新中断但高于其他
         */
        HAL_NVIC_SetPriority(TIM1_UP_TIM16_IRQn, 0, 0);
        HAL_NVIC_EnableIRQ(TIM1_UP_TIM16_IRQn);
        HAL_NVIC_SetPriority(TIM1_BRK_TIM15_IRQn, 0, 1);
        HAL_NVIC_EnableIRQ(TIM1_BRK_TIM15_IRQn);
    }
}

/* ==================================================================
 *  TIM 基本定时器 MSP 初始化
 * ================================================================== */
void HAL_TIM_Base_MspInit(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM6) {
        /* 使能 TIM6 时钟（挂在 APB1） */
        __HAL_RCC_TIM6_CLK_ENABLE();

        /*
         * TIM6 中断优先级（中等）：
         *   抢占优先级 1，子优先级 0
         *   低于 PWM/刹车（0.x），高于 DMA/ADC（2.x）
         */
        HAL_NVIC_SetPriority(TIM6_DAC_IRQn, 1, 0);
        HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);
    } else if (htim->Instance == TIM1) {
        /* TIM1 既需要基本功能（时基）也需要 PWM，统一走 PWM MSP */
        HAL_TIM_PWM_MspInit(htim);
    }
}

/* ==================================================================
 *  TIM MSP 后初始化（GPIO 复用功能配置）
 * ================================================================== */
void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    if (htim->Instance != TIM1) {
        return;
    }

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /*
     * TIM1 PWM 输出引脚配置
     * -------------------
     * 复用推挽输出 (AF_PP)、高速 (FREQ_HIGH)、无上下拉。
     * 复用功能 AF6 = TIM1（具体映射参考 STM32G431 数据手册 Table 13）。
     *
     *   PA8 (AF6) → TIM1_CH1   全桥 A 高侧
     *   PA7 (AF6) → TIM1_CH1N  全桥 A 低侧
     *   PA9 (AF6) → TIM1_CH2   全桥 B 高侧
     *   PB0 (AF6) → TIM1_CH2N  全桥 B 低侧
     *   PA6 (AF6) → TIM1_BKIN  硬件刹车输入
     */
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF6_TIM1;

    /* PA8 + PA9 = CH1 + CH2 (高侧) */
    GPIO_InitStruct.Pin = PWM_AH_Pin | PWM_BH_Pin;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* PA7 = CH1N (A 低侧) */
    GPIO_InitStruct.Pin = PWM_AL_Pin;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* PB0 = CH2N (B 低侧) */
    GPIO_InitStruct.Pin = PWM_BL_Pin;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /*
     * BKIN 引脚配置：PA6
     * 注意使用下拉电阻：BKIN 高电平触发刹车，
     * 下拉确保上电/复位期间不误触发。
     */
    GPIO_InitStruct.Pin = PWM_BKIN_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    GPIO_InitStruct.Alternate = GPIO_AF6_TIM1;
    HAL_GPIO_Init(PWM_BKIN_GPIO_Port, &GPIO_InitStruct);
}

/* ==================================================================
 *  I2C2 MSP 初始化 (SSD1306 OLED)
 * ================================================================== */
void HAL_I2C_MspInit(I2C_HandleTypeDef *hi2c)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    if (hi2c->Instance != I2C2) return;

    __HAL_RCC_I2C2_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /*
     * PB10 = SCL, PB11 = SDA
     * 复用开漏输出 (AF_OD)、外部上拉 4.7kΩ 到 3.3V
     */
    GPIO_InitStruct.Pin = OLED_I2C_SCL_Pin | OLED_I2C_SDA_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF4_I2C2;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

/* ==================================================================
 *  UART MSP 初始化
 * ================================================================== */
void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    if (huart->Instance != USART1) {
        return;
    }

    /* 使能 USART1 和 GPIOB 时钟 */
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /*
     * USART1 引脚配置：
     *   PB6 (AF7) → USART1_TX
     *   PB7 (AF7) → USART1_RX
     *
     * 复用推挽输出、上拉（确保空闲状态为高电平）。
     */
    GPIO_InitStruct.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}
