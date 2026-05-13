# STM32G431 纯正弦波 SPWM 逆变器

STM32 HAL 版参考工程，建议 MCU `STM32G431KBT6/KBU6`（32 脚 LQFP32），
TIM1 互补 PWM、BKIN 硬件刹车、快速 ADC/DMA，22 kHz 纯正弦逆变后级。

## 功能总览

### SPWM
- TIM1 22 kHz 互补 SPWM，单极性/双极性可选，50/60 Hz 可选
- 线性插值正弦查表（257 点 Q1.10 + 8 位小数），THD 性能优于直接查表
- 死区 ns → DTG 硬件编码自动转换

### 闭环控制
- 增量式 PID（测量微分 + 积分限幅 + 输出斜率限制），Q10 定点运算
- 感性负载优化：5s 软启动 / 50% 起步调制 / 过流主动降压
- 输出电压 RMS 真有效值闭环，替代瞬时值反馈

### 保护体系
- **硬件短路**：PA6 BKIN 纳秒关断 + PA15 通知 MCU → 3 次重试 → 永久锁定
- **三级过流**：100% 过载 5s / 110% 过流 2s / 120% 过流 0.5s → 锁存故障
- 过温 / 母线过压 / 母线欠压 / 输出欠压，各有独立延时确认
- 故障 LED 闪烁码 + UART 串口输出故障信息

### 显示与交互（v2.0.0）
- SSD1306 128×64 OLED（I2C，仅 2 脚），主屏显示 RMS 电压 / 电流 / 功率 / 频率 / 温度 / 调制幅度
- 3 键数字菜单（UP/DOWN/OK），10s 空闲自动回主屏
- 7 个参数可运行时修改并保存至 Flash，断电保留：死区 / 软启动 / 电压目标 / Kp / Ki / 过载延时 / 启动限幅
- Flash 存储 CRC16 校验，首次上电自动加载编译期默认值

### 信号处理（借鉴 ZFM32F030 方案）
- RMS 有效值计算（800 点累加 + 牛顿迭代开方）
- 上电电流零点自动校准（消除传感器直流偏置）
- NTC 温度查表（256 项 β=3950，输出实际 ℃）
- 有功功率计算（P = Vrms × Irms）
- HV/LV 编译开关：切换全部阈值和标定系数

---

## 引脚分配（STM32G431KBT6 LQFP32）

| 引脚 | 功能 | 说明 |
|---|---|---|
| PA0 | ADC1_IN1 | 输出电压采样 |
| PA1 | ADC1_IN2 | 输出电流采样（互感器/采样电阻） |
| PA2 | USART2_TX | 调试串口 |
| PA3 | USART2_RX | 调试串口 |
| PA4 | ADC2_IN4 | NTC 温度传感器 |
| PA5 | ADC2_IN5 | 母线电压采样 |
| PA6 | TIM1_BKIN | 硬件刹车输入（比较器高电平触发） |
| PA7 | TIM1_CH1N | A 桥臂低侧 MOSFET |
| PA8 | TIM1_CH1 | A 桥臂高侧 MOSFET |
| PA9 | TIM1_CH2 | B 桥臂高侧 MOSFET |
| PA10 | GPIO 输出 | 散热风扇 |
| PA11 | BTN_DOWN | 菜单下/减少 |
| PA12 | BTN_OK | 菜单确认 |
| PA13 | SWDIO | 调试（必须保留） |
| PA14 | SWCLK | 调试（必须保留） |
| PA15 | GPIO 输入 | 外部短路信号（比较器输出 → MCU 重试计数） |
| PB0 | TIM1_CH2N | B 桥臂低侧 MOSFET |
| PB3 | GPIO 输入 | 50/60 Hz 选择（高=60Hz） |
| PB4 | GPIO 输入 | SPWM 模式选择（高=双极性） |
| PB5 | GPIO 输出 | 继电器控制（高=吸合，可选焊） |
| PB6 | I2C1_SCL | SSD1306 OLED |
| PB7 | I2C1_SDA | SSD1306 OLED |
| PF0 | GPIO 输入 | 菜单上/增加 |
| PF1 | GPIO 输出 | 故障 LED（闪烁码） |

> **注意**：LQFP32 封装没有 `PB1/PB2/PB9/PB10/PB11`；`PB8` 为 `BOOT0` 相关脚，不建议作普通 GPIO 使用。

---

## 保护参数一览

| 保护 | 条件 | 动作 |
|---|---|---|
| 硬件短路 | PA6 BKIN 比较器触发 | 纳秒关 PWM + PA15 通知 MCU → 重试 3 次 → 锁定 |
| Lv1 过载 | I > 3040 ADC (100%), 5s | 软件关 PWM + 断继电器 |
| Lv2 过流 | I > 3344 ADC (110%), 2s | 同上 |
| Lv3 过流 | I > 3648 ADC (120%), 0.5s | 同上（逼近短路速度） |
| 过温 | NTC > 60.0℃ | 立即锁存 |
| 母线过压 | Vbus > VBUS_ADC_MAX_RUN | 立即锁存 |
| 母线欠压 | Vbus < VBUS_ADC_MIN_RUN, 80ms | 锁存 |
| 输出欠压 | Vout < VOUT_ADC_LOW_LOCK, 700ms | 锁存（启动后） |

---

## OLED 菜单可调参数

短按 OK → 进入菜单 → UP/DOWN 选择 → OK 编辑 → UP/DOWN 调值（长按快调）→ OK 确认 → 选 "SaveFlash" 保存

| 参数 | 范围 | 默认值 |
|---|---|---|
| 死区时间 DeadTime | 500~5000 ns | 1000 |
| 软启动 SoftStart | 1~30 s | 5 |
| 电压目标 VoutADC | 0~4095 | 2480 |
| PID Kp | 0~10.0 | 0.254 |
| PID Ki | 0~1.0 | 0.010 |
| 过载延时 Ovl.Trip | 0.1~10 s | 3.0 |
| 启动限幅 St.Limit | 100~930 | 500 |

---

## 编译

需 `arm-none-eabi-gcc` + make + STM32CubeG4 固件包。

1. 从 STM32CubeG4 复制 `Drivers/CMSIS` 和 `Drivers/STM32G4xx_HAL_Driver` 到本目录
2. 运行 `build.bat` 或 `make all`
3. 输出 `build/stm32g431_spwm_inverter.hex`

也可导入 STM32CubeIDE：新建 G431KB 工程 → CubeMX 配置引脚 → 生成代码 → 用本目录 `Core/` 覆盖生成的应用层。

---

## 硬件注意事项

- **短路必须走硬件**：外部比较器 → PA6 BKIN + PA15 MCU 通知。软件三级过流是补充，不能替代硬件。
- **感性负载**：比较器短路阈值设高（远离正常工作电流），靠软件过流分级处理浪涌。
- **电流零点校准**：上电时自动采集偏置值，需确保上电瞬间电流传感器输出为零（PWM 未开）。
- **继电器 PB5 可留空**：PWM 关断已足够应对正常保护场景。仅 MOSFET 击穿时需要继电器。
- **NTC 查表**：默认 β=3950 / R25=10kΩ / 10kΩ 上拉。更换 NTC 需重新生成查表。
