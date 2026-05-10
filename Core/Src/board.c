/*
 * 板级支持层实现
 * =============
 * 封装 HAL GPIO 操作，提供：
 *   - 输出控制（LED/风扇/继电器）
 *   - 输入读取（频率/模式/短路信号）
 *   - 故障 LED 闪烁码生成
 */

#include "board.h"

volatile uint32_t g_ms = 0;  /* 系统毫秒计数器，TIM6 ISR 递增 */

/* ==================================================================
 *  输出初始化
 * ================================================================== */
void board_init_outputs(void)
{
    /* 确保所有输出在上电时为安全状态 */
    board_fault_led_set(false);
    board_fan_set(false);
    board_relay_set(false);
}

/* ==================================================================
 *  故障 LED 控制
 * ================================================================== */
void board_fault_led_set(bool on)
{
    /* HAL 抽象层：GPIO_PIN_SET = 高电平点亮 */
    HAL_GPIO_WritePin(FAULT_LED_GPIO_Port, FAULT_LED_Pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/*
 * 故障码 → 闪烁次数映射
 * ---------------------
 * 返回闪烁脉冲数（亮灭各算 1 次，即 N 次闪烁 = 2N 个相位）。
 */
uint8_t board_fault_blink_count(fault_code_t fault)
{
    switch (fault) {
    case FAULT_SHORT:      return 1U;
    case FAULT_OVERLOAD:   return 2U;
    case FAULT_OVER_TEMP:  return 3U;
    case FAULT_UNDER_BUS:  return 4U;
    case FAULT_OUTPUT_LOW: return 5U;
    case FAULT_OVER_BUS:   return 6U;
    default:               return 0U;  /* FAULT_NONE → 不闪烁 */
    }
}

/*
 * 故障 LED 闪烁状态机（每 1 ms 调用）
 * ---------------------------------
 * 时序（以 SHORT = 1 次闪烁为例）：
 *   200 ms 亮 → 200 ms 灭 → 600 ms 间隔 → 重复
 *
 * 总周期取决于故障码的闪烁次数，越长则越容易区分。
 */
void board_fault_led_task_1ms(fault_code_t fault)
{
    static uint16_t tick = 0;     /* 200 ms 计步器 */
    static uint8_t pulse = 0;     /* 当前闪烁相位 */
    uint8_t total = board_fault_blink_count(fault);

    /* 无故障时保持熄灭，清零状态 */
    if (fault == FAULT_NONE) {
        tick = 0;
        pulse = 0;
        board_fault_led_set(false);
        return;
    }

    /* 每 200 ms 切换一次 LED 状态 */
    if (++tick < 200U) {
        return;
    }
    tick = 0;

    /*
     * 闪烁阶段：共 total×2 个相位（奇数次亮、偶数次灭）
     * 间隔阶段：6 个相位（约 600 ms）熄灭
     */
    if (pulse < (uint8_t)(total * 2U)) {
        board_fault_led_set((pulse & 1U) == 0U);  /* 偶数相位亮、奇数灭 */
        pulse++;
    } else {
        board_fault_led_set(false);
        if (pulse++ >= (uint8_t)(total * 2U + 6U)) {
            pulse = 0;  /* 循环结束，重新开始 */
        }
    }
}

/* ==================================================================
 *  风扇 / 继电器控制
 * ================================================================== */
void board_fan_set(bool on)
{
    HAL_GPIO_WritePin(FAN_GPIO_Port, FAN_Pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void board_relay_set(bool on)
{
    HAL_GPIO_WritePin(RELAY_GPIO_Port, RELAY_Pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/* ==================================================================
 *  GPIO 输入读取
 * ================================================================== */
bool board_frequency_is_60hz(void)
{
    /* PB1 高电平 → 60 Hz，低电平 → 50 Hz */
    return HAL_GPIO_ReadPin(FREQ_SEL_GPIO_Port, FREQ_SEL_Pin) == GPIO_PIN_SET;
}

spwm_mode_t board_read_spwm_mode(void)
{
    /* PB2 高电平 → 双极性，低电平 → 单极性 */
    return (HAL_GPIO_ReadPin(MODE_SEL_GPIO_Port, MODE_SEL_Pin) == GPIO_PIN_SET)
           ? SPWM_MODE_BIPOLAR : SPWM_MODE_UNIPOLAR;
}

bool board_short_input_active(void)
{
    /* PB4 高电平 → 外部比较器指示短路 */
    return HAL_GPIO_ReadPin(SHORT_MCU_GPIO_Port, SHORT_MCU_Pin) == GPIO_PIN_SET;
}
