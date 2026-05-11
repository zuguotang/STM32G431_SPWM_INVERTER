#ifndef LCD_MENU_H
#define LCD_MENU_H

/*
 * LCD 菜单系统
 * ===========
 *
 * 状态机：
 *   MAIN_SCREEN  → BTN_OK短按 → MAIN_MENU
 *   MAIN_MENU    → BTN_OK短按 → EDIT_PARAM
 *   MAIN_MENU    → 选"保存" → SAVE_CONFIRM
 *   EDIT_PARAM   → BTN_OK短按 → MAIN_MENU（接受新值）
 *   SAVE_CONFIRM → BTN_OK     → Flash 写入 → MAIN_SCREEN
 *
 * 任何非主屏状态 + 10 秒无操作 → 自动回到 MAIN_SCREEN。
 * 故障时强制显示故障信息，覆盖所有其他显示。
 */

#include <stdint.h>
#include <stdbool.h>

void menu_init(void);
void menu_task_1ms(void);

/* 故障发生时强制切回主屏显示故障 */
void menu_force_to_main(void);

#endif
