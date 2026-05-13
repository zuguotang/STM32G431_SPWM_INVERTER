/*
 * 主程序
 * =====
 * 系统启动流程和中断回调。
 *
 * 启动流程：
 *   1. HAL_Init()                → HAL 库和 SysTick 初始化
 *   2. SystemClock_Config()      → HSI+PLL → 170 MHz
 *   3. MX_GPIO_Init()            → 输出/输入引脚配置
 *   4. MX_DMA_Init()             → DMA + DMAMUX 时钟使能
 *   5. MX_ADC1_Init()            → ADC1 4通道扫描 + DMA
 *   6. MX_TIM1_Init()            → TIM1 22 kHz 互补 PWM + BKIN
 *   7. MX_TIM6_Init()            → TIM6 1 kHz 时基
 *   8. MX_USART1_UART_Init()     → USART1 115200 调试串口
 *   9. app_init()                → 应用层初始化（SPWM/保护/PID/ADC启动）
 *
 * 主循环：
 *   等待 s_tick_1ms 标志（由 TIM6 中断置位），
 *   每 ms 调用 app_task_1ms() 执行全部应用层逻辑。
 *
 * 与 STM8S 版本的关键区别：
 *   - 使用 CubeMX 风格的 MX_xxx_Init() 函数初始化外设
 *   - TIM6 直接产生 1 kHz 中断（而非 16 kHz 分频）
 *   - ADC 使用 DMA 连续扫描，无需 CPU 轮询
 *   - 故障 LED 和串口调试在本版本中新增
 */

#include "main.h"
#include "app.h"
#include "board.h"
#include "spwm.h"

/* HAL 外设句柄定义（头文件中为 extern 声明） */
ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;
TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim6;
UART_HandleTypeDef huart2;
I2C_HandleTypeDef hi2c1;

/* 1 ms 标志：TIM6 ISR 置位，主循环清零 */
static volatile bool s_tick_1ms = false;

/* 前向声明：CubeMX 风格的初始化函数 */
static void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_ADC1_Init(void);
static void MX_TIM1_Init(void);
static void MX_TIM6_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_I2C1_Init(void);

/* ==================================================================
 *  main() 主函数
 * ================================================================== */
int main(void)
{
    /*
     * 第 1 步：HAL 库初始化
     *   - SysTick 配置为 1 ms 中断
     *   - 中断优先级分组设置为 4（全部抢占优先级）
     *   - 使能指令和数据缓存（STM32G4 ART Accelerator）
     */
    HAL_Init();

    /*
     * 第 2 步：系统时钟配置
     *   16 MHz HSI → PLL×85/4 = 340 MHz → PLLP÷2 = 170 MHz SYSCLK
     */
    SystemClock_Config();

    /*
     * 第 3 步：外设初始化
     *   顺序：GPIO → DMA → ADC → TIM1 → TIM6 → USART1
     *   DMA 必须在 ADC 之前初始化（ADC 依赖 DMA 句柄）
     *   TIM1 在 app_init 中才启动 PWM 输出
     */
    MX_GPIO_Init();
    MX_DMA_Init();
    MX_ADC1_Init();
    MX_TIM1_Init();
    MX_TIM6_Init();
    MX_USART2_UART_Init();
    MX_I2C1_Init();

    /*
     * 第 4 步：应用层初始化
     *   启动 ADC DMA → 配置 SPWM → 开启 PWM 和定时器中断 →
     *   延时 50 ms → 读取频率/模式 IO → 条件满足则开启 SPWM
     */
    app_init();

    /*
     * 第 5 步：主循环
     *   以 1 ms 为周期执行 app_task_1ms()。
     *   s_tick_1ms 由 TIM6 中断回调置位，主循环消费后清零。
     *
     *   这种设计比在 ISR 中直接执行任务更安全：
     *     - ISR 快速进出，不阻塞其他中断
     *     - 1 ms 任务执行时间不影响定时器精度
     */
    while (1) {
        if (s_tick_1ms) {
            s_tick_1ms = false;
            app_task_1ms();
        }
    }
}

/* ==================================================================
 *  系统时钟配置：HSI + PLL → 170 MHz
 * ================================================================== */
static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    /*
     * 使能升压模式（PWR_REGULATOR_VOLTAGE_SCALE1_BOOST）
     * STM32G431 运行在 170 MHz 必须使用此模式。
     * 150 MHz 以内可用 SCALE1（无需 BOOST）。
     */
    HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

    /*
     * HSI 振荡器配置：
     *   16 MHz HSI → PLLM÷4=4 MHz → PLLN×85=340 MHz →
     *   PLLP÷2=170 MHz SYSCLK
     *   PLLQ÷2=170 MHz (用于 ADC/其他)
     *   PLLR÷2=170 MHz (备用)
     */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV4;       /* 16M / 4 = 4M */
    RCC_OscInitStruct.PLL.PLLN = 85;                  /* 4M × 85 = 340M */
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;       /* 340M / 2 = 170M */
    RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
    RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        Error_Handler();
    }

    /*
     * 时钟分配：
     *   SYSCLK = PLLCLK = 170 MHz
     *   HCLK   = SYSCLK / 1 = 170 MHz
     *   PCLK1  = HCLK / 1 = 170 MHz (APB1 定时器 ×2 = 340 MHz)
     *   PCLK2  = HCLK / 1 = 170 MHz (APB2 定时器 ×2 = 340 MHz)
     *
     * TIM1 挂在 APB2，因此 TIM1 时钟 = 170 MHz × 2 = 340 MHz？
     * 不对：当 PCLKx / HCLK = 1 时，定时器时钟 = PCLKx = 170 MHz。
     * 当 PCLKx / HCLK ≠ 1 时，定时器时钟 = PCLKx × 2。
     */
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                  RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_8) != HAL_OK) {
        Error_Handler();
    }
}

/* ==================================================================
 *  ADC1 初始化：4 通道扫描 + DMA 连续转换
 * ================================================================== */
static void MX_ADC1_Init(void)
{
    ADC_ChannelConfTypeDef sConfig = {0};
    ADC_MultiModeTypeDef multimode = {0};

    hadc1.Instance = ADC1;

    /*
     * ADC 时钟 = SYSCLK / 4 = 42.5 MHz（异步时钟）
     * 12 位分辨率：每个采样 47.5 + 12.5 = 60 ADC 周期 ≈ 1.4 us
     *
     * 扫描模式：4 个通道按 Rank 顺序依次转换
     * 连续转换 + DMA：转换结果自动经 DMA 循环写入 s_adc_dma[4]
     * EOC 选择 SEQ_CONV：序列结束才置 EOC（减少中断）
     */
    hadc1.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV4;
    hadc1.Init.Resolution = ADC_RESOLUTION_12B;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.GainCompensation = 0;
    hadc1.Init.ScanConvMode = ADC_SCAN_ENABLE;
    hadc1.Init.EOCSelection = ADC_EOC_SEQ_CONV;
    hadc1.Init.LowPowerAutoWait = DISABLE;
    hadc1.Init.ContinuousConvMode = ENABLE;
    hadc1.Init.NbrOfConversion = 4;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc1.Init.DMAContinuousRequests = ENABLE;
    hadc1.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;    /* DMA 模式下覆盖旧数据 */
    hadc1.Init.OversamplingMode = DISABLE;
    if (HAL_ADC_Init(&hadc1) != HAL_OK) {
        Error_Handler();
    }

    /* 独立模式（非双 ADC 交错） */
    multimode.Mode = ADC_MODE_INDEPENDENT;
    if (HAL_ADCEx_MultiModeConfigChannel(&hadc1, &multimode) != HAL_OK) {
        Error_Handler();
    }

    /*
     * 配置通道采样参数（四个通道共用相同的采样时间）
     * 采样时间 47.5 ADC 周期 @ 42.5 MHz ≈ 1.12 us
     * 总转换时间 = 1.12 + 0.29(12.5周期) ≈ 1.41 us/ch
     */
    sConfig.SamplingTime = ADC_SAMPLETIME_47CYCLES_5;
    sConfig.SingleDiff = ADC_SINGLE_ENDED;
    sConfig.OffsetNumber = ADC_OFFSET_NONE;
    sConfig.Offset = 0;

    /*
     * 通道 → DMA 缓冲索引映射：
     *   Rank 1 → CH1 (PA0) → s_adc_dma[0]  → 输出电压
     *   Rank 2 → CH2 (PA1) → s_adc_dma[1]  → 输出电流
     *   Rank 3 → CH4 (PA4) → s_adc_dma[2]  → NTC 温度
     *   Rank 4 → CH5 (PA5) → s_adc_dma[3]  → 母线电压
     */
    sConfig.Channel = ADC_CHANNEL_1;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) Error_Handler();

    sConfig.Channel = ADC_CHANNEL_2;
    sConfig.Rank = ADC_REGULAR_RANK_2;
    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) Error_Handler();

    sConfig.Channel = ADC_CHANNEL_4;
    sConfig.Rank = ADC_REGULAR_RANK_3;
    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) Error_Handler();

    sConfig.Channel = ADC_CHANNEL_5;
    sConfig.Rank = ADC_REGULAR_RANK_4;
    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) Error_Handler();
}

/* ==================================================================
 *  TIM1 初始化：22 kHz 互补 PWM + BKIN 刹车
 * ================================================================== */
static void MX_TIM1_Init(void)
{
    TIM_ClockConfigTypeDef sClockSourceConfig = {0};
    TIM_MasterConfigTypeDef sMasterConfig = {0};
    TIM_OC_InitTypeDef sConfigOC = {0};
    TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

    htim1.Instance = TIM1;

    /*
     * 时基配置：
     *   PSC = 0 → TIM1_CLK = 170 MHz
     *   ARR = 7726 → PWM 频率 = 170M / (7726+1) ≈ 22,000 Hz
     *   向上计数模式，中心值 = 7726/2 = 3863
     */
    htim1.Init.Prescaler = 0;
    htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim1.Init.Period = PWM_PERIOD_TICKS;
    htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim1.Init.RepetitionCounter = 0;
    htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if (HAL_TIM_Base_Init(&htim1) != HAL_OK) Error_Handler();

    sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
    if (HAL_TIM_ConfigClockSource(&htim1, &sClockSourceConfig) != HAL_OK) Error_Handler();
    if (HAL_TIM_PWM_Init(&htim1) != HAL_OK) Error_Handler();

    /* 主从模式：禁用（独立运行） */
    sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
    sMasterConfig.MasterOutputTrigger2 = TIM_TRGO2_RESET;
    sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK) Error_Handler();

    /*
     * PWM 输出通道配置
     * ----------------
     * PWM 模式 1：CNT < CCR → 高电平，CNT > CCR → 低电平
     * CH1/CH2 配置相同，互补输出自动由硬件生成。
     *
     * 初始占空比 = 50%（PWM_PERIOD_TICKS / 2），上电无输出。
     * app_init 中运行时才会调用 spwm_init_runtime() 启动。
     */
    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = PWM_PERIOD_TICKS / 2U;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;     /* 空闲低电平（安全） */
    sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
    if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1) != HAL_OK) Error_Handler();
    if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_2) != HAL_OK) Error_Handler();

    /*
     * 刹车/死区配置
     * -----------
     * BKIN 使能：高电平触发（外部比较器输出高 = 过流）
     * 死区初始设为 0，运行时通过 spwm_set_deadtime_ns() 设置。
     *
     * 重要：OffStateRunMode/DLEMode = DISABLE，
     * 刹车时 MOE 硬件清零，输出强制为 IDLE 状态（低电平）。
     */
    sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
    sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
    sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
    sBreakDeadTimeConfig.DeadTime = 0;
    sBreakDeadTimeConfig.BreakState = TIM_BREAK_ENABLE;
    sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
    sBreakDeadTimeConfig.BreakFilter = 0;              /* 无滤波（最快响应） */
    sBreakDeadTimeConfig.BreakAFMode = TIM_BREAK_AFMODE_INPUT;
    sBreakDeadTimeConfig.Break2State = TIM_BREAK2_DISABLE;
    sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
    if (HAL_TIMEx_ConfigBreakDeadTime(&htim1, &sBreakDeadTimeConfig) != HAL_OK) Error_Handler();

    /* GPIO 复用功能初始化（在 HAL_TIM_MspPostInit 中完成） */
    HAL_TIM_MspPostInit(&htim1);
}

/* ==================================================================
 *  TIM6 初始化：1 ms 系统时基
 * ================================================================== */
static void MX_TIM6_Init(void)
{
    /*
     * TIM6 是一个简单的 16 位基本定时器。
     *
     * 时钟链：
     *   170 MHz / 170 = 1 MHz  (PSC = 169)
     *   1 MHz / 1000 = 1 kHz   (ARR = 999)
     *
     * 每次溢出产生中断，回调中递增 g_ms 并置 s_tick_1ms = true。
     */
    htim6.Instance = TIM6;
    htim6.Init.Prescaler = (uint16_t)((SYSCLK_HZ / 1000000UL) - 1UL);  /* 169 */
    htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim6.Init.Period = 999;
    htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_Base_Init(&htim6) != HAL_OK) {
        Error_Handler();
    }
}

/* ==================================================================
 *  USART1 初始化：调试串口
 * ================================================================== */
static void MX_USART2_UART_Init(void)
{
    /*
     * PA2=TX, PA3=RX, 115200-8-N-1
     * 无硬件流控，16 倍过采样。
     */
    huart2.Instance = USART2;
    huart2.Init.BaudRate = 115200;
    huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits = UART_STOPBITS_1;
    huart2.Init.Parity = UART_PARITY_NONE;
    huart2.Init.Mode = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    huart2.Init.ClockPrescaler = UART_PRESCALER_DIV1;
    huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    if (HAL_UART_Init(&huart2) != HAL_OK) {
        Error_Handler();
    }
}

/* ==================================================================
 *  I2C1 初始化：SSD1306 OLED
 * ================================================================== */
static void MX_I2C1_Init(void)
{
    /*
     * I2C1: PB6=SCL, PB7=SDA, 400kHz Fast Mode
     * SSD1306 支持 100kHz 和 400kHz，这里选 400kHz 以便快速刷新。
     * 8 页 × 128 字节 × 9 bits / 400kHz ≈ 23ms 全屏刷新。
     */
    hi2c1.Instance = I2C1;
    hi2c1.Init.Timing = 0x00303D5B;  /* 170MHz 下 400kHz 的推荐 Timing 值 */
    hi2c1.Init.OwnAddress1 = 0;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2 = 0;
    hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&hi2c1) != HAL_OK) {
        Error_Handler();
    }
}

/* ==================================================================
 *  DMA 初始化：使能时钟
 * ================================================================== */
static void MX_DMA_Init(void)
{
    /*
     * DMA 具体配置在 HAL_ADC_MspInit() 中完成（MSP = MCU Support Package）。
     * 这里只使能 DMA 和 DMAMUX 时钟，确保后续 HAL_DMA_Init 能正常工作。
     *
     * DMAMUX 是 STM32G4 的 DMA 请求路由器：
     *   DMA1_Channel1 通过 DMAMUX 连接到 ADC1 的 DMA 请求。
     */
    __HAL_RCC_DMAMUX1_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();
}

/* ==================================================================
 *  GPIO 初始化
 * ================================================================== */
static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* enable GPIO clocks */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();

    /* RELAY(PB5) */
    GPIO_InitStruct.Pin = RELAY_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* FAN(PA10) -- PB9 not on LQFP32 */
    GPIO_InitStruct.Pin = FAN_Pin;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* FAULT_LED(PF1) -- PB8 is BOOT0, moved to PF1 */
    GPIO_InitStruct.Pin = FAULT_LED_Pin;
    HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

    /* FREQ_SEL(PB3) + MODE_SEL(PB4) -- pull-down input */
    GPIO_InitStruct.Pin = FREQ_SEL_Pin | MODE_SEL_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* SHORT_MCU(PA15) -- PB10 not on LQFP32 */
    GPIO_InitStruct.Pin = SHORT_MCU_Pin;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* BTN_DOWN(PA11) + BTN_OK(PA12) */
    GPIO_InitStruct.Pin = BTN_DOWN_Pin | BTN_OK_Pin;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* BTN_UP(PF0) -- PB11 not on LQFP32 */
    GPIO_InitStruct.Pin = BTN_UP_Pin;
    HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);
}


/* ==================================================================
 *  HAL 中断回调
 * ================================================================== */

/*
 * 定时器周期中断回调
 * ---------------
 * TIM1 更新: 22 kHz SPWM 更新 → spwm_tim_update_isr()
 * TIM6 更新: 1 kHz 时基   → g_ms++ + 置 s_tick_1ms
 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM1) {
        spwm_tim_update_isr();
    } else if (htim->Instance == TIM6) {
        g_ms++;
        s_tick_1ms = true;
    }
}

/*
 * TIM1 刹车中断回调
 * -------------
 * BKIN 引脚检测到高电平（外部比较器过流信号）→ 硬件自动关断 MOE
 * ISR 中调用 spwm_break_isr() 设置刹车挂起标志，
 * 主循环的 1 ms 任务中检测并处理恢复逻辑。
 */
void HAL_TIMEx_BreakCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM1) {
        spwm_break_isr();
    }
}

/*
 * 错误处理
 * -------
 * 任何 HAL 初始化失败时进入此函数。
 * 关全局中断 → 死循环 → 调试器可在此设断点诊断。
 */
void Error_Handler(void)
{
    __disable_irq();
    while (1) {
    }
}
