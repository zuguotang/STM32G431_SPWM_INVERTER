/*
 * SPWM 调制实现
 * ============
 * 核心算法与 STM8S 版本完全一致：
 *   - 32 位 DDS 相位累加
 *   - 257 点四分之一正弦表 + 象限反射
 *   - 调制幅度千分比 (0..1000)
 *
 * 新增功能（STM32G4 特有）：
 *   - 单极性/双极性模式可选（GPIO 选择）
 *   - 死区时间由 ns 值自动计算 DTG 编码
 *   - HAL 宏操作比较寄存器（__HAL_TIM_SET_COMPARE）
 *
 * 单极性模式：
 *   正弦正半周 → A 桥臂 PWM、B 桥臂固定低
 *   正弦负半周 → B 桥臂 PWM、A 桥臂固定低
 *   每个 MOSFET 仅在半个周期内开关 → 开关损耗减半
 *
 * 双极性模式：
 *   A、B 桥臂始终互补 PWM → 与 STM8S 版本一致
 *   THD 略好但开关损耗翻倍
 */

#include "spwm.h"
#include "board.h"

volatile uint16_t g_spwm_amp = 0;
volatile bool g_spwm_enabled = false;

/* DDS 状态 */
static volatile uint32_t s_phase_acc = 0;         /* 32 位相位累加器 */
static volatile uint32_t s_phase_step = PHASE_STEP_50HZ; /* 当前步进 */
static volatile spwm_mode_t s_mode = SPWM_DEFAULT_MODE;
static volatile bool s_break_pending = false;      /* BKIN 触发标记 */

/*
 * 四分之一正弦表 (0° → 90°)
 * --------------------------
 * 格式：Q1.10 定点数，1000 对应 sin(90°) = 1.0
 * 257 点，共 514 字节 Flash。
 * 与 STM8S 项目使用完全相同的表格数据。
 */
static const uint16_t qsin_0_90[257] = {
    0, 6, 12, 18, 25, 31, 37, 43,
    49, 55, 61, 67, 74, 80, 86, 92,
    98, 104, 110, 116, 122, 128, 135, 141,
    147, 153, 159, 165, 171, 177, 183, 189,
    195, 201, 207, 213, 219, 225, 231, 237,
    243, 249, 255, 261, 267, 273, 279, 284,
    290, 296, 302, 308, 314, 320, 325, 331,
    337, 343, 348, 354, 360, 366, 371, 377,
    383, 388, 394, 400, 405, 411, 416, 422,
    428, 433, 439, 444, 450, 455, 461, 466,
    471, 477, 482, 488, 493, 498, 504, 509,
    514, 519, 525, 530, 535, 540, 545, 550,
    556, 561, 566, 571, 576, 581, 586, 591,
    596, 601, 606, 610, 615, 620, 625, 630,
    634, 639, 644, 649, 653, 658, 662, 667,
    672, 676, 681, 685, 690, 694, 698, 703,
    707, 711, 716, 720, 724, 728, 733, 737,
    741, 745, 749, 753, 757, 761, 765, 769,
    773, 777, 781, 785, 788, 792, 796, 800,
    803, 807, 810, 814, 818, 821, 825, 828,
    831, 835, 838, 842, 845, 848, 851, 855,
    858, 861, 864, 867, 870, 873, 876, 879,
    882, 885, 888, 890, 893, 896, 899, 901,
    904, 907, 909, 912, 914, 917, 919, 922,
    924, 926, 929, 931, 933, 935, 937, 939,
    942, 944, 946, 948, 950, 951, 953, 955,
    957, 959, 960, 962, 964, 965, 967, 969,
    970, 972, 973, 974, 976, 977, 978, 980,
    981, 982, 983, 984, 985, 986, 987, 988,
    989, 990, 991, 992, 992, 993, 994, 995,
    995, 996, 996, 997, 997, 998, 998, 998,
    999, 999, 999, 1000, 1000, 1000, 1000, 1000,
    1000
};

/* ==================================================================
 *  CCR 限幅
 * ================================================================== */
static uint16_t limit_ccr(int32_t value)
{
    /*
     * 将 CCR 值限制在 [PWM_MIN_CCR_TICKS, PERIOD - MIN] 范围内。
     * 防止占空比过于接近 0% 或 100%：
     *   - 过于接近 0%：MOSFET 关断后自举电容无法充电
     *   - 过于接近 100%：高侧 MOSFET 持续导通，自举电容放电殆尽
     *
     * MIN_CCR = 24 ticks @ 170 MHz ≈ 141 ns
     */
    if (value < (int32_t)PWM_MIN_CCR_TICKS) {
        return PWM_MIN_CCR_TICKS;
    }
    if (value > ((int32_t)PWM_PERIOD_TICKS - (int32_t)PWM_MIN_CCR_TICKS)) {
        return (uint16_t)(PWM_PERIOD_TICKS - PWM_MIN_CCR_TICKS);
    }
    return (uint16_t)value;
}

/* ==================================================================
 *  DDS 正弦计算（线性插值版）
 *
 *  相比于原 STM8S 的直接查表（257 点, 0.35°/step），
 *  增加 8 位小数线性插值后等效分辨率 ≈ 0.0014°，THD 显著改善。
 *
 *  位域分配（32 位相位累加器）：
 *     bit31..30 → 象限 (0=I, 1=II, 2=III, 3=IV)
 *     bit29..22 → 表索引 (0..255, 对应 0°→~90°)
 *     bit21..14 → 插值小数 (0..255, 表示表内亚像素位置)
 *     bit13..0  → 未使用（更高精度被 PWM 分辨率截断）
 *
 *  插值公式（8 位小数，256 级）：
 *     value = table[idx] + (table[idx+1] - table[idx]) × frac / 256
 *
 *  象限处理：
 *     I/III  → 正向查表（sin 上升）
 *     II/IV → 反向查表（sin 下降），ridx = 256 - idx
 *     正向结果取正，反向结果取负（III/IV）
 *
 *  返回值范围：-1000 .. +1000（Q1.10 千分比调制幅度）
 * ================================================================== */
static int16_t sine_from_phase(uint32_t phase)
{
    uint8_t idx  = (uint8_t)((phase >> 22) & 0xFFU);   /* 表索引 0..255 */
    uint8_t frac = (uint8_t)((phase >> 14) & 0xFFU);   /* 插值小数 0..255 */
    uint8_t quad = (uint8_t)(phase >> 30);              /* 象限 0..3 */
    uint16_t y0, y1;
    int32_t value;

    if ((quad & 1U) == 0U) {
        /* 正向查表（象限 I, III）：sin 上升 */
        y0 = qsin_0_90[idx];
        y1 = qsin_0_90[idx + 1U];                      /* idx<256，安全 */
        value = (int32_t)y0 + (((int32_t)(y1 - y0) * (int32_t)frac) >> 8);
    } else {
        /* 反向查表（象限 II, IV）：sin 下降 */
        uint8_t ridx = (uint8_t)(256U - idx);
        y0 = qsin_0_90[ridx];
        y1 = qsin_0_90[ridx - 1U];                     /* 反向插值 */
        value = (int32_t)y0 + (((int32_t)(y1 - y0) * (int32_t)frac) >> 8);
    }

    return (quad < 2U) ? (int16_t)value : (int16_t)(-value);
}

/* ==================================================================
 *  死区时间 ns → DTG 编码转换
 * ==================================================================
 *
 * STM32G4 TIM1 BDTR.DTG 的编码规则：
 *   DTG[7:5]=000: dt = DTG[7:0] × tDTS           (0..127 × tDTS)
 *   DTG[7:5]=001: dt = (64+DTG[5:0]) × 2 × tDTS   (128..254 × tDTS)
 *   DTG[7:5]=010: dt = (32+DTG[4:0]) × 8 × tDTS   (256..504 × tDTS)
 *   DTG[7:5]=011: dt = (32+DTG[4:0]) × 16 × tDTS  (512..1008 × tDTS)
 *
 * 其中 tDTS = 1 / TIM1_CLK = 1 / 170 MHz ≈ 5.88 ns。
 *
 * 算法：计算所需 ticks = ceil(ns / tDTS)，
 * 然后选择覆盖该 ticks 的最优编码（精度最高的一段）。
 */
static uint32_t deadtime_ticks_from_ns(uint32_t ns)
{
    /*
     * 计算所需的 ticks 数（向上取整）
     * = (170M × ns + 10^9 - 1) / 10^9
     */
    uint64_t ticks = ((uint64_t)PWM_TIMER_CLOCK_HZ * (uint64_t)ns + 999999999ULL) / 1000000000ULL;

    /* 第 0 段：0..127 ticks，步进 1 tick ≈ 5.88 ns */
    if (ticks <= 127ULL) {
        return (uint32_t)ticks;
    }
    /* 第 1 段：128..254 ticks，步进 2 tick ≈ 11.76 ns */
    if (ticks <= (64ULL + 63ULL) * 2ULL) {
        return 0x80UL | (uint32_t)((ticks / 2ULL) - 64ULL);
    }
    /* 第 2 段：256..504 ticks，步进 8 tick ≈ 47.06 ns */
    if (ticks <= (32ULL + 31ULL) * 8ULL) {
        return 0xC0UL | (uint32_t)((ticks / 8ULL) - 32ULL);
    }
    /* 第 3 段：512..1008 ticks，步进 16 tick ≈ 94.12 ns */
    if (ticks <= (32ULL + 31ULL) * 16ULL) {
        return 0xE0UL | (uint32_t)((ticks / 16ULL) - 32ULL);
    }
    /* 超过最大死区时间 ≈ 5.93 us → 返回最大编码 */
    return 0xFFUL;
}

/* ==================================================================
 *  运行时初始化
 * ================================================================== */
void spwm_init_runtime(void)
{
    g_spwm_amp = 0;
    g_spwm_enabled = false;
    s_phase_acc = 0;
    s_phase_step = (SPWM_DEFAULT_FREQ == AC_FREQ_60HZ) ? PHASE_STEP_60HZ : PHASE_STEP_50HZ;
    s_mode = SPWM_DEFAULT_MODE;
    s_break_pending = false;

    /* 将死区 ns 值转换为 BDTR.DTG 编码并写入硬件 */
    spwm_set_deadtime_ns(SPWM_DEADTIME_NS);
}

/* ==================================================================
 *  PWM 输出开关
 * ================================================================== */
void spwm_outputs_on(void)
{
    __HAL_TIM_CLEAR_FLAG(&htim1, TIM_FLAG_BREAK | TIM_FLAG_UPDATE);
    __HAL_TIM_MOE_ENABLE(&htim1);    /* 主输出使能 */
    g_spwm_enabled = true;
}

void spwm_outputs_off(void)
{
    g_spwm_enabled = false;
    g_spwm_amp = 0;
    __HAL_TIM_MOE_DISABLE(&htim1);   /* 主输出禁止 → 输出强制空闲状态 */
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, PWM_PERIOD_TICKS / 2U);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, PWM_PERIOD_TICKS / 2U);
    board_relay_set(false);
}

/* ==================================================================
 *  频率 / 模式 / 死区设置
 * ================================================================== */
void spwm_set_frequency(ac_freq_t freq)
{
    /* 更新 DDS 步进值，累加器无需复位——频率平滑过渡 */
    s_phase_step = (freq == AC_FREQ_60HZ) ? PHASE_STEP_60HZ : PHASE_STEP_50HZ;
}

void spwm_set_mode(spwm_mode_t mode)
{
    s_mode = mode;
}

void spwm_set_deadtime_ns(uint32_t ns)
{
    /* 计算 DTG 编码并写入 BDTR 寄存器 */
    MODIFY_REG(htim1.Instance->BDTR, TIM_BDTR_DTG, deadtime_ticks_from_ns(ns));
}

/* ==================================================================
 *  TIM1 更新中断 (22 kHz) —— SPWM 核心计算
 * ================================================================== */
void spwm_tim_update_isr(void)
{
    int16_t sine;
    int32_t center = (int32_t)PWM_PERIOD_TICKS / 2L;  /* 50% 占空比中心值 */
    int32_t delta;
    uint16_t duty_a;
    uint16_t duty_b;
    uint16_t amp;

    if (!g_spwm_enabled) {
        return;
    }

    /* DDS 累加 */
    s_phase_acc += s_phase_step;

    /* 查表得当前正弦值 (-1000 .. +1000) */
    sine = sine_from_phase(s_phase_acc);

    /* 读取调制幅度快照（主循环写入，ISR 读取） */
    amp = g_spwm_amp;

    if (s_mode == SPWM_MODE_BIPOLAR) {
        /*
         * 双极性调制
         * ---------
         * CH1 = 中心 + delta  (正半周)
         * CH2 = 中心 - delta  (负半周，互补)
         *
         * delta = sin × amp / 1000 × center / 1000 × center ?
         *
         * 实际上：
         * delta = sin × amp × center / 1,000,000
         *
         * 因为 sin 和 amp 都是千分比（÷1000），
         * 所以总共需要除以 1,000,000。
         */
        delta = ((int32_t)sine * (int32_t)amp * center) / 1000000L;
        duty_a = limit_ccr(center + delta);
        duty_b = limit_ccr(center - delta);
    } else {
        /*
         * 单极性调制
         * ---------
         * 仅正半周的桥臂开关，另一半桥臂固定为中心值（50%）。
         *
         * sin >= 0 时：A 桥臂 PWM，B 桥臂固定 50%
         * sin < 0 时：A 桥臂固定 50%，B 桥臂 PWM
         *
         * unipolar = sin × amp × PERIOD / 2,000,000
         *   （因子 2,000,000 因为仅半个周期调制）
         */
        int32_t unipolar = ((int32_t)sine * (int32_t)amp * (int32_t)PWM_PERIOD_TICKS) / 2000000L;
        if (sine >= 0) {
            duty_a = limit_ccr(center + unipolar);
            duty_b = limit_ccr(center);
        } else {
            duty_a = limit_ccr(center);
            duty_b = limit_ccr(center - unipolar);
        }
    }

    /* 更新比较寄存器（硬件在下一个更新事件生效） */
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, duty_a);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, duty_b);
}

/* ==================================================================
 *  TIM1 刹车中断
 * ================================================================== */
void spwm_break_isr(void)
{
    /*
     * BKIN 触发时 MOE 已被硬件自动清零（PWM 立即关闭），
     * 这里设置软件标志，让主循环的 1 ms 任务处理重试逻辑。
     */
    s_break_pending = true;
    g_spwm_enabled = false;
    g_spwm_amp = 0;
    board_relay_set(false);
}

bool spwm_break_pending(void)
{
    return s_break_pending;
}

void spwm_clear_break_pending(void)
{
    s_break_pending = false;
    __HAL_TIM_CLEAR_FLAG(&htim1, TIM_FLAG_BREAK);  /* 清除 BKIN 硬件标志 */
}
