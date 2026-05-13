#ifndef PARAM_STORE_H
#define PARAM_STORE_H

/*
 * Flash 参数存储模块
 * -----------------
 * 使用 STM32G431 最后一页 Flash (Page 63, 2KB) 存储运行时参数。
 */

#include <stdint.h>
#include <stdbool.h>

/* Flash 布局：Page 63 = 最后 2KB 页 (0x0801F800) */
#define PARAM_MAGIC             0xA5F0U
#define PARAM_FLASH_PAGE        63
#define PARAM_FLASH_ADDR        0x0801F800U
#define PARAM_BLOB_SIZE         1024U

/* 参数结构体 */
typedef struct {
    uint16_t magic;
    uint16_t crc16;
    uint16_t version;
    uint16_t reserved;

    /* 可调参数（物理单位） */
    uint16_t deadtime_ns;       /* 死区时间 ns (500..5000) */
    uint16_t softstart_ms;      /* 软启动时间 ms (1000..30000) */
    uint16_t vout_target_v;     /* 输出电压目标 (V), 如220=220V */
    uint16_t pid_kp_q10;        /* PID Kp Q10 (0..10230) */
    uint16_t pid_ki_q10;        /* PID Ki Q10 (0..1023) */
    uint16_t overload_trip_ms;  /* 过载延时 ms (100..10000) */
    uint16_t pid_start_limit;   /* 软启动调制上限 (100..930) */

    uint8_t  padding[1002];
} param_blob_t;

_Static_assert(sizeof(param_blob_t) == PARAM_BLOB_SIZE,
               "param_blob_t size must match PARAM_BLOB_SIZE");

extern param_blob_t g_params;

void param_store_init(void);
bool param_store_save(void);
bool param_store_load(void);
void param_store_load_defaults(void);
uint16_t param_store_crc16(const param_blob_t *blob);
bool param_store_has_valid_data(void);

#endif
