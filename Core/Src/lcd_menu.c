/*
 * SSD1306 OLED 菜单系统 (128×64)
 * ==============================
 * 主屏 6 行 + 菜单 5 行，充分利用 128×64 屏幕。
 */

#include "lcd_menu.h"
#include "ssd1306.h"
#include "button.h"
#include "param_store.h"
#include "inverter_config.h"
#include "adc_driver.h"
#include "spwm.h"
#include "protection.h"
#include "board.h"

typedef enum {
    ST_MAIN_SCREEN = 0,
    ST_MAIN_MENU,
    ST_EDIT_PARAM,
    ST_SAVE_CONFIRM
} menu_state_t;

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

typedef struct {
    const char *name;
    const char *unit;
    uint16_t   *value;
    uint16_t   min, max, step, step_fast;
    uint8_t    divisor;          /* 0=整数, 非0=Q10 */
} menu_item_t;

static menu_item_t s_menu_items[MI_COUNT] = {
    [MI_DEADTIME]       = {"Dead Time",   "ns", &g_params.deadtime_ns,      500, 5000,  50, 200, 0 },
    [MI_SOFTSTART]      = {"Soft Start",  "s",  &g_params.softstart_ms,     1000,30000, 100,1000,0 },
    [MI_VOUT_TARGET]    = {"Vout",        "V",  &g_params.vout_target_v,    100, 260,   1,  5,   0 },
    [MI_PID_KP]         = {"PID Kp",      "",   &g_params.pid_kp_q10,       0,   1023,  5,  20,  10},
    [MI_PID_KI]         = {"PID Ki",      "",   &g_params.pid_ki_q10,       0,   255,   1,  5,   10},
    [MI_OVERLOAD_TRIP]  = {"Ovl Trip",    "ms", &g_params.overload_trip_ms, 100, 10000, 100,500,0 },
    [MI_PID_START_LIMIT]= {"St Limit",    "",   &g_params.pid_start_limit,  100, 930,   10, 50,  0 },
    [MI_SAVE_FLASH]     = {"* SAVE FLASH *",NULL,NULL,0,0,0,0,0},
};

static menu_state_t s_state = ST_MAIN_SCREEN;
static uint8_t s_cursor = 0;
static uint16_t s_edit_val = 0;
static bool s_save_yes = false;
static uint16_t s_idle_ms = 0;
#define IDLE_TIMEOUT_MS 10000

/* ==================================================================
 *  Q10 浮点显示
 * ================================================================== */
static void q10_to_str(uint16_t q10, char *buf)
{
    uint16_t int_part = q10 / 1024;
    uint16_t frac3 = (uint16_t)((uint32_t)(q10 % 1024) * 1000 / 1024);
    uint8_t pos = 0;
    if (int_part >= 10) buf[pos++] = (char)('0' + int_part / 10);
    buf[pos++] = (char)('0' + int_part % 10);
    buf[pos++] = '.';
    buf[pos++] = (char)('0' + frac3 / 100);
    buf[pos++] = (char)('0' + (frac3 / 10) % 10);
    buf[pos++] = (char)('0' + frac3 % 10);
    buf[pos] = '\0';
}

/* ==================================================================
 *  主屏渲染 (128×64, 21 字符 × 8 行可用)
 * ================================================================== */
static void render_main_screen(void)
{
    ssd1306_clear();
    char line[22];

    /* 标题栏 + 反色背景 */
    ssd1306_fill_rect(0, 0, 128, 10, true);
    ssd1306_draw_string(18, 2, "INVERTER V2.0");

    /* 行 1: RMS 电压 + 电流 */
    uint16_t volt_disp = g_adc.rms_volt / 100;  /* 220.00V → 220 */
    uint16_t curr_disp = g_adc.rms_curr;        /* ×100, 如 500 = 5.00A */
    snprintf(line, sizeof(line), "Vo:%3uV Io:%u.%02uA %s",
             (unsigned)volt_disp,
             (unsigned)(curr_disp / 100), (unsigned)(curr_disp % 100),
             g_spwm_enabled ? "ON" : "OFF");
    ssd1306_draw_string(0, 13, line);

    /* 行 2: 功率 + 频率/模式 */
    snprintf(line, sizeof(line), "P:%3uW %s %s",
             (unsigned)g_adc.power,
             board_frequency_is_60hz() ? "60Hz" : "50Hz",
             board_read_spwm_mode() == SPWM_MODE_BIPOLAR ? "BI" : "UNI");
    ssd1306_draw_string(0, 22, line);

    /* 行 3: 母线 + 温度（℃） */
    int16_t temp_disp = g_adc.temp_celsius;  /* ×10, 如 255 = 25.5℃ */
    snprintf(line, sizeof(line), "Vbus:%04u T:%c%d.%dC",
             (unsigned)g_adc.vbus,
             (temp_disp < 0) ? '-' : ' ',
             (unsigned)(temp_disp >= 0 ? temp_disp / 10 : (-temp_disp) / 10),
             (unsigned)(temp_disp >= 0 ? temp_disp % 10 : (-temp_disp) % 10));
    ssd1306_draw_string(0, 31, line);

    /* 行 4: 调制幅度 */
    snprintf(line, sizeof(line), "Amp: %03u/1000", (unsigned)g_spwm_amp);
    ssd1306_draw_string(0, 40, line);

    /* 行 5: 故障状态或正常 */
    if (g_fault != FAULT_NONE) {
        snprintf(line, sizeof(line), "FAULT: %u", (unsigned)g_fault);
    } else {
        snprintf(line, sizeof(line), "STATUS: OK");
    }
    ssd1306_draw_string(0, 49, line);

    /* 底部分隔线 */
    ssd1306_draw_hline(0, 57, 128, true);
    ssd1306_draw_string(0, 58, "OK=Menu");

    ssd1306_request_refresh();
}

/* ==================================================================
 *  菜单渲染（5 项可见）
 * ================================================================== */
static void render_menu(void)
{
    ssd1306_clear();
    ssd1306_draw_string(0, 0, "=== MENU ===");

    uint8_t scroll = (s_cursor >= 5) ? (uint8_t)(s_cursor - 4) : 0;

    for (uint8_t i = 0; i < 5; i++) {
        uint8_t idx = (uint8_t)(scroll + i);
        if (idx >= MI_COUNT) break;

        uint8_t y = (uint8_t)(10 + i * 10);
        char line[22];
        snprintf(line, sizeof(line), "%c %s",
                 (idx == s_cursor) ? '>' : ' ', s_menu_items[idx].name);
        ssd1306_draw_string(0, y, line);
    }

    ssd1306_draw_hline(0, 60, 128, true);
    ssd1306_draw_string(0, 61, "UP/DN  OK=Enter");
    ssd1306_request_refresh();
}

/* ==================================================================
 *  编辑渲染
 * ================================================================== */
static void render_edit(void)
{
    ssd1306_clear();
    menu_item_t *item = &s_menu_items[s_cursor];
    char line[22];

    snprintf(line, sizeof(line), "EDIT: %s", item->name);
    ssd1306_draw_string(0, 0, line);

    /* 当前值（大字号位置） */
    if (item->divisor > 0) {
        char qbuf[10];
        q10_to_str(s_edit_val, qbuf);
        ssd1306_draw_string(10, 16, qbuf);
    } else {
        snprintf(line, sizeof(line), "%u %s", (unsigned)s_edit_val,
                 item->unit ? item->unit : "");
        ssd1306_draw_string(10, 16, line);
    }

    /* 范围 */
    snprintf(line, sizeof(line), "Range: %u - %u", (unsigned)item->min, (unsigned)item->max);
    ssd1306_draw_string(0, 32, line);

    ssd1306_draw_hline(0, 46, 128, true);
    ssd1306_draw_string(0, 48, "+UP  -DN  OK=Save");
    ssd1306_draw_string(0, 57, "Hold for fast adj.");
    ssd1306_request_refresh();
}

/* ==================================================================
 *  保存确认
 * ================================================================== */
static void render_save_confirm(void)
{
    ssd1306_clear();
    ssd1306_draw_string(0, 10, "SAVE TO FLASH?");
    ssd1306_draw_string(10, 28, s_save_yes ? ">> YES <<" : "   YES");
    ssd1306_draw_string(10, 38, s_save_yes ? "   NO" : ">> NO <<");
    ssd1306_draw_string(0, 55, "OK=Confirm");
    ssd1306_request_refresh();
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
 *  1ms 任务
 * ================================================================== */
void menu_task_1ms(void)
{
    button_event_t ev;
    button_id_t btn = button_get_event(&ev);

    if (btn != BTN_NONE) s_idle_ms = 0;
    else if (s_idle_ms < IDLE_TIMEOUT_MS) s_idle_ms++;

    /* 故障时强制回主屏 */
    if (g_fault != FAULT_NONE && s_state != ST_MAIN_SCREEN) {
        menu_force_to_main();
        return;
    }

    switch (s_state) {

    case ST_MAIN_SCREEN:
        if (s_idle_ms == 0 || (s_idle_ms % 500 == 0)) {
            render_main_screen();
        }
        if (btn == BTN_OK && ev == BTN_EVENT_SHORT) {
            s_state = ST_MAIN_MENU;
            s_cursor = 0;
            render_menu();
        }
        break;

    case ST_MAIN_MENU:
        if (s_idle_ms >= IDLE_TIMEOUT_MS) {
            s_state = ST_MAIN_SCREEN;
            render_main_screen();
            break;
        }
        if (btn == BTN_UP) {
            s_cursor = (s_cursor > 0) ? (uint8_t)(s_cursor - 1) : (uint8_t)(MI_COUNT - 1);
            render_menu();
        } else if (btn == BTN_DOWN) {
            s_cursor = (s_cursor < MI_COUNT - 1) ? (uint8_t)(s_cursor + 1) : 0;
            render_menu();
        } else if (btn == BTN_OK && ev == BTN_EVENT_SHORT) {
            if (s_cursor == MI_SAVE_FLASH) {
                s_state = ST_SAVE_CONFIRM;
                s_save_yes = false;
                render_save_confirm();
            } else {
                s_state = ST_EDIT_PARAM;
                s_edit_val = *s_menu_items[s_cursor].value;
                render_edit();
            }
        }
        break;

    case ST_EDIT_PARAM:
        if (s_idle_ms >= IDLE_TIMEOUT_MS) {
            s_state = ST_MAIN_SCREEN;
            render_main_screen();
            break;
        }
        if (btn == BTN_UP && ev != BTN_EVENT_NONE) {
            menu_item_t *item = &s_menu_items[s_cursor];
            uint16_t step = (ev == BTN_EVENT_REPEAT) ? item->step_fast : item->step;
            if (s_edit_val + step >= s_edit_val && s_edit_val + step <= item->max)
                s_edit_val += step;
            else s_edit_val = item->max;
            render_edit();
        } else if (btn == BTN_DOWN && ev != BTN_EVENT_NONE) {
            menu_item_t *item = &s_menu_items[s_cursor];
            uint16_t step = (ev == BTN_EVENT_REPEAT) ? item->step_fast : item->step;
            if (s_edit_val >= item->min + step) s_edit_val -= step;
            else s_edit_val = item->min;
            render_edit();
        } else if (btn == BTN_OK && ev == BTN_EVENT_SHORT) {
            *s_menu_items[s_cursor].value = s_edit_val;
            s_state = ST_MAIN_MENU;
            render_menu();
        }
        break;

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
            if (s_save_yes) param_store_save();
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
