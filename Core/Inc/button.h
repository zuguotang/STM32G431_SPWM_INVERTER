#ifndef BUTTON_H
#define BUTTON_H

/*
 * 按键驱动（数字 GPIO，3 键）
 * ==========================
 * PF0  = BTN_UP   (下拉输入)
 * PA11 = BTN_DOWN (下拉输入)
 * PA12 = BTN_OK   (下拉输入)
 *
 * 消抖：50ms 确认 + 支持短按(<1s)/长按(≥1s)/连发(>500ms 每100ms)。
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
    BTN_EVENT_SHORT,
    BTN_EVENT_LONG,
    BTN_EVENT_REPEAT
} button_event_t;

void button_init(void);
void button_scan_1ms(void);
button_id_t button_get_event(button_event_t *event);

#endif
