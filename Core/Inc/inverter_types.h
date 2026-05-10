#ifndef INVERTER_TYPES_H
#define INVERTER_TYPES_H

/*
 * 公共类型定义
 * -----------
 * 使用 stdint.h 和 stdbool.h 替代自定义类型（如 STM8S 项目中的 u8/u16），
 * 这是 STM32 HAL 生态的标准做法：
 *   - uint8_t / uint16_t / uint32_t / int16_t / int32_t  明确位宽
 *   - bool / true / false                                  布尔语义清晰
 */

#include <stdint.h>
#include <stdbool.h>

/*
 * 故障码枚举
 * ---------
 * 每个故障对应一种保护触发源。
 * FAULT_NONE 表示正常运行。
 * 故障响应：关闭 PWM → 断开继电器 → LED 闪烁故障码 → 串口打印。
 *
 * 恢复策略因故障类型而异：
 *   - SHORT：最多重试 3 次，之后永久锁定
 *   - 其他：等待条件恢复后自动清除
 */
typedef enum {
    FAULT_NONE = 0,       /* 无故障，正常运行 */
    FAULT_SHORT = 1,      /* 短路：硬件 BKIN 或软件 ADC 阈值触发 */
    FAULT_OVERLOAD = 2,   /* 过载：输出电流持续超过阈值 */
    FAULT_OVER_TEMP = 3,  /* 过温：NTC 采样超过 TEMP_ADC_OVER */
    FAULT_UNDER_BUS = 4,  /* 母线欠压：低于 VBUS_ADC_MIN_RUN */
    FAULT_OUTPUT_LOW = 5, /* 输出欠压：启动后 VOUT 低于锁定阈值 */
    FAULT_OVER_BUS = 6    /* 母线过压：高于 VBUS_ADC_MAX_RUN（新增，STM8S 版本无） */
} fault_code_t;

/*
 * SPWM 调制模式
 * ------------
 * UNIPOLAR（单极性）：每个半桥仅在半个正弦周期内开关，
 *   开关损耗减半，LC 滤波纹波更小，适合大多数应用。
 *
 * BIPOLAR（双极性）：两个半桥始终互补开关，
 *   输出 THD 理论上更好，但开关损耗加倍，MOSFET 温升更高。
 */
typedef enum {
    SPWM_MODE_UNIPOLAR = 0,
    SPWM_MODE_BIPOLAR = 1
} spwm_mode_t;

/*
 * 交流输出频率
 * ----------
 * 50 Hz 或 60 Hz，由 GPIO 输入选择。
 */
typedef enum {
    AC_FREQ_50HZ = 50,
    AC_FREQ_60HZ = 60
} ac_freq_t;

#endif
