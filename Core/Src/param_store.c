/*
 * Flash 参数存储实现
 * =================
 *
 * STM32G4 Flash 操作注意事项（RM0440 §3.3.6）：
 *   - 擦除/写入期间禁止读取 Flash（指令取指暂停）
 *   - 必须关全局中断
 *   - 页擦除最大 22ms，64 位双字写入约 90us
 *   - 总耗时 ≈ 22ms + 128×90us ≈ 34ms
 *
 * CRC16/XMODEM (x^16 + x^12 + x^5 + 1)
 *   与 CCITT 相同，广泛用于固件参数校验。
 */

#include "param_store.h"
#include <stddef.h>
#include <string.h>
#include "stm32g4xx_hal.h"
#include "debug_uart.h"

/* 编译期默认值 */
#define DFLT_DEADTIME_NS        1000
#define DFLT_SOFTSTART_MS       5000
/* DFLT_VOUT_TARGET_ADC removed -- now stored as actual volts in g_params.vout_target_v */
#define DFLT_PID_KP_Q10         260
#define DFLT_PID_KI_Q10         10
#define DFLT_OVERLOAD_TRIP_MS   3000
#define DFLT_PID_START_LIMIT    500

param_blob_t g_params;

/* ==================================================================
 *  CRC16/XMODEM 计算
 * ================================================================== */
uint16_t param_store_crc16(const param_blob_t *blob)
{
    /*
     * 计算范围：从 version 字段开始，到 padding 结束。
     * 不包含 magic 和 crc16 本身。
     */
    static const uint16_t crc16_table[256] = {
        0x0000,0x1021,0x2042,0x3063,0x4084,0x50A5,0x60C6,0x70E7,
        0x8108,0x9129,0xA14A,0xB16B,0xC18C,0xD1AD,0xE1CE,0xF1EF,
        0x1231,0x0210,0x3273,0x2252,0x52B5,0x4294,0x72F7,0x62D6,
        0x9339,0x8318,0xB37B,0xA35A,0xD3BD,0xC39C,0xF3FF,0xE3DE,
        0x2462,0x3443,0x0420,0x1401,0x64E6,0x74C7,0x44A4,0x5485,
        0xA56A,0xB54B,0x8528,0x9509,0xE5EE,0xF5CF,0xC5AC,0xD58D,
        0x3653,0x2672,0x1611,0x0630,0x76D7,0x66F6,0x5695,0x46B4,
        0xB75B,0xA77A,0x9719,0x8738,0xF7DF,0xE7FE,0xD79D,0xC7BC,
        0x48C4,0x58E5,0x6886,0x78A7,0x0840,0x1861,0x2802,0x3823,
        0xC9CC,0xD9ED,0xE98E,0xF9AF,0x8948,0x9969,0xA90A,0xB92B,
        0x5AF5,0x4AD4,0x7AB7,0x6A96,0x1A71,0x0A50,0x3A33,0x2A12,
        0xDBFD,0xCBDC,0xFBBF,0xEB9E,0x9B79,0x8B58,0xBB3B,0xAB1A,
        0x6CA6,0x7C87,0x4CE4,0x5CC5,0x2C22,0x3C03,0x0C60,0x1C41,
        0xEDAE,0xFD8F,0xCDEC,0xDDCD,0xAD2A,0xBD0B,0x8D68,0x9D49,
        0x7E97,0x6EB6,0x5ED5,0x4EF4,0x3E13,0x2E32,0x1E51,0x0E70,
        0xFF9F,0xEFBE,0xDFDD,0xCFFC,0xBF1B,0xAF3A,0x9F59,0x8F78,
        0x9188,0x81A9,0xB1CA,0xA1EB,0xD10C,0xC12D,0xF14E,0xE16F,
        0x1080,0x00A1,0x30C2,0x20E3,0x5004,0x4025,0x7046,0x6067,
        0x83B9,0x9398,0xA3FB,0xB3DA,0xC33D,0xD31C,0xE37F,0xF35E,
        0x02B1,0x1290,0x22F3,0x32D2,0x4235,0x5214,0x6277,0x7256,
        0xB5EA,0xA5CB,0x95A8,0x8589,0xF56E,0xE54F,0xD52C,0xC50D,
        0x34E2,0x24C3,0x14A0,0x0481,0x7466,0x6447,0x5424,0x4405,
        0xA7DB,0xB7FA,0x8799,0x97B8,0xE75F,0xF77E,0xC71D,0xD73C,
        0x26D3,0x36F2,0x0691,0x16B0,0x6657,0x7676,0x4615,0x5634,
        0xD94C,0xC96D,0xF90E,0xE92F,0x99C8,0x89E9,0xB98A,0xA9AB,
        0x5844,0x4865,0x7806,0x6827,0x18C0,0x08E1,0x3882,0x28A3,
        0xCB7D,0xDB5C,0xEB3F,0xFB1E,0x8BF9,0x9BD8,0xABBB,0xBB9A,
        0x4A75,0x5A54,0x6A37,0x7A16,0x0AF1,0x1AD0,0x2AB3,0x3A92,
        0xFD2E,0xED0F,0xDD6C,0xCD4D,0xBDAA,0xAD8B,0x9DE8,0x8DC9,
        0x7C26,0x6C07,0x5C64,0x4C45,0x3CA2,0x2C83,0x1CE0,0x0CC1,
        0xEF1F,0xFF3E,0xCF5D,0xDF7C,0xAF9B,0xBFBA,0x8FD9,0x9FF8,
        0x6E17,0x7E36,0x4E55,0x5E74,0x2E93,0x3EB2,0x0ED1,0x1EF0
    };

    uint16_t crc = 0;
    const uint8_t *data = (const uint8_t *)&blob->version;
    uint16_t len = sizeof(param_blob_t) - offsetof(param_blob_t, version);

    while (len--) {
        crc = (uint16_t)((crc << 8) ^ crc16_table[((crc >> 8) ^ *data++) & 0xFF]);
    }
    return crc;
}

/* ==================================================================
 *  加载默认值
 * ================================================================== */
void param_store_load_defaults(void)
{
    memset(&g_params, 0, sizeof(g_params));

    g_params.magic    = PARAM_MAGIC;
    g_params.version  = 1;
    g_params.deadtime_ns      = DFLT_DEADTIME_NS;
    g_params.softstart_ms     = DFLT_SOFTSTART_MS;
    g_params.vout_target_v    = 220;  /* 默认 220V */
    g_params.pid_kp_q10       = DFLT_PID_KP_Q10;
    g_params.pid_ki_q10       = DFLT_PID_KI_Q10;
    g_params.overload_trip_ms = DFLT_OVERLOAD_TRIP_MS;
    g_params.pid_start_limit  = DFLT_PID_START_LIMIT;
}

/* ==================================================================
 *  从 Flash 加载
 * ================================================================== */
bool param_store_load(void)
{
    const param_blob_t *flash = (const param_blob_t *)PARAM_FLASH_ADDR;

    /* Magic 不匹配 → Flash 未写入过 */
    if (flash->magic != PARAM_MAGIC) {
        return false;
    }

    /* 校验 CRC */
    uint16_t calc_crc = param_store_crc16(flash);
    if (calc_crc != flash->crc16) {
        return false;
    }

    /* 数据有效，复制到 RAM */
    memcpy(&g_params, flash, sizeof(param_blob_t));
    return true;
}

/* ==================================================================
 *  保存到 Flash
 * ================================================================== */
bool param_store_save(void)
{
    uint32_t page_error;
    FLASH_EraseInitTypeDef erase = {0};

    /* 计算 CRC 并写入头部 */
    g_params.magic = PARAM_MAGIC;
    g_params.crc16 = param_store_crc16(&g_params);

    /*
     * 关全局中断 —— 关键！
     * Flash 操作期间不能取指，否则 HardFault。
     * 约 34ms 内 SPWM ISR 不执行，PWM 输出保持最后 CCR 值。
     */
    __disable_irq();

    HAL_FLASH_Unlock();

    /* 擦除 Page 63 */
    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.Banks     = FLASH_BANK_1;
    erase.Page      = PARAM_FLASH_PAGE;
    erase.NbPages   = 1;
    if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK) {
        HAL_FLASH_Lock();
        __enable_irq();
        return false;
    }

    /*
     * 按 64 位双字写入 Flash（STM32G4 最小写入单位）
     * 1024 字节 / 8 = 128 次双字写入
     */
    uint64_t *src = (uint64_t *)&g_params;
    uint64_t *dst = (uint64_t *)PARAM_FLASH_ADDR;
    for (uint16_t i = 0; i < (PARAM_BLOB_SIZE / 8); i++) {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD,
                              (uint32_t)(dst + i),
                              *(src + i)) != HAL_OK) {
            HAL_FLASH_Lock();
            __enable_irq();
            return false;
        }
    }

    HAL_FLASH_Lock();
    __enable_irq();

    /* 回读校验 */
    const param_blob_t *verify = (const param_blob_t *)PARAM_FLASH_ADDR;
    if (memcmp(&g_params, verify, sizeof(param_blob_t)) != 0) {
        return false;
    }

    return true;
}

/* ==================================================================
 *  检查 Flash 有效
 * ================================================================== */
bool param_store_has_valid_data(void)
{
    const param_blob_t *flash = (const param_blob_t *)PARAM_FLASH_ADDR;
    if (flash->magic != PARAM_MAGIC) {
        return false;
    }
    return param_store_crc16(flash) == flash->crc16;
}

/* ==================================================================
 *  上电初始化
 * ================================================================== */
void param_store_init(void)
{
    if (!param_store_load()) {
        /* Flash 无有效数据 → 加载编译期默认值 */
        param_store_load_defaults();
        debug_uart_print("PARAM: defaults loaded\r\n");
    } else {
        debug_uart_print("PARAM: loaded from flash\r\n");
    }
}
