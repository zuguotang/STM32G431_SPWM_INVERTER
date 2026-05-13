# 在 STM32CubeIDE 中编译本工程

---

## 第一步：创建 CubeMX 工程并生成 HAL 库

### 1.1 新建工程

1. 打开 **STM32CubeIDE**
2. `File → New → STM32 Project`
3. 搜索芯片：`STM32G431KBT6` → 选中 → **Next**
4. 工程名填 `stm32g431_spwm_inverter` → **Finish**
5. 弹窗 "Initialize all peripherals with their default Mode?" → **Yes**

### 1.2 CubeMX 引脚配置

对照下表在芯片引脚图上逐个右键 → 搜索 → 选中：

| 引脚 | 功能 | 说明 |
|------|------|------|
| PA0 | ADC1_IN1 | 输出电压采样 |
| PA1 | ADC1_IN2 | 输出电流采样 |
| PA2 | USART2_TX | 调试串口 |
| PA3 | USART2_RX | 调试串口 |
| PA4 | ADC1_IN4 | NTC 温度传感器 |
| PA5 | ADC1_IN5 | 母线电压采样 |
| PA6 | TIM1_BKIN | 硬件刹车 |
| PA7 | TIM1_CH1N | A 桥臂低侧 |
| PA8 | TIM1_CH1 | A 桥臂高侧 |
| PA9 | TIM1_CH2 | B 桥臂高侧 |
| PA10 | GPIO_Output | 风扇 (FAN) |
| PA11 | GPIO_Input | 按键 DOWN |
| PA12 | GPIO_Input | 按键 OK |
| PA13 | SYS_SWDIO | 调试 (默认) |
| PA14 | SYS_SWCLK | 调试 (默认) |
| PA15 | GPIO_Input | 外部短路信号 (SHORT_MCU) |
| PB0 | TIM1_CH2N | B 桥臂低侧 |
| PB3 | GPIO_Input | 50/60Hz 选择 |
| PB4 | GPIO_Input | SPWM 模式选择 |
| PB5 | GPIO_Output | 继电器 (RELAY) |
| PB6 | I2C1_SCL | OLED |
| PB7 | I2C1_SDA | OLED |
| PB8 | -- 不分配 -- | BOOT0，留给硬件 |
| PF0 | GPIO_Input | 按键 UP |
| PF1 | GPIO_Output | 故障 LED |

### 1.3 GPIO 微调

左侧 **System Core → GPIO**，逐个设置 Pull：

| 引脚 | Pull |
|------|------|
| PA11 (BTN_DOWN) | Pull-down |
| PA12 (BTN_OK) | Pull-down |
| PA15 (SHORT_MCU) | Pull-down |
| PB3 (FREQ_SEL) | Pull-down |
| PB4 (MODE_SEL) | Pull-down |
| PF0 (BTN_UP) | Pull-down |

### 1.4 时钟配置

1. **System Core → RCC**：HSE = Disable（用 HSI）
2. 点顶部 **Clock Configuration** 标签页：
   - PLL Source Mux → **HSI**
   - PLLM: `/4`
   - PLLN: `×85`
   - PLLP: `/2`
   - System Clock Mux → **PLLCLK**
   - 确认右上角 System Clock = **170 MHz**
   - AHB/APB1/APB2 Prescaler 全部 `/1`

### 1.5 TIM1 配置（SPWM）

左侧 **Timers → TIM1**，右侧勾选：
- [x] Internal Clock
- [x] Channel1 → PWM Generation CH1
- [x] Channel1 → PWM Generation CH1N
- [x] Channel2 → PWM Generation CH2
- [x] Channel2 → PWM Generation CH2N
- [x] Break Input → Enable

Configuration：
- Counter Settings：
  - Prescaler: **0**
  - Counter Period: **7726**
  - Auto-reload preload: **Enable**
- Break：
  - Break State: **Enable**
  - Break Polarity: **High**
- Dead Time：**0**（运行时动态设）
- PWM CH1/CH2：Mode=PWM mode 1, Pulse=**3863**

### 1.6 TIM6 配置（1ms 时基）

左侧 **Timers → TIM6**：
- [x] Activated
- Prescaler: **169**
- Counter Period: **999**

### 1.7 ADC1 配置

左侧 **Analog → ADC1**：
- [x] IN1 / IN2 / IN4 / IN5 Single-ended

Configuration：
- Clock Prescaler: **Asynchronous /4**
- Resolution: **12 bits**
- Scan Conversion Mode: **Enable**
- Continuous Conversion Mode: **Enable**
- Number Of Conversions: **4**
- DMA Continuous Requests: **Enable**

四个 Rank：
| Rank | Channel | Sampling Time |
|------|---------|---------------|
| 1 | Channel 1 | 47.5 Cycles |
| 2 | Channel 2 | 47.5 Cycles |
| 3 | Channel 4 | 47.5 Cycles |
| 4 | Channel 5 | 47.5 Cycles |

### 1.8 DMA 配置

**System Core → DMA** → DMA1 标签页：
- 点 **Add** → 选 **ADC1**
- Mode: **Circular**
- Memory Increment: **Enable**
- Data Width: **Half Word**

### 1.9 I2C1 配置

**Connectivity → I2C1**：
- Mode: **I2C**
- Speed: **Fast Mode (400kHz)**

### 1.10 USART2 配置

**Connectivity → USART2**：
- Mode: **Asynchronous**
- Baud Rate: **115200**

### 1.11 系统设置

**System Core → SYS**：
- Debug: **Serial Wire**

### 1.12 项目设置

点 **Project Manager** 标签页：
- Application Structure: **Advanced**
- Code Generator 选项卡：
  - [x] Copy all used libraries into the project folder
  - [x] Keep User Code when re-generating

### 1.13 生成代码

点工具栏 **齿轮 ⚙ Generate Code**（或 Alt+K）

---

## 第二步：用本工程代码覆盖 CubeMX 空壳

生成完成后，**不要关闭 CubeIDE**，执行：

```bat
cd C:\Users\zuguotang\Desktop\STM32G431_SPWM_INVERTER
setup_for_cubeide.bat "你的CubeIDE工程路径"
```

例如：
```bat
setup_for_cubeide.bat "C:\Users\zuguotang\STM32CubeIDE\workspace_1.15.0\stm32g431_spwm_inverter"
```

此脚本自动：
- 复制我们所有 `Core/Inc/*.h` → 工程 `Core/Inc/`
- 复制我们所有 `Core/Src/*.c` → 工程 `Core/Src/`
- 删除冗余 `gpio.c`

---

## 第三步：编译 + 烧录

1. CubeIDE 中 `Project → Build All`（Ctrl+B）
2. ST-Link 接好 → 点绿色三角 **Run** 烧录
3. 点虫子 **Debug** → 在线调试

---

## 引脚速查表（LQFP32）

```
Pin1  VDD       Pin9  PA4(ADC)   Pin17 VDD       Pin25 PA15(SHORT)
Pin2  PF0(BTN)  Pin10 PA5(ADC)   Pin18 PA8(CH1)   Pin26 PB3(FREQ)
Pin3  PF1(LED)  Pin11 PA6(BKIN)  Pin19 PA9(CH2)   Pin27 PB4(MODE)
Pin4  NRST      Pin12 PA7(CH1N)  Pin20 PA10(FAN)  Pin28 PB5(RELAY)
Pin5  PA0(ADC)  Pin13 PB0(CH2N)  Pin21 PA11(DOWN) Pin29 PB6(SCL)
Pin6  PA1(ADC)  Pin14 VSSA       Pin22 PA12(OK)   Pin30 PB7(SDA)
Pin7  PA2(TX)   Pin15 VDDA       Pin23 PA13(SWDIO) Pin31 PB8-BOOT0
Pin8  PA3(RX)   Pin16 VSS        Pin24 PA14(SWCLK) Pin32 VSS
```
