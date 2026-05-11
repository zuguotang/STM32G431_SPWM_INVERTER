/*
 * 按键驱动实现（数字 GPIO，3 键，消抖状态机）
 */

#include "button.h"
#include "inverter_config.h"
#include "stm32g4xx_hal.h"

#define BTN_DEBOUNCE_MS     50
#define BTN_LONG_MS         1000
#define BTN_REPEAT_START_MS 500
#define BTN_REPEAT_MS       100

static button_id_t   s_last_event_btn  = BTN_NONE;
static button_event_t s_last_event_type = BTN_EVENT_NONE;

static button_id_t s_cur_btn    = BTN_NONE;
static button_id_t s_stable_btn = BTN_NONE;
static uint16_t    s_hold_ms    = 0;
static uint16_t    s_debounce_ms = 0;
static button_id_t s_debounce_btn = BTN_NONE;
static uint16_t    s_repeat_ms   = 0;

/* ==================================================================
 *  读取 3 个按键 GPIO
 * ================================================================== */
static button_id_t read_buttons(void)
{
    if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_5)  == GPIO_PIN_SET) return BTN_UP;
    if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_11) == GPIO_PIN_SET) return BTN_DOWN;
    if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_12) == GPIO_PIN_SET) return BTN_OK;
    return BTN_NONE;
}

void button_init(void)
{
    s_last_event_btn  = BTN_NONE;
    s_last_event_type = BTN_EVENT_NONE;
}

void button_scan_1ms(void)
{
    button_id_t raw = read_buttons();

    /* 消抖：同一按钮需连续 BTN_DEBOUNCE_MS 次 */
    if (raw == s_debounce_btn) {
        s_debounce_ms++;
    } else {
        s_debounce_btn = raw;
        s_debounce_ms = 0;
    }

    /* 消抖确认 */
    if (s_debounce_ms >= BTN_DEBOUNCE_MS && s_stable_btn != s_debounce_btn) {
        if (s_stable_btn != BTN_NONE && s_hold_ms < BTN_LONG_MS) {
            s_last_event_btn  = s_stable_btn;
            s_last_event_type = BTN_EVENT_SHORT;
        }
        s_stable_btn = s_debounce_btn;
        s_hold_ms    = 0;
        s_repeat_ms  = 0;
    }

    if (s_stable_btn == BTN_NONE) return;

    s_hold_ms++;

    /* 长按 */
    if (s_hold_ms == BTN_LONG_MS) {
        s_last_event_btn  = s_stable_btn;
        s_last_event_type = BTN_EVENT_LONG;
    }

    /* 连发 */
    if (s_hold_ms >= BTN_REPEAT_START_MS) {
        s_repeat_ms++;
        if (s_repeat_ms >= BTN_REPEAT_MS) {
            s_repeat_ms = 0;
            s_last_event_btn  = s_stable_btn;
            s_last_event_type = BTN_EVENT_REPEAT;
        }
    }
}

button_id_t button_get_event(button_event_t *event)
{
    button_id_t btn = s_last_event_btn;
    *event = s_last_event_type;
    s_last_event_btn  = BTN_NONE;
    s_last_event_type = BTN_EVENT_NONE;
    return btn;
}
