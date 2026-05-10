/*
 * 保护模块实现
 * ===========
 * 完整的故障检测、响应和恢复状态机。
 *
 * 相比 STM8S 版本的新增功能：
 *   1. 短路多重重试逻辑（3 次后永久锁定）
 *   2. 母线过压保护（STM8S 版本没有）
 *   3. 过载恢复计时（电流回落 500 ms 后清零）
 *   4. 启动电流屏蔽窗口（250 ms）
 *   5. 外部短路信号独立检测（PB4 输入）
 *
 * 状态机：
 *   NORMAL → (检测到故障) → FAULT → (等待延时) → RETRY_CHECK →
 *     → (条件满足) → NORMAL (重新启动)
 *     → (条件不满足) → 继续等待
 *
 *   短路特殊路径：
 *     hw_short_event → s_short_waiting_retry → s_short_retry_count++
 *     → 达到 SHORT_RETRY_MAX → s_short_lockout (永久)
 */

#include "protection.h"
#include "adc_driver.h"
#include "board.h"
#include "debug_uart.h"
#include "pid.h"
#include "spwm.h"

volatile fault_code_t g_fault = FAULT_NONE;

/* 保护状态变量 */
static uint16_t s_overload_ms;           /* 过载累计 ms */
static uint16_t s_overload_recover_ms;   /* 过载恢复累计 ms */
static uint16_t s_short_sw_ms;           /* 软件短路确认 ms */
static uint16_t s_under_bus_ms;          /* 欠压确认 ms */
static uint16_t s_output_low_ms;         /* 输出欠压确认 ms */
static uint16_t s_short_retry_wait_ms;   /* 短路重试等待 ms */
static uint8_t  s_short_retry_count;     /* 累计短路次数 */
static bool     s_short_lockout;         /* true = 永久锁定 */
static bool     s_short_waiting_retry;   /* true = 正在等待重试 */
static uint32_t s_run_ms;               /* 正常运行累计 ms */

/* ==================================================================
 *  初始化和复位
 * ================================================================== */
void protection_init(void)
{
    g_fault = FAULT_NONE;
    protection_reset_for_start();
}

void protection_reset_for_start(void)
{
    s_overload_ms = 0;
    s_overload_recover_ms = 0;
    s_short_sw_ms = 0;
    s_under_bus_ms = 0;
    s_output_low_ms = 0;
    s_short_retry_wait_ms = 0;
    s_run_ms = 0;
}

/* ==================================================================
 *  运行条件检查
 * ================================================================== */
bool protection_can_run(void)
{
    /*
     * PID 控制仅在以下条件全部满足时执行：
     *   1. 无故障 (g_fault == FAULT_NONE)
     *   2. 未永久锁定短路 (s_short_lockout == false)
     *
     * 短路重试等待期间 s_short_lockout=false 但 g_fault=FAULT_SHORT，
     * 因此 PID 不会运行——正确。
     */
    return (g_fault == FAULT_NONE) && !s_short_lockout;
}

/* ==================================================================
 *  清除故障（非短路类型）
 * ================================================================== */
static void clear_non_latched_fault(void)
{
    g_fault = FAULT_NONE;
    protection_reset_for_start();
}

/* ==================================================================
 *  故障锁存
 * ================================================================== */
void protection_latch_fault(fault_code_t fault)
{
    /*
     * 仅锁存第一个故障（不覆盖），方便诊断。
     */
    if (g_fault == FAULT_NONE) {
        g_fault = fault;
        debug_uart_print_fault(fault, s_short_retry_count);
    }
    spwm_outputs_off();  /* 关闭 PWM + 断开继电器 */
}

/* ==================================================================
 *  硬件短路事件处理
 * ================================================================== */
void protection_short_hw_event(void)
{
    /*
     * 如果已经永久锁定，再次触发短路则保持锁存。
     */
    if (s_short_lockout) {
        protection_latch_fault(FAULT_SHORT);
        return;
    }

    /* 立即关闭输出 */
    spwm_outputs_off();
    g_fault = FAULT_SHORT;
    debug_uart_print_fault(FAULT_SHORT, s_short_retry_count);

    /* 进入重试等待状态 */
    s_short_waiting_retry = true;
    s_short_retry_wait_ms = 0;
    if (s_short_retry_count < 255U) {
        s_short_retry_count++;
    }

    /*
     * 短路次数超过阈值 → 永久锁定
     * 此后只能通过重新上电或外部复位清除。
     */
    if (s_short_retry_count >= SHORT_RETRY_MAX) {
        s_short_lockout = true;
        s_short_waiting_retry = false;
    }
}

/* ==================================================================
 *  风扇控制
 * ================================================================== */
static void fan_task(void)
{
    /*
     * 温度回差控制：
     *   TEMP > TEMP_ADC_FAN_ON   → 开风扇
     *   TEMP < TEMP_ADC_RECOVER  → 关风扇
     *
     * 回差 = 2480 - 2760 = -280（注意方向：FAN_ON 620 是运行阈值，
     * RECOVER 690 是关闭阈值，两者之间保持当前状态）。
     *
     * 实际上看代码：FAN_ON=2480, RECOVER=2760。
     * temp > 2480 → 开；temp < 2760 → 关。
     * 中间 200 个码值保持当前状态，防止频繁启停。
     */
    if (g_adc.temp > TEMP_ADC_FAN_ON) {
        board_fan_set(true);
    } else if (g_adc.temp < TEMP_ADC_RECOVER) {
        board_fan_set(false);
    }
}

/* ==================================================================
 *  短路重试任务
 * ================================================================== */
static void short_retry_task(void)
{
    /* 未等待重试或已永久锁定 → 跳过 */
    if (!s_short_waiting_retry || s_short_lockout) {
        return;
    }

    /*
     * 等待 SHORT_RETRY_DELAY_MS (2 秒)
     */
    if (++s_short_retry_wait_ms < SHORT_RETRY_DELAY_MS) {
        return;
    }

    /*
     * 重试条件：
     *   1. 母线电压正常
     *   2. 温度正常
     *   3. 外部短路信号已解除
     *
     * 条件满足 → 清除故障并重新开启 PWM
     * 条件不满足 → 继续等待（回退计时器，100 ms 后再检）
     */
    if ((g_adc.vbus > VBUS_ADC_MIN_RUN) && (g_adc.vbus < VBUS_ADC_MAX_RUN) &&
        (g_adc.temp < TEMP_ADC_RECOVER) && !board_short_input_active()) {
        clear_non_latched_fault();
        s_short_waiting_retry = false;
        spwm_clear_break_pending();
        spwm_outputs_on();
        debug_uart_print("SHORT RETRY\r\n");
    } else {
        s_short_retry_wait_ms = SHORT_RETRY_DELAY_MS - 100U;
    }
}

/* ==================================================================
 *  保护检测 1 ms 任务
 * ================================================================== */
void protection_task_1ms(void)
{
    /*
     * 任务 1：风扇控制（独立于故障状态，始终工作）
     */
    fan_task();

    /*
     * 任务 2：硬件刹车/外部短路检测
     *   优先级最高——每次 1 ms 最先检查。
     *   BKIN 挂起标志或 PB4 外部短路信号任一有效 → 进入短路处理。
     */
    if (spwm_break_pending() || board_short_input_active()) {
        spwm_clear_break_pending();
        protection_short_hw_event();
    }

    /*
     * 任务 3：故障 LED 闪烁
     *   由 board 层管理闪烁状态机。
     */
    board_fault_led_task_1ms(g_fault);

    /*
     * 任务 4：短路重试管理
     *   在等待重试期间检测恢复条件。
     */
    short_retry_task();

    /*
     * 以下检测仅在无故障时进行。
     * 已锁存故障时不扫描新故障（单故障锁存策略）。
     */
    if (g_fault != FAULT_NONE) {
        return;
    }

    /*
     * 任务 5：运行时间累计
     *   仅 PWM 实际开启时递增。
     */
    if (g_spwm_enabled) {
        s_run_ms++;
    }

    /*
     * 任务 6：母线欠压检测
     *   需持续 UNDER_BUS_TRIP_MS (80 ms) 才触发。
     *   瞬时跌落（如电机启动）不会误触发。
     */
    if (g_adc.vbus < VBUS_ADC_MIN_RUN) {
        if (++s_under_bus_ms >= UNDER_BUS_TRIP_MS) {
            protection_latch_fault(FAULT_UNDER_BUS);
        }
        return;
    }
    s_under_bus_ms = 0;

    /*
     * 任务 7：母线过压检测
     *   立即锁存（不延时）。母线过压可损坏母线电容和 MOSFET。
     */
    if (g_adc.vbus > VBUS_ADC_MAX_RUN) {
        protection_latch_fault(FAULT_OVER_BUS);
        return;
    }

    /*
     * 任务 8：过温检测
     *   立即锁存（不延时）。功率器件过热可瞬间损坏。
     */
    if (g_adc.temp > TEMP_ADC_OVER) {
        protection_latch_fault(FAULT_OVER_TEMP);
        return;
    }

    /*
     * 任务 9：软件短路检测
     *   ADC 电流超过软件短路阈值持续 SHORT_SW_TRIP_MS (2 ms) → 短路。
     *   这作为硬件 BKIN（微秒级）的补充保护。
     */
    if (g_adc.iout > IOUT_ADC_SHORT_SW) {
        if (++s_short_sw_ms >= SHORT_SW_TRIP_MS) {
            protection_short_hw_event();
        }
        return;
    }
    s_short_sw_ms = 0;

    /*
     * 任务 10：过载检测
     *   仅在启动电流屏蔽窗口 (250 ms) 之后检测。
     *   连续过载 OVERLOAD_TRIP_MS (1.5 s) 后触发保护。
     *   电流回落并保持 OVERLOAD_RECOVER_MS (500 ms) 后清零计时器。
     */
    if (s_run_ms > STARTUP_CURRENT_BLANK_MS && g_adc.iout > IOUT_ADC_OVERLOAD) {
        s_overload_recover_ms = 0;
        if (++s_overload_ms >= OVERLOAD_TRIP_MS) {
            protection_latch_fault(FAULT_OVERLOAD);
        }
    } else if (s_overload_ms > 0U) {
        if (++s_overload_recover_ms >= OVERLOAD_RECOVER_MS) {
            s_overload_ms = 0;
            s_overload_recover_ms = 0;
        }
    }

    /*
     * 任务 11：输出欠压检测
     *   仅在软启动完成后检测。
     *   持续 OUTPUT_LOW_TRIP_MS (700 ms) 后触发。
     *   可能原因：负载短路、输出开路、反馈回路断裂。
     */
    if ((s_run_ms > SPWM_SOFTSTART_MS) && (g_adc.vout < VOUT_ADC_LOW_LOCK)) {
        if (++s_output_low_ms >= OUTPUT_LOW_TRIP_MS) {
            protection_latch_fault(FAULT_OUTPUT_LOW);
        }
    } else {
        s_output_low_ms = 0;
    }
}
