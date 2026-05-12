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
#include "param_store.h"
#include "ssd1306.h"
#include "lcd_menu.h"
#include "button.h"

static pid_q10_t s_voltage_pid;    /* 电压环 PID 控制器 */
static uint32_t s_start_ms;        /* 启动计时器（从 0 到 SPWM_SOFTSTART_MS） */
static uint32_t s_relay_ms;        /* 继电器延时计时器 */
static uint16_t s_status_ms;       /* 串口状态输出间隔计时器 */

bool g_relay_startup_ok = false;   /* 上电延时完成（供 bus_relay_task 用） */
bool g_relay_engaged = false;       /* 继电器已吸合（bus_relay_task 置位） */

bool g_system_ready = false;        /* 基础条件满足（供 protection.c 短路重试使用） */
static bool s_spwm_started = false; /* SPWM 已启动（避免重复触发） */

/* ==================================================================
 *  启动条件检查（不立即启动，等继电器吸合后再启动 SPWM）
 *
 *  时序：
 *    上电 → 条件检查(本函数) → 延时 → 继电器吸合 → 软启动
 *
 *            inverter_start_if_ready()     bus_relay_task()     control_task_1ms()
 *            ├─ Vbus 在运行窗口?          ├─ Vbus 在继电器窗口?  ├─ relay engaged?
 *            ├─ 温度正常?                 ├─ 延时到?            ├─ 启动 SPWM
 *            ├─ 无短路?                   ├─ 吸合继电器         └─ 开始软启动
 *            └─ 置 g_system_ready=true     └─ 置 g_relay_engaged
 * ================================================================== */
static void inverter_start_if_ready(void)
{
    adc_sample_filtered_1ms();

    if ((g_adc.vbus > VBUS_ADC_MIN_RUN) && (g_adc.vbus < VBUS_ADC_MAX_RUN) &&
        (g_adc.temp_celsius < TEMP_OVER_CELSIUS) && !board_short_input_active()) {
        /*
         * 基础条件满足，但不立即启动 SPWM。
         * 等 bus_relay_task 吸合继电器后再触发软启动。
         */
        protection_reset_for_start();
        pid_reset(&s_voltage_pid);
        s_start_ms = 0;
        s_relay_ms = 0;
        g_relay_startup_ok = false;
        g_system_ready = true;     /* 通知 bus_relay_task：系统就绪，可以吸合 */
        s_spwm_started = false;
        g_spwm_amp = 0;
        debug_uart_print("SYSTEM READY, wait relay...\r\n");
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

    /* 1.5 从 Flash 加载运行时参数（必须在 pid_init 之前！） */
    param_store_init();

    /* 2. 初始化 PID 参数（从 g_params 加载，已由 param_store_init 赋值） */
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

    /* 8.5 初始化 OLED + 按键 + 菜单 */
    ssd1306_init();
    button_init();
    menu_init();

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
 *
 *  启动时序（严格保证）：
 *    1. g_system_ready = true     → 基础条件检查通过
 *    2. s_relay_ms 计时到         → g_relay_startup_ok = true
 *    3. bus_relay_task 吸合继电器  → g_relay_engaged = true
 *    4. 本函数检测到 relay 吸合   → spwm_outputs_on() → 软启动开始
 * ================================================================== */
static void control_task_1ms(void)
{
    int16_t limit;
    int16_t slew;

    /*
     * 阶段 0：等待继电器吸合。
     *   独立于 g_spwm_enabled 判断——即使 SPWM 未使能，只要基础条件
     *   已通过 (g_system_ready)，就累计延时等继电器。继电器吸合后
     *   才启动 SPWM，保证时序严格。
     *
     *   短路恢复场景：spwm_outputs_on 使能了 SPWM，但 s_spwm_started
     *   为 false → 走这里重新等继电器。
     */
    if (!s_spwm_started && g_system_ready) {

        /* 累计上电延时（SPWM 未使能时也要计时） */
        if (s_relay_ms < VBUS_RELAY_STARTUP_DELAY_MS) {
            s_relay_ms++;
        } else {
            g_relay_startup_ok = true;
        }

        /* 继电器吸合 → 启动 SPWM 软启动 */
        if (g_relay_engaged) {
            s_spwm_started = true;
            g_spwm_amp = 0;
            spwm_outputs_on();
            debug_uart_print("RELAY ON, SPWM SOFTSTART\r\n");
            return;
        }
        return;
    }

    /* SPWM 未使能且系统未就绪（故障/停止/首次上电等待检查） */
    if (!g_spwm_enabled && !g_system_ready) {
        return;
    }

    /* SPWM 被关闭（故障），复位所有启动标志 */
    if (!g_spwm_enabled) {
        g_relay_startup_ok = false;
        g_relay_engaged = false;
        s_relay_ms = 0;
        s_spwm_started = false;
        g_system_ready = false;
        return;
    }

    /*
     * 阶段 1 & 2：软启动 + 正常运行
     *   根据启动阶段选择 PID 参数
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
     * 继电器管理（母线电压窗口）
     *
     *   继电器用于母线侧控制（预充电旁路/直流接触器等），
     *   在 control_task_1ms 中仅处理正常运行的延时吸合。
     *
     *   断开由 protection_task_1ms 统一管理：
     *     - Vbus 超出 [VBUS_RELAY_ON_MIN, VBUS_RELAY_ON_MAX] → 断开
     *     - 任何故障 → spwm_outputs_off() → 断开
     *
     *   上电延时 VBUS_RELAY_STARTUP_DELAY_MS 后才允许吸合，
     *   给母线电容充电留足时间（400V 系统需要更长的预充电）。
     */
    if (s_relay_ms < VBUS_RELAY_STARTUP_DELAY_MS) {
        s_relay_ms++;
    } else {
        g_relay_startup_ok = true;   /* 上电延时到，通知 protection.c 可以吸合 */
    }
    /* 继电器实际吸合由 protection_task_1ms 中的 bus_relay_task 判断母线窗口决定 */
}

/* ==================================================================
 *  1 ms 主调度
 * ================================================================== */
void app_task_1ms(void)
{
    /*
     * 任务 1：ADC 采样 + 低通滤波 + RMS 有效值计算
     *   从 DMA 缓冲读取四通道原始值，更新 g_adc 滤波值
     *   adc_calc_rms_1ms() 累积 800 个采样点后更新 RMS 电压/电流/功率
     */
    adc_sample_filtered_1ms();
    adc_calc_rms_1ms();

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

    /*
     * 任务 6：按键扫描 + OLED 菜单 + I2C 逐页刷新
     *   按键消抖 (~3us) + 菜单状态机 (~50us) + 一页 I2C 发送 (~260us)
     */
    button_scan_1ms();
    menu_task_1ms();
    if (ssd1306_is_busy()) {
        ssd1306_refresh_tick_1ms();
    }
}
