#ifndef BUTTON_H
#define BUTTON_H

/*
 * 按键驱动（ADC 电阻分压）
 * =======================
 *
 * 使用 PA10 (ADC1_IN13) 读取三个按键的电阻分压值。
 * 省 GPIO（1 个 ADC 通道替代 3 个数字输入）。
 *
 * 电阻阶梯（3.3V → R1 → R2 → R3 → R4 → GND）：
 *   BTN_UP   按下 → ADC ≈ 3720 (3.0V)
 *   BTN_DOWN 按下 → ADC ≈ 2730 (2.2V)
 *   BTN_OK   按下 → ADC ≈ 1365 (1.1V)
 *   无按键   → ADC ≈ 0 (下拉到 GND)
 *
 * 消抖：50ms 确认 + 10% 滞回窗口。
 * 支持短按（<1s）和长按（≥1s）和连发（>500ms 后每 100ms）。
 */

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    BTN_NONE = 0,
    BTN_UP,
    BTN_DOWN,
    BTN_OK
} button_id_t;

typedef enum {
    BTN_EVENT_NONE = 0,
    BTN_EVENT_SHORT,   /* 短按释放（<1s） */
    BTN_EVENT_LONG,    /* 长按（≥1s 按住） */
    BTN_EVENT_REPEAT   /* 连发（按住 >500ms 后每 100ms） */
} button_event_t;

/* 初始化 PA10 为 ADC 输入（需在 HAL_ADC_MspInit 中配置） */
void button_init(void);

/* 每 1ms 调用：采样 ADC + 消抖状态机 */
void button_scan_1ms(void);

/* 获取最新按键事件（消费型：读后清零） */
button_id_t button_get_event(button_event_t *event);

#endif
