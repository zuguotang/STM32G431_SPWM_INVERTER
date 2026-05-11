/*
 * 按键驱动实现（ADC 电阻分压 + 消抖状态机）
 */

#include "button.h"
#include "inverter_config.h"
#include "stm32g4xx_hal.h"

/*
 * 按钮 ADC 阈值（12 位，0..4095，3.3V 基准）
 * 每个按钮有 ±10% 滞回窗口。
 *
 * 电阻阶梯：3.3V → 10k → BTN_UP → 10k → BTN_DOWN → 10k → BTN_OK → 10k → GND
 *
 *   BTN_UP:   3.3 × 30k/40k = 2.475V → ADC ≈ 3070 (2700..3400)
 *   BTN_DOWN: 3.3 × 20k/40k = 1.650V → ADC ≈ 2050 (1800..2300)
 *   BTN_OK:   3.3 × 10k/40k = 0.825V → ADC ≈ 1025 (800..1200)
 *   GND:      0V                     → ADC < 200
 */

/* 阈值中心值 */
#define BTN_UP_ADC_CENTER    3070
#define BTN_DOWN_ADC_CENTER  2050
#define BTN_OK_ADC_CENTER    1025

/* 滞回窗口 = 中心值 ± 10% */
#define BTN_UP_HIGH          3370
#define BTN_UP_LOW           2770
#define BTN_DOWN_HIGH        2250
#define BTN_DOWN_LOW         1850
#define BTN_OK_HIGH          1120
#define BTN_OK_LOW           930
#define BTN_IDLE_MAX         200    /* 低于此值认为无按键 */

/* 消抖定时器 */
#define BTN_DEBOUNCE_MS      50     /* 确认按下所需的持续 ms */
#define BTN_LONG_MS          1000   /* 长按判定 ms */
#define BTN_REPEAT_START_MS   500   /* 连发起始 ms */
#define BTN_REPEAT_MS         100   /* 连发间隔 ms */

/* ==================================================================
 *  按键状态机 (每个 1ms 调用)
 * ================================================================== */

static button_id_t s_last_event_btn = BTN_NONE;
static button_event_t s_last_event_type = BTN_EVENT_NONE;

static button_id_t s_cur_btn = BTN_NONE;   /* 当前 ADC 读数对应按钮 */
static button_id_t s_stable_btn = BTN_NONE;/* 消抖确认后的按钮 */
static uint16_t s_hold_ms = 0;              /* 稳定状态下已按住 ms */
static uint16_t s_debounce_ms = 0;          /* 当前候选按钮的持续 ms */
static button_id_t s_debounce_btn = BTN_NONE;
static uint16_t s_repeat_timer_ms = 0;      /* 连发间隔计时 */

/* ==================================================================
 *  ADC 单通道读取（PA10 = ADC1_IN13）
 * ================================================================== */
void button_init(void)
{
    /* PA10 已在 stm32g4xx_hal_msp.c 的 HAL_ADC_MspInit 中配置为模拟输入 */
    s_last_event_btn = BTN_NONE;
    s_last_event_type = BTN_EVENT_NONE;
}

/* ==================================================================
 *  ADC 读取并判定按钮
 * ================================================================== */
static button_id_t adc_to_button(uint16_t adc)
{
    if (adc > BTN_UP_LOW && adc < BTN_UP_HIGH)     return BTN_UP;
    if (adc > BTN_DOWN_LOW && adc < BTN_DOWN_HIGH) return BTN_DOWN;
    if (adc > BTN_OK_LOW && adc < BTN_OK_HIGH)     return BTN_OK;
    return BTN_NONE;
}

static uint16_t adc_read_raw(void)
{
    /*
     * 使用 ADC1 直接读取 PA10 (IN13)。
     * 注意：ADC1 正在做 DMA 连续扫描 4 通道（IN1~IN4），
     * 这里需要临时切换或者直接通过软件触发单次转换。
     *
     * 简化方案：使用 ADC1 的注入通道或直接复用现有的 DMA 缓冲。
     * 由于按键不需要高速采样，这里直接在 1ms 任务中
     * 临时中断 DMA 扫描、读一次 IN13、再恢复 DMA。
     *
     * 更优方案：使用注入通道（Injected Channel），
     * 由 TIM6 触发，每 1ms 自动采样一次 IN13，不干扰规则组 DMA。
     *
     * 当前实现：简化版——直接读 DMA 缓冲中对应 IN13 的值。
     * 需要把 IN13 加入规则组扫描（变为 5 通道），或者单独读。
     */

    /* 简化：直接软件触发一次 ADC 转换 */
    ADC1->CFGR &= ~ADC_CFGR_DMAEN;       /* 暂停 DMA */
    ADC1->CFGR &= ~ADC_CFGR_CONT;        /* 暂停连续模式 */

    ADC1->SQR1 = (0U << ADC_SQR1_SQ1_Pos); /* 仅转换 1 个通道 */
    ADC1->SQR1 &= ~ADC_SQR1_L;             /* 序列长度 = 1 */
    ADC1->SQR1 |= (13U << ADC_SQR1_SQ1_Pos); /* CH13 = IN13 (PA10) */

    ADC1->CR |= ADC_CR_ADSTART;          /* 启动单次转换 */
    while ((ADC1->ISR & ADC_ISR_EOC) == 0U) {} /* 等待完成 */

    uint16_t result = ADC1->DR;

    /* 恢复扫描配置 */
    ADC1->SQR1 = (4U - 1U) << ADC_SQR1_L_Pos; /* 4 个通道 */
    ADC1->SQR1 &= ~(0x1FU << ADC_SQR1_SQ1_Pos);
    ADC1->SQR1 |= (1U << ADC_SQR1_SQ1_Pos);   /* CH1 = IN1 */
    ADC1->SQR1 |= (2U << ADC_SQR1_SQ2_Pos);
    ADC1->SQR1 |= (3U << ADC_SQR1_SQ3_Pos);
    ADC1->SQR1 |= (4U << ADC_SQR1_SQ4_Pos);

    ADC1->CFGR |= ADC_CFGR_CONT | ADC_CFGR_DMAEN;
    ADC1->CR |= ADC_CR_ADSTART;

    return result;
}

/* ==================================================================
 *  1 ms 扫描主函数
 * ================================================================== */
void button_scan_1ms(void)
{
    /* 读取并识别当前按钮 */
    uint16_t adc = adc_read_raw();
    button_id_t raw_btn = adc_to_button(adc);

    /*
     * 消抖：同一按钮需连续检测 BTN_DEBOUNCE_MS 次才确认。
     * 按钮变化时重置消抖计时。
     */
    if (raw_btn == s_debounce_btn) {
        s_debounce_ms++;
    } else {
        s_debounce_btn = raw_btn;
        s_debounce_ms = 0;
    }

    /* 消抖确认 */
    if (s_debounce_ms >= BTN_DEBOUNCE_MS && s_stable_btn != s_debounce_btn) {
        /* 状态变化 */
        if (s_stable_btn != BTN_NONE) {
            /* 之前按着别的键 → 释放旧键 */
            if (s_hold_ms < BTN_LONG_MS) {
                s_last_event_btn = s_stable_btn;
                s_last_event_type = BTN_EVENT_SHORT;
            }
        }
        s_stable_btn = s_debounce_btn;
        s_hold_ms = 0;
        s_repeat_timer_ms = 0;
    }

    if (s_stable_btn == BTN_NONE) {
        return;
    }

    /* 稳定按钮状态：计时 */
    s_hold_ms++;

    /* 长按检测 */
    if (s_hold_ms == BTN_LONG_MS) {
        s_last_event_btn = s_stable_btn;
        s_last_event_type = BTN_EVENT_LONG;
    }

    /* 连发检测（仅在超过长按阈值后生效） */
    if (s_hold_ms >= BTN_REPEAT_START_MS) {
        s_repeat_timer_ms++;
        if (s_repeat_timer_ms >= BTN_REPEAT_MS) {
            s_repeat_timer_ms = 0;
            s_last_event_btn = s_stable_btn;
            s_last_event_type = BTN_EVENT_REPEAT;
        }
    }
}

/* ==================================================================
 *  事件消费
 * ================================================================== */
button_id_t button_get_event(button_event_t *event)
{
    button_id_t btn = s_last_event_btn;
    *event = s_last_event_type;
    s_last_event_btn = BTN_NONE;
    s_last_event_type = BTN_EVENT_NONE;
    return btn;
}
