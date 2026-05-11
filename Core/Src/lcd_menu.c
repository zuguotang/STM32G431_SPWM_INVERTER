/*
 * LCD 菜单系统实现
 * ===============
 * 主屏显示运行状态，菜单模式编辑参数。
 */

#include "lcd_menu.h"
#include "lcd_nokia5110.h"
#include "button.h"
#include "param_store.h"
#include "inverter_config.h"
#include "adc_driver.h"
#include "spwm.h"
#include "protection.h"
#include "board.h"

/* 菜单状态 */
typedef enum {
    ST_MAIN_SCREEN = 0,
    ST_MAIN_MENU,
    ST_EDIT_PARAM,
    ST_SAVE_CONFIRM
} menu_state_t;

/* 菜单项索引 */
enum {
    MI_DEADTIME = 0,
    MI_SOFTSTART,
    MI_VOUT_TARGET,
    MI_PID_KP,
    MI_PID_KI,
    MI_OVERLOAD_TRIP,
    MI_PID_START_LIMIT,
    MI_SAVE_FLASH,
    MI_COUNT
};

/* 每个参数的定义 */
typedef struct {
    const char *name;          /* 参数名称（显示在菜单） */
    const char *unit;          /* 单位 */
    uint16_t   *value;         /* 指向 g_params 中的字段 */
    uint16_t   min;            /* 最小值 */
    uint16_t   max;            /* 最大值 */
    uint16_t   step;           /* 步进值 */
    uint16_t   step_fast;      /* 长按连发时的步进（更大步进） */
    uint8_t    divisor;        /* Q10 格式除 1024 显示为浮点？0=整数显示 */
} menu_item_t;

/* ==================================================================
 *  菜单项定义（对应 g_params 字段）
 * ================================================================== */
static menu_item_t s_menu_items[MI_COUNT] = {
    [MI_DEADTIME]      = {"DeadTime",  "ns",  &g_params.deadtime_ns,      500, 5000, 50, 200, 0},
    [MI_SOFTSTART]     = {"SoftStart", "s",   &g_params.softstart_ms,     1000,30000,100,1000,0},
    [MI_VOUT_TARGET]   = {"VoutADC",   "",    &g_params.vout_target_adc,  0,   4095, 10, 50,  0},
    [MI_PID_KP]        = {"PID Kp",    "",    &g_params.pid_kp_q10,       0,   1023, 5,  20,  10},
    [MI_PID_KI]        = {"PID Ki",    "",    &g_params.pid_ki_q10,       0,   255,  1,  5,   10},
    [MI_OVERLOAD_TRIP] = {"Ovl.Trip",  "ms",  &g_params.overload_trip_ms, 100, 10000,100,500,0},
    [MI_PID_START_LIMIT]={"St.Limit",  "",    &g_params.pid_start_limit,  100, 930,  10, 50,  0},
    [MI_SAVE_FLASH]    = {"SaveFlash", NULL, NULL, 0, 0, 0, 0, 0},
};

/* 菜单状态变量 */
static menu_state_t s_state = ST_MAIN_SCREEN;
static uint8_t s_cursor = 0;         /* 当前菜单项索引 */
static uint8_t s_edit_value_hi;      /* 编辑值高字节（uint16 的编辑缓冲） */
static uint8_t s_edit_value_lo;
static bool s_save_yes = false;      /* 保存确认：Yes/No */
static uint16_t s_idle_ms = 0;       /* 空闲计时器（无操作自动回主屏） */
#define IDLE_TIMEOUT_MS 10000

/* ==================================================================
 *  辅助函数
 * ================================================================== */

static uint16_t edit_get_value(void)
{
    return ((uint16_t)s_edit_value_hi << 8) | s_edit_value_lo;
}

static void edit_set_value(uint16_t v)
{
    s_edit_value_hi = (uint8_t)(v >> 8);
    s_edit_value_lo = (uint8_t)v;
}

static void edit_apply(void)
{
    uint16_t v = edit_get_value();
    menu_item_t *item = &s_menu_items[s_cursor];
    *item->value = v;
}

/* Q10 格式显示为浮点字符串（如 0.254） */
static void q10_to_str(uint16_t q10, char *buf)
{
    /* 例如 q10=260 → val=260/1024≈0.254 → "0.254" */
    uint16_t int_part = q10 / 1024;
    uint16_t frac = q10 % 1024;
    /* 取 3 位小数 */
    uint16_t frac3 = (uint16_t)((uint32_t)frac * 1000 / 1024);

    uint8_t pos = 0;
    if (int_part >= 100) {
        buf[pos++] = (char)('0' + (int_part / 100));
        int_part %= 100;
    }
    if (int_part >= 10 || pos > 0) {
        buf[pos++] = (char)('0' + (int_part / 10));
        int_part %= 10;
    }
    buf[pos++] = (char)('0' + int_part);
    buf[pos++] = '.';
    buf[pos++] = (char)('0' + (frac3 / 100));
    buf[pos++] = (char)('0' + ((frac3 / 10) % 10));
    buf[pos++] = (char)('0' + (frac3 % 10));
    buf[pos] = '\0';
}

/* ==================================================================
 *  渲染主屏
 * ================================================================== */
static void render_main_screen(void)
{
    lcd_clear();
    char line[15];  /* 84/6 = 14 字符/行 */

    /* 第 0 行：标题 */
    lcd_draw_string(0, 0, "INVERTER V2.0");

    /* 第 1 行：输出电压 + 电流 */
    uint8_t vout_disp = (uint8_t)((uint32_t)g_adc.vout * 100 / 4095);
    snprintf(line, sizeof(line), "Vo:%02u Io:%04u", (unsigned)vout_disp, (unsigned)g_adc.iout);
    lcd_draw_string(0, 10, line);

    /* 第 2 行：频率 + 模式 */
    snprintf(line, sizeof(line), "%s %s",
             board_frequency_is_60hz() ? "60Hz" : "50Hz",
             g_spwm_enabled ? "RUN" : "STOP");
    lcd_draw_string(0, 19, line);

    /* 第 3 行：母线电压 + 温度 */
    uint8_t temp_pct = (uint8_t)((uint32_t)g_adc.temp * 100 / 4095);
    snprintf(line, sizeof(line), "Vb:%04u T:%02u", (unsigned)g_adc.vbus, (unsigned)temp_pct);
    lcd_draw_string(0, 28, line);

    /* 第 4 行：调制幅度 + 故障码 */
    if (g_fault == FAULT_NONE) {
        snprintf(line, sizeof(line), "AMP:%03u", (unsigned)g_spwm_amp);
    } else {
        snprintf(line, sizeof(line), "FLT:%01u", (unsigned)g_fault);
    }
    lcd_draw_string(0, 37, line);

    /* 底部分隔线 */
    lcd_draw_hline(0, 46, 84, true);

    lcd_request_refresh();
}

/* ==================================================================
 *  渲染菜单
 * ================================================================== */
static void render_menu(void)
{
    lcd_clear();

    /* 标题 */
    lcd_draw_string(0, 0, ">>> MENU <<<");

    /* 显示 4 个菜单项（可滚动） */
    uint8_t scroll = 0;
    if (s_cursor >= 4) scroll = (uint8_t)(s_cursor - 3);

    for (uint8_t i = 0; i < 4; i++) {
        uint8_t idx = (uint8_t)(scroll + i);
        if (idx >= MI_COUNT) break;

        uint8_t y = (uint8_t)(10 + i * 9);
        char line[15];
        snprintf(line, sizeof(line), "%c%s", (idx == s_cursor) ? '>' : ' ', s_menu_items[idx].name);
        lcd_draw_string(0, y, line);
    }

    lcd_draw_hline(0, 46, 84, true);
    lcd_request_refresh();
}

/* ==================================================================
 *  渲染编辑
 * ================================================================== */
static void render_edit(void)
{
    lcd_clear();
    menu_item_t *item = &s_menu_items[s_cursor];
    uint16_t v = edit_get_value();
    char line[15];

    /* 第 0 行：参数名称 */
    snprintf(line, sizeof(line), "EDIT %s", item->name);
    lcd_draw_string(0, 0, line);

    /* 第 1~3 行：当前值（大字） */
    if (item->divisor > 0) {
        char qbuf[10];
        q10_to_str(v, qbuf);
        lcd_draw_string(6, 12, qbuf);
    } else {
        snprintf(line, sizeof(line), "%u %s", (unsigned)v, item->unit ? item->unit : "");
        lcd_draw_string(6, 12, line);
    }

    /* 第 4~5 行：范围提示 */
    snprintf(line, sizeof(line), "[%u..%u]", (unsigned)item->min, (unsigned)item->max);
    lcd_draw_string(0, 28, line);
    lcd_draw_string(30, 37, "+UP -DN OK=SAVE");

    lcd_draw_hline(0, 46, 84, true);
    lcd_request_refresh();
}

/* ==================================================================
 *  渲染保存确认
 * ================================================================== */
static void render_save_confirm(void)
{
    lcd_clear();
    lcd_draw_string(0, 0, "SAVE TO FLASH?");
    lcd_draw_string(0, 20, s_save_yes ? ">> YES <<" : "   YES");
    lcd_draw_string(0, 30, s_save_yes ? "   NO" : ">> NO <<");
    lcd_draw_hline(0, 46, 84, true);
    lcd_request_refresh();
}

/* ==================================================================
 *  初始化
 * ================================================================== */
void menu_init(void)
{
    s_state = ST_MAIN_SCREEN;
    s_cursor = 0;
    s_idle_ms = 0;
    render_main_screen();
}

/* ==================================================================
 *  1 ms 任务
 * ================================================================== */
void menu_task_1ms(void)
{
    button_event_t ev;
    button_id_t btn = button_get_event(&ev);

    /* 任何按键重置空闲计时 */
    if (btn != BTN_NONE) {
        s_idle_ms = 0;
    } else {
        if (s_idle_ms < IDLE_TIMEOUT_MS) s_idle_ms++;
    }

    /* 故障时强制回主屏 */
    if (g_fault != FAULT_NONE && s_state != ST_MAIN_SCREEN) {
        menu_force_to_main();
        return;
    }

    switch (s_state) {

    /* ====== 主屏 ====== */
    case ST_MAIN_SCREEN:
        /* 短路信号或非待机状态时刷新主屏 */
        if (s_idle_ms == 0 || (s_idle_ms % 500 == 0)) {
            render_main_screen();
        }
        if (btn == BTN_OK && ev == BTN_EVENT_SHORT) {
            s_state = ST_MAIN_MENU;
            s_cursor = 0;
            render_menu();
        }
        break;

    /* ====== 菜单 ====== */
    case ST_MAIN_MENU:
        if (s_idle_ms >= IDLE_TIMEOUT_MS) {
            s_state = ST_MAIN_SCREEN;
            render_main_screen();
            break;
        }

        if (btn == BTN_UP) {
            if (s_cursor > 0) s_cursor--;
            else s_cursor = MI_COUNT - 1;
            render_menu();
        } else if (btn == BTN_DOWN) {
            if (s_cursor < MI_COUNT - 1) s_cursor++;
            else s_cursor = 0;
            render_menu();
        } else if (btn == BTN_OK && ev == BTN_EVENT_SHORT) {
            if (s_cursor == MI_SAVE_FLASH) {
                s_state = ST_SAVE_CONFIRM;
                s_save_yes = false;
                render_save_confirm();
            } else {
                s_state = ST_EDIT_PARAM;
                edit_set_value(*s_menu_items[s_cursor].value);
                render_edit();
            }
        }
        break;

    /* ====== 编辑 ====== */
    case ST_EDIT_PARAM:
        if (s_idle_ms >= IDLE_TIMEOUT_MS) {
            s_state = ST_MAIN_SCREEN;
            render_main_screen();
            break;
        }

        if (btn == BTN_UP && ev != BTN_EVENT_NONE) {
            menu_item_t *item = &s_menu_items[s_cursor];
            uint16_t step = (ev == BTN_EVENT_REPEAT) ? item->step_fast : item->step;
            uint16_t v = edit_get_value();
            if (v + step >= v && v + step <= item->max) v += step;
            else v = item->max;
            edit_set_value(v);
            render_edit();
        } else if (btn == BTN_DOWN && ev != BTN_EVENT_NONE) {
            menu_item_t *item = &s_menu_items[s_cursor];
            uint16_t step = (ev == BTN_EVENT_REPEAT) ? item->step_fast : item->step;
            uint16_t v = edit_get_value();
            if (v >= item->min + step) v -= step;
            else v = item->min;
            edit_set_value(v);
            render_edit();
        } else if (btn == BTN_OK && ev == BTN_EVENT_SHORT) {
            edit_apply();  /* 接受编辑值到 RAM */
            s_state = ST_MAIN_MENU;
            render_menu();
        }
        break;

    /* ====== 保存确认 ====== */
    case ST_SAVE_CONFIRM:
        if (s_idle_ms >= IDLE_TIMEOUT_MS) {
            s_state = ST_MAIN_SCREEN;
            render_main_screen();
            break;
        }

        if (btn == BTN_UP || btn == BTN_DOWN) {
            s_save_yes = !s_save_yes;
            render_save_confirm();
        } else if (btn == BTN_OK && ev == BTN_EVENT_SHORT) {
            if (s_save_yes) {
                param_store_save();  /* 写 Flash */
            }
            s_state = ST_MAIN_SCREEN;
            render_main_screen();
        }
        break;
    }
}

void menu_force_to_main(void)
{
    s_state = ST_MAIN_SCREEN;
    s_cursor = 0;
    s_idle_ms = 0;
    render_main_screen();
}
