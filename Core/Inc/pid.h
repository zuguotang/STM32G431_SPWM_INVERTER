#ifndef PID_H
#define PID_H

/*
 * PID 控制器（Q10 定点格式）
 * -------------------------
 *
 * Q10 格式：实际值 = 整数值 / 1024 (右移 10 位)。
 * 使用定点而非浮点的原因：
 *   1. STM32G4 有 FPU，但定点运算仍有确定性延迟优势
 *   2. 避免浮点→整数转换损失
 *   3. 与 STM8S 版本保持概念一致（虽然 STM32 可以用 float）
 *
 * 算法：位置式 PID（测量微分 + 积分限幅 + 输出斜率限制）
 *
 *   误差  = 设定值 - 测量值
 *   P 项  = Kp × 误差
 *   D 项  = -Kd × (测量值 - 上次测量值)     ← 测量微分，避免设定值冲击
 *   I 项  = clamp(I + Ki × 误差, I_MIN, I_MAX)  ← 积分限幅防饱和
 *
 *   PID 输出 = clamp((P + I + D) >> 10, OUT_MIN, OUT_MAX)
 *   最终输出 = PID 输出（经斜率限制）
 *
 *   使用示例:
 *     pid_q10_t pid;
 *     pid_init(&pid);
 *     int16_t amp = pid_step(&pid, target_adc, meas_adc, AMP_LIMIT, SLEW_LIMIT);
 */

#include "main.h"

/*
 * PID 状态结构体
 * ------------
 * 所有状态集中在结构体中，便于多个 PID 实例共存
 * （当前项目只用一个电压环，但架构预留多环扩展空间）。
 */
typedef struct {
    int32_t kp_q10;      /* 比例系数 (Q10) */
    int32_t ki_q10;      /* 积分系数 (Q10) */
    int32_t kd_q10;      /* 微分系数 (Q10) */
    int32_t i_acc;       /* 积分累加器 */
    int32_t i_min;       /* 积分下限 */
    int32_t i_max;       /* 积分上限 */
    int16_t out_min;     /* 输出下限 */
    int16_t out_max;     /* 输出上限 */
    int16_t last_meas;   /* 上次测量值（用于微分计算） */
    int16_t last_out;    /* 上次输出值（用于斜率限制） */
} pid_q10_t;

/* 初始化 PID 结构体：从 inverter_config.h 加载默认参数 */
void pid_init(pid_q10_t *pid);

/* 复位 PID 状态：清零积分累加器和历史值（用于故障恢复后重新启动） */
void pid_reset(pid_q10_t *pid);

/*
 * 执行一次 PID 计算
 * ---------------
 * 参数：
 *   pid        : PID 状态指针
 *   setpoint   : 目标值（ADC 码值）
 *   measurement: 当前测量值（ADC 码值）
 *   out_limit  : 输出上限（同时更新 pid->out_max）
 *   slew_limit : 输出每步最大增量（下降不限速）
 *
 * 返回：PID 输出值（0..1000 千分比调制幅度）
 */
int16_t pid_step(pid_q10_t *pid, int16_t setpoint, int16_t measurement,
                 int16_t out_limit, int16_t slew_limit);

#endif
