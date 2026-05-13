# STM32G431_SPWM_INVERTER CubeMX 对照清单

适用目标：

- MCU：`STM32G431KBT6` 或同资源 `STM32G431KBU6`
- 封装：`LQFP32`
- 目标：生成一个和本工程代码一致的 `CubeMX/CubeIDE` 工程，并能输出 `.hex`

---

## 1. 新建工程

- [ ] 打开 `STM32CubeIDE`
- [ ] `File -> New -> STM32 Project`
- [ ] 搜索并选择 `STM32G431KBT6`
- [ ] Project Name 填：`stm32g431_spwm_inverter`
- [ ] 弹窗 `Initialize all peripherals with their default Mode?` 选 `Yes`

---

## 2. Pinout 总表

按这个表配，和当前工程代码一致：

| 引脚 | CubeMX 功能 | 工程用途 |
|---|---|---|
| PA0 | ADC1_IN1 | 输出电压采样 |
| PA1 | ADC1_IN2 | 输出电流采样 |
| PA2 | USART2_TX | 调试串口 TX |
| PA3 | USART2_RX | 调试串口 RX |
| PA4 | ADC2_IN4 | NTC 温度采样 |
| PA5 | ADC2_IN5 | 母线电压采样 |
| PA6 | TIM1_BKIN | 硬件刹车输入 |
| PA7 | TIM1_CH1N | A 桥臂低侧 |
| PA8 | TIM1_CH1 | A 桥臂高侧 |
| PA9 | TIM1_CH2 | B 桥臂高侧 |
| PA10 | GPIO_Output | 风扇 |
| PA11 | GPIO_Input | BTN_DOWN |
| PA12 | GPIO_Input | BTN_OK |
| PA13 | SYS_SWDIO | SWD 调试 |
| PA14 | SYS_SWCLK | SWD 调试 |
| PA15 | GPIO_Input | SHORT_MCU |
| PB0 | TIM1_CH2N | B 桥臂低侧 |
| PB3 | GPIO_Input | 50/60Hz 选择 |
| PB4 | GPIO_Input | 单/双极性模式选择 |
| PB5 | GPIO_Output | 继电器 |
| PB6 | I2C1_SCL | OLED SCL |
| PB7 | I2C1_SDA | OLED SDA |
| PB8 | 不分配 | `BOOT0` 相关，留空 |
| PF0 | GPIO_Input | BTN_UP |
| PF1 | GPIO_Output | FAULT_LED |

不要配错成下面这些旧版本映射：

- [ ] `PA4/PA5` 不能再配成 `GPIO/BTN`
- [ ] `PB6/PB7` 不是 `USART1`
- [ ] `PB10/PB11` 不能配 `I2C2`
- [ ] `PB9` 不能配风扇
- [ ] `PB8` 不要拿来挂故障灯

---

## 3. Pinout 页面逐项配置

### 3.1 SYS

- [ ] `System Core -> SYS`
- [ ] Debug 选 `Serial Wire`

### 3.2 RCC

- [ ] `System Core -> RCC`
- [ ] `HSE` 保持 `Disable`
- [ ] 使用 `HSI`

### 3.3 GPIO 输入

下面 6 个脚都配成 `GPIO_Input`：

- [ ] `PA11` -> `BTN_DOWN`
- [ ] `PA12` -> `BTN_OK`
- [ ] `PA15` -> `SHORT_MCU`
- [ ] `PB3` -> `FREQ_SEL`
- [ ] `PB4` -> `MODE_SEL`
- [ ] `PF0` -> `BTN_UP`

GPIO 参数：

- [ ] Pull = `Pull-down`

### 3.4 GPIO 输出

下面 3 个脚都配成 `GPIO_Output`：

- [ ] `PA10` -> `FAN`
- [ ] `PB5` -> `RELAY`
- [ ] `PF1` -> `FAULT_LED`

GPIO 参数建议：

- [ ] Output Type = `Push Pull`
- [ ] Pull = `No pull`
- [ ] Speed = `Low`

### 3.5 ADC 引脚

- [ ] `PA0` -> `ADC1_IN1`
- [ ] `PA1` -> `ADC1_IN2`
- [ ] `PA4` -> `ADC2_IN4`
- [ ] `PA5` -> `ADC2_IN5`

### 3.6 TIM1 引脚

- [ ] `PA6` -> `TIM1_BKIN`
- [ ] `PA7` -> `TIM1_CH1N`
- [ ] `PA8` -> `TIM1_CH1`
- [ ] `PA9` -> `TIM1_CH2`
- [ ] `PB0` -> `TIM1_CH2N`

### 3.7 USART2

- [ ] `PA2` -> `USART2_TX`
- [ ] `PA3` -> `USART2_RX`

### 3.8 I2C1

- [ ] `PB6` -> `I2C1_SCL`
- [ ] `PB7` -> `I2C1_SDA`

---

## 4. Clock Configuration 页面

进入顶部 `Clock Configuration` 页面，确认：

- [ ] Clock Source = `HSI`
- [ ] `PLLM = /4`
- [ ] `PLLN = 85`
- [ ] `PLLP = /2`
- [ ] `PLLQ = /2`
- [ ] `PLLR = /2`
- [ ] `SYSCLK = 170 MHz`
- [ ] `AHB Prescaler = /1`
- [ ] `APB1 Prescaler = /1`
- [ ] `APB2 Prescaler = /1`

如果 CubeMX 弹出电压等级提示，确认：

- [ ] 允许使用 `Boost` 相关配置以支持 `170 MHz`

---

## 5. TIM1 配置

进入 `Timers -> TIM1`

Pinout/Mode：

- [ ] `Internal Clock`
- [ ] `PWM Generation CH1`
- [ ] `PWM Generation CH1N`
- [ ] `PWM Generation CH2`
- [ ] `PWM Generation CH2N`
- [ ] `Break Input`

Parameter Settings：

- [ ] Prescaler = `0`
- [ ] Counter Mode = `Up`
- [ ] Counter Period = `7726`
- [ ] Auto-reload preload = `Enable`
- [ ] Repetition Counter = `0`
- [ ] Clock Division = `DIV1`

CH1 / CH2：

- [ ] PWM mode = `PWM mode 1`
- [ ] Pulse = `3863`
- [ ] Polarity = `High`

Break and Dead Time：

- [ ] Break State = `Enable`
- [ ] Break Polarity = `High`
- [ ] Dead Time = `0`
- [ ] Automatic Output = `Disable`

BKIN 引脚建议：

- [ ] `PA6` 下拉，避免上电误刹车

---

## 6. TIM6 配置

进入 `Timers -> TIM6`

- [ ] Activated = `Enable`
- [ ] Prescaler = `169`
- [ ] Counter Period = `999`
- [ ] Auto-reload preload = `Disable`

这组参数对应：

- `170 MHz / 170 = 1 MHz`
- `1 MHz / 1000 = 1 kHz`
- 即 `1 ms` 节拍

---

## 7. ADC1 配置

进入 `Analog -> ADC1`

Pinout：

- [ ] `IN1`
- [ ] `IN2`

Parameter Settings：

- [ ] Clock Prescaler = `Asynchronous clock mode / 4`
- [ ] Resolution = `12 bits`
- [ ] Data Alignment = `Right`
- [ ] Scan Conversion Mode = `Enable`
- [ ] Continuous Conversion Mode = `Enable`
- [ ] Number Of Conversion = `2`
- [ ] End Of Conversion Selection = `End of sequence conversion`
- [ ] External Trigger Conversion Source = `Software Start`
- [ ] DMA Continuous Requests = `Enable`
- [ ] Overrun = `Data overwritten`
- [ ] Oversampling = `Disable`

Rank：

- [ ] Rank 1 = `Channel 1`, Sampling Time = `47.5 Cycles`
- [ ] Rank 2 = `Channel 2`, Sampling Time = `47.5 Cycles`

ADC1 对应关系：

- [ ] CH1 -> `PA0` -> 输出电压
- [ ] CH2 -> `PA1` -> 输出电流

---

## 8. ADC2 配置

进入 `Analog -> ADC2`

Pinout：

- [ ] `IN4`
- [ ] `IN5`

Parameter Settings：

- [ ] Clock Prescaler = `Asynchronous clock mode / 4`
- [ ] Resolution = `12 bits`
- [ ] Data Alignment = `Right`
- [ ] Scan Conversion Mode = `Enable`
- [ ] Continuous Conversion Mode = `Enable`
- [ ] Number Of Conversion = `2`
- [ ] End Of Conversion Selection = `End of sequence conversion`
- [ ] External Trigger Conversion Source = `Software Start`
- [ ] DMA Continuous Requests = `Enable`
- [ ] Overrun = `Data overwritten`
- [ ] Oversampling = `Disable`

Rank：

- [ ] Rank 1 = `Channel 4`, Sampling Time = `47.5 Cycles`
- [ ] Rank 2 = `Channel 5`, Sampling Time = `47.5 Cycles`

ADC2 对应关系：

- [ ] CH4 -> `PA4` -> 温度
- [ ] CH5 -> `PA5` -> 母线电压

---

## 9. DMA 配置

进入 `System Core -> DMA`

### ADC1 DMA

- [ ] Add `ADC1`
- [ ] Request / Channel 对应 `DMA1 Channel1`
- [ ] Mode = `Circular`
- [ ] Direction = `Peripheral To Memory`
- [ ] Peripheral Increment = `Disable`
- [ ] Memory Increment = `Enable`
- [ ] Peripheral Data Width = `Half Word`
- [ ] Memory Data Width = `Half Word`
- [ ] Priority = `High`

### ADC2 DMA

- [ ] Add `ADC2`
- [ ] Request / Channel 对应 `DMA1 Channel2`
- [ ] Mode = `Circular`
- [ ] Direction = `Peripheral To Memory`
- [ ] Peripheral Increment = `Disable`
- [ ] Memory Increment = `Enable`
- [ ] Peripheral Data Width = `Half Word`
- [ ] Memory Data Width = `Half Word`
- [ ] Priority = `High`

---

## 10. I2C1 配置

进入 `Connectivity -> I2C1`

- [ ] Mode = `I2C`
- [ ] Speed Mode = `Fast Mode`
- [ ] Speed Frequency = `400 kHz`

如果 CubeMX 自动生成 Timing 值即可；本工程代码覆盖后会使用自己的初始化参数。

---

## 11. USART2 配置

进入 `Connectivity -> USART2`

- [ ] Mode = `Asynchronous`
- [ ] Baud Rate = `115200`
- [ ] Word Length = `8 Bits`
- [ ] Parity = `None`
- [ ] Stop Bits = `1`
- [ ] Hardware Flow Control = `Disable`

---

## 12. GPIO 参数复查

进入 `System Core -> GPIO`，重点复查：

- [ ] `PA11/PA12/PA15/PB3/PB4/PF0` 都是 `Pull-down`
- [ ] `PA10/PB5/PF1` 都是 `Output Push Pull`
- [ ] `PA6` 作为 `BKIN` 建议下拉
- [ ] `PB6/PB7` 是 `I2C1 AF_OD`
- [ ] `PA2/PA3` 是 `USART2 AF`
- [ ] `PA0/PA1/PA4/PA5` 是 `Analog`

---

## 13. Project Manager

进入 `Project Manager`

### Project

- [ ] Toolchain / IDE = `STM32CubeIDE`

### Code Generator

- [ ] `Keep User Code when re-generating`
- [ ] `Copy all used libraries into the project folder`

### Advanced Settings

- [ ] Application Structure = `Advanced`

---

## 14. 生成代码

- [ ] 点击 `Generate Code`

生成完成后，用本工程代码覆盖：

```bat
setup_for_cubeide.bat "你的CubeIDE工程目录"
```

例如：

```bat
setup_for_cubeide.bat "C:\Users\zuguotang\STM32CubeIDE\workspace_xxx\stm32g431_spwm_inverter"
```

---

## 15. 在 CubeIDE 输出 HEX

CubeMX 只负责生成工程，`.hex` 一般由 `CubeIDE` 构建输出。

进入：

- [ ] `Project -> Properties`
- [ ] `C/C++ Build -> Settings`
- [ ] `MCU Post build outputs`
- [ ] 勾选 `Convert to Intel Hex file`

然后：

- [ ] `Project -> Build All`

正常情况下会得到：

- `Debug/stm32g431_spwm_inverter.hex`
或
- `Release/stm32g431_spwm_inverter.hex`

---

## 16. 最终自检

如果下面任一项出现，说明配错了：

- [ ] `PA4/PA5` 被配成 `ADC1`
- [ ] `PB6/PB7` 被配成 `USART1`
- [ ] `PB10/PB11` 被配成 `I2C2`
- [ ] `PB9` 被拿来接风扇
- [ ] `PB8` 被拿来接故障灯
- [ ] 只配了 `ADC1`，没配 `ADC2`
- [ ] `ADC1/ADC2` 的 `Number Of Conversion` 不是 `2`
- [ ] `TIM1` 没开 `CH1N/CH2N/BKIN`
- [ ] `TIM6` 不是 `PSC=169, ARR=999`
- [ ] `SYSCLK` 不是 `170 MHz`

---

## 17. 建议的配完顺序

建议你实际操作时按这个顺序点：

1. `SYS`
2. `RCC`
3. 全部 `Pinout`
4. `Clock Configuration`
5. `TIM1`
6. `TIM6`
7. `ADC1`
8. `ADC2`
9. `DMA`
10. `USART2`
11. `I2C1`
12. `GPIO`
13. `Project Manager`
14. `Generate Code`
15. `覆盖 Core/`
16. `打开 HEX 输出`

