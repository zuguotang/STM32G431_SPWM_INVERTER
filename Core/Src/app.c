/*
 * 应用层实现
 * =========
 * app_init() 和 app_task_1ms() 将各模块组合为完整的逆变器应用。
 *
 * 全局状态（本文件管理）：
 *   - s_voltage_pid : 电压环 PID 状态
 *   - s_start_ms    : 启动计时，0 → SPWM_SOFTSTART_MS
 *   - s_relay_ms    : 继电器延时计时，0 → SPWM_RELAY_DELAY_MS
 *   - s_status_ms   : 串口状态输出间隔计时
 */

#include "app.h"
#include "adc_driver.h"
#include "board.h"
#include "debug_uart.h"
#include "pid.h"
#include "protection.h"
#include "spwm.h"

static pid_q10_t s_voltage_pid;    /* 电压环 PID 控制器 */
static uint32_t s_start_ms;        /* 启动计时器（从 0 到 SPWM_SOFTSTART_MS） */
static uint32_t s_relay_ms;        /* 继电器延时计时器 */
static uint16_t s_status_ms;       /* 串口状态输出间隔计时器 */

/* ==================================================================
 *  首次启动尝试
 * ================================================================== */
static void inverter_start_if_ready(void)
{
    /* 获取当前 ADC 采样值 */
    adc_sample_filtered_1ms();

    /*
     * 启动条件：
     *   1. 母线电压在正常范围内
     *   2. 温度低于过温阈值
     *   3. 外部短路信号未激活
     *
     * 条件满足 → 复位保护/PID 状态 → 从零幅度开始 → 开启 PWM
     * 条件不满足 → 锁存母线欠压故障
     */
    if ((g_adc.vbus > VBUS_ADC_MIN_RUN) && (g_adc.vbus < VBUS_ADC_MAX_RUN) &&
        (g_adc.temp < TEMP_ADC_OVER) && !board_short_input_active()) {
        protection_reset_for_start();
        pid_reset(&s_voltage_pid);
        s_start_ms = 0;
        s_relay_ms = 0;
        g_spwm_amp = 0;                    /* 从零开始软启动 */
        spwm_outputs_on();
        debug_uart_print("SPWM START\r\n");
    } else {
        protection_latch_fault(FAULT_UNDER_BUS);
    }
}

/* ==================================================================
 *  应用层初始化
 * ================================================================== */
void app_init(void)
{
    /* 1. 初始化输出为安全状态（LED灭，风扇停，继电器断） */
    board_init_outputs();

    /* 2. 初始化 PID 参数（从 inverter_config.h 加载） */
    pid_init(&s_voltage_pid);

    /* 3. 初始化 SPWM 运行时参数，设置死区 */
    spwm_init_runtime();

    /* 4. 初始化保护状态 */
    protection_init();

    /*
     * 5. 启动 ADC DMA 连续采样
     *    先校准 ADC，再以 DMA 循环模式启动 4 通道扫描。
     *    之后 s_adc_dma[] 会被 DMA 自动更新，CPU 无需干预。
     */
    adc_driver_start();

    /*
     * 6. 启动 TIM1 PWM 输出
     *    CH1/CH1N + CH2/CH2N 互补输出开启。
     *    此时 MOE 尚未使能（spwm_outputs_on 才会使能），
     *    输出保持空闲状态（低电平）。
     */
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
    HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_2);

    /* 7. 启动 TIM1 更新中断 + 刹车中断 */
    HAL_TIM_Base_Start_IT(&htim1);
    __HAL_TIM_ENABLE_IT(&htim1, TIM_IT_BREAK);

    /* 8. 启动 TIM6 1 ms 时基中断 */
    HAL_TIM_Base_Start_IT(&htim6);

    /*
     * 9. 延时 50 ms 等待电源和 ADC 稳定，
     *    读取频率/模式选择 IO，然后尝试启动。
     */
    HAL_Delay(50U);
    spwm_set_frequency(board_frequency_is_60hz() ? AC_FREQ_60HZ : AC_FREQ_50HZ);
    spwm_set_mode(board_read_spwm_mode());
    inverter_start_if_ready();
}

/* ==================================================================
 *  PID 控制任务
 * ================================================================== */
static void control_task_1ms(void)
{
    int16_t limit;
    int16_t slew;

    /* PWM 未使能时跳过控制 */
    if (!g_spwm_enabled) {
        return;
    }

    /*
     * 根据启动阶段选择 PID 参数：
     *   软启动阶段：输出上限 760 (76%)、斜率 2/ms
     *   正常阶段：输出上限 930 (93%)、斜率 5/ms
     */
    if (s_start_ms < SPWM_SOFTSTART_MS) {
        limit = PID_OUT_START_LIMIT;
        slew = PID_SLEW_PER_MS_START;
        s_start_ms++;
    } else {
        limit = PID_OUT_RUN_LIMIT;
        slew = PID_SLEW_PER_MS_RUN;
    }

    /*
     * 感性负载启动限流
     * ---------------
     * 启动阶段若检测到过载电流，降低 PID 输出上限
     * 并限制斜率为 1/ms，主动降压以限制励磁涌流。
     *
     * 注意：这个逻辑与 STM8S 版本略有不同——
     * STM8S 直接减少 out 值，而这里减少 PID out_limit。
     * 效果相似：都是限制调制幅度来限流。
     */
    /* 感性负载过流时每 ms 降 120（原 80），更快压低励磁涌流 */
    if ((s_start_ms < SPWM_SOFTSTART_MS) && (g_adc.iout > IOUT_ADC_OVERLOAD)) {
        limit = (limit > 120) ? (int16_t)(limit - 120) : limit;
        slew = 1;
    }

    /* 调用 PID 计算，输出写入 g_spwm_amp */
    g_spwm_amp = (uint16_t)pid_step(&s_voltage_pid,
                                    (int16_t)VOUT_ADC_TARGET_220V,
                                    (int16_t)g_adc.vout,
                                    limit,
                                    slew);

    /*
     * 继电器延时管理
     * 启动后等待 SPWM_RELAY_DELAY_MS 再吸合继电器。
     * PID 控制与继电器计时并行（不阻塞），
     * 确保输出电压先稳定再接通负载。
     */
    if (s_relay_ms < SPWM_RELAY_DELAY_MS) {
        s_relay_ms++;
    } else {
        board_relay_set(true);
    }
}

/* ==================================================================
 *  1 ms 主调度
 * ================================================================== */
void app_task_1ms(void)
{
    /*
     * 任务 1：ADC 采样 + 低通滤波
     *   从 DMA 缓冲读取四通道原始值，更新 g_adc 滤波值
     */
    adc_sample_filtered_1ms();

    /*
     * 任务 2：读取频率/模式 IO 并更新 SPWM 设置
     *   每次 1 ms 检查（IO 变化延迟 < 1 ms）
     */
    spwm_set_frequency(board_frequency_is_60hz() ? AC_FREQ_60HZ : AC_FREQ_50HZ);
    spwm_set_mode(board_read_spwm_mode());

    /*
     * 任务 3：保护检测
     *   扫描 BKIN/刹车/短路/过温/欠压/过载等条件
     */
    protection_task_1ms();

    /*
     * 任务 4：PID 控制（仅无故障且未短路锁定时执行）
     *   电压闭环 + 软启动 + 继电器延时
     */
    if (protection_can_run()) {
        control_task_1ms();
    }

    /*
     * 任务 5：每秒一次串口状态输出
     *   正常运行时打印 ADC 值和调制幅度，方便监控
     */
    if (++s_status_ms >= 1000U) {
        s_status_ms = 0;
        if (g_fault == FAULT_NONE) {
            debug_uart_print_status();
        }
    }
}
