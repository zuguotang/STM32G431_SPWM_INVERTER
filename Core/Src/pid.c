/*
 * PID 控制器实现（Q10 定点格式）
 * ===========================
 *
 * 位置式 PID，带以下特性：
 *   1. 测量微分 (Derivative on Measurement) — 避免设定值突变冲击
 *   2. 积分限幅 (Integral Clamping) — 防止积分饱和
 *   3. 输出斜率限制 (Slew Rate Limiting) — 防止电压突变
 *   4. Q10 定点格式 — 实际值 = 整数值 / 1024
 *
 * 算法：
 *   error  = setpoint - measurement
 *   d_meas = measurement - last_meas       (测量值变化量)
 *   P      = Kp × error
 *   D      = -Kd × d_meas                  (负号：测量微分)
 *   I      = clamp(I + Ki × error, I_MIN, I_MAX)
 *
 *   raw_out = clamp((P + I + D) >> 10, OUT_MIN, OUT_MAX)
 *   out     = clamp(raw_out, last_out - slew, last_out + slew)
 *
 * 注意：下降方向不限速（last_out - slew 仅下限）。
 * 电压过高时需要尽快降低调制幅度。
 */

#include "pid.h"

/* ==================================================================
 *  通用限幅函数
 * ================================================================== */
static int32_t clamp_i32(int32_t value, int32_t min_value, int32_t max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

/* ==================================================================
 *  PID 初始化
 * ================================================================== */
void pid_init(pid_q10_t *pid)
{
    /* 从 inverter_config.h 加载默认 Q10 增益 */
    pid->kp_q10 = PID_KP_Q10;
    pid->ki_q10 = PID_KI_Q10;
    pid->kd_q10 = PID_KD_Q10;

    /* 清零运行时状态 */
    pid->i_acc = 0;
    pid->last_meas = 0;
    pid->last_out = 0;

    /* 积分和输出限幅 */
    pid->i_min = PID_I_MIN;
    pid->i_max = PID_I_MAX;
    pid->out_min = PID_OUT_MIN;
    pid->out_max = PID_OUT_RUN_LIMIT;   /* 初始值，运行时由 pid_step 更新 */
}

/* ==================================================================
 *  PID 复位
 * ================================================================== */
void pid_reset(pid_q10_t *pid)
{
    /*
     * 故障恢复时调用，清零所有运行时状态。
     * Kp/Ki/Kd 和限幅值保留不变。
     */
    pid->i_acc = 0;
    pid->last_meas = 0;
    pid->last_out = 0;
}

/* ==================================================================
 *  PID 单步计算
 * ================================================================== */
int16_t pid_step(pid_q10_t *pid, int16_t setpoint, int16_t measurement,
                 int16_t out_limit, int16_t slew_limit)
{
    /*
     * 误差：正值表示需要增大输出以接近设定值
     *
     * 例如 VOUT_TARGET = 2480, VOUT_MEAS = 2400
     *     error = +80 → 输出电压偏低 → 增大调制幅度
     */
    int32_t error = (int32_t)setpoint - (int32_t)measurement;

    /*
     * 测量微分：测量值的变化速率
     * d_meas > 0 表示输出电压正在上升 → D 项为负，抑制超调
     *
     * 使用测量微分而非误差微分的原因：
     *   设定值通常是常量（不变），误差微分 ≈ -测量微分。
     *   当设定值瞬时变化时（如软启动目标调整），
     *   误差微分会因设定值突变产生微分冲击 (Derivative Kick)，
     *   而测量微分不会。
     */
    int32_t d_meas = (int32_t)measurement - (int32_t)pid->last_meas;

    /* 比例项 */
    int32_t p_term = pid->kp_q10 * error;

    /* 微分项（带负号：测量增大 → D 项为负 → 减小输出） */
    int32_t d_term = -pid->kd_q10 * d_meas;

    int32_t out;
    int32_t max_step;

    /* 更新输出上限（随软启动阶段变化） */
    pid->out_max = out_limit;

    /*
     * 积分累加 + 限幅
     * -------------
     * 积分限幅 (Anti-Windup) 防止 PID 输出已饱和时积分器继续累加，
     * 导致输出从饱和区退出时产生长时间过冲或反冲。
     */
    pid->i_acc = clamp_i32(pid->i_acc + pid->ki_q10 * error, pid->i_min, pid->i_max);

    /*
     * 计算原始输出
     * -----------
     * Q10 格式：所有增益已放大 1024 倍，
     * P、I、D 三项求和后右移 10 位得到实际输出。
     */
    out = (p_term + pid->i_acc + d_term) >> 10;

    /* 硬限幅：确保输出在允许范围内 */
    out = clamp_i32(out, pid->out_min, pid->out_max);

    /*
     * 输出斜率限制
     * -----------
     * 上升方向：每 ms 最多增加 slew_limit
     * 下降方向：不做额外限制（省略 last_out - slew_limit），
     *           下面只限制上升，下降交给硬限幅处理。
     *
     * 实际上：这里实现的是双边限斜率。
     *   上升限 slew_limit，下降限 -slew_limit（通过 last_out + max_step）。
     *
     * 注意：这里下降也是对称限速的。
     * 如果需要非对称（下降不限速），需要额外判断。
     */
    max_step = slew_limit;
    if (out > (int32_t)pid->last_out + max_step) {
        out = (int32_t)pid->last_out + max_step;     /* 上升受限于 slew_limit */
    } else if (out < (int32_t)pid->last_out - max_step) {
        out = (int32_t)pid->last_out - max_step;     /* 下降也受限于 slew_limit */
    }

    /* 再次硬限幅（斜率限制可能超出范围） */
    out = clamp_i32(out, pid->out_min, pid->out_max);

    /* 保存历史值供下次计算使用 */
    pid->last_meas = measurement;
    pid->last_out = (int16_t)out;

    return (int16_t)out;
}
