# ================================================================
#  STM32G431 SPWM 逆变器 Makefile
# ================================================================
#
# 编译器：GNU Arm Embedded Toolchain
# 目标芯片：STM32G431KBT6 (Cortex-M4F, 170 MHz, 128 KB Flash, 32 KB RAM)
#
# 使用方法：
#   make all      编译生成 ELF 和 HEX
#   make clean    删除 build 目录

TARGET = stm32g431_spwm_inverter
BUILD_DIR = build

# 工具链前缀
PREFIX = arm-none-eabi-
CC = $(PREFIX)gcc
AS = $(PREFIX)gcc -x assembler-with-cpp
CP = $(PREFIX)objcopy   # ELF → HEX/BIN 转换
SZ = $(PREFIX)size      # 输出段大小统计

# CubeG4 软件包路径（默认当前目录，可覆盖：make CUBE_G4=/path/to/cube）
CUBE_G4 ?= .

# ================================================================
#  CPU 选项
# ================================================================
CPU = -mcpu=cortex-m4              # Cortex-M4 内核
FPU = -mfpu=fpv4-sp-d16            # 单精度 FPU
FLOAT_ABI = -mfloat-abi=hard       # 硬件浮点 ABI
MCU = $(CPU) -mthumb $(FPU) $(FLOAT_ABI)

# 预定义宏
C_DEFS = -DUSE_HAL_DRIVER -DSTM32G431xx

# 头文件包含路径
C_INCLUDES = \
-ICore/Inc \
-IDrivers/STM32G4xx_HAL_Driver/Inc \
-IDrivers/STM32G4xx_HAL_Driver/Inc/Legacy \
-IDrivers/CMSIS/Device/ST/STM32G4xx/Include \
-IDrivers/CMSIS/Include

# ================================================================
#  源文件列表
# ================================================================

# 应用层源文件（本工程编写的代码）
APP_SOURCES = \
Core/Src/main.c \
Core/Src/app.c \
Core/Src/adc_driver.c \
Core/Src/board.c \
Core/Src/button.c \
Core/Src/debug_uart.c \
Core/Src/lcd_menu.c \
Core/Src/param_store.c \
Core/Src/pid.c \
Core/Src/protection.c \
Core/Src/spwm.c \
Core/Src/ssd1306.c \
Core/Src/stm32g4xx_hal_msp.c \
Core/Src/stm32g4xx_it.c \
Core/Src/syscalls.c \
Core/Src/system_stm32g4xx.c

# STM32 HAL 库源文件
HAL_SOURCES = \
Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal.c \
Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_adc.c \
Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_adc_ex.c \
Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_cortex.c \
Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_dma.c \
Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_dma_ex.c \
Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_exti.c \
Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_flash.c \
Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_flash_ex.c \
Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_gpio.c \
Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_i2c.c \
Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_i2c_ex.c \
Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_pwr.c \
Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_pwr_ex.c \
Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_rcc.c \
Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_rcc_ex.c \
Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_tim.c \
Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_tim_ex.c \
Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_uart.c \
Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_uart_ex.c

# 汇编源文件（启动文件）
ASM_SOURCES = startup_stm32g431xx.s

SOURCES = $(APP_SOURCES) $(HAL_SOURCES)

# 目标文件：所有 .c/.s → build/xxx.o
OBJECTS = $(addprefix $(BUILD_DIR)/,$(notdir $(SOURCES:.c=.o)))
OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SOURCES:.s=.o)))
vpath %.c $(sort $(dir $(SOURCES)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))

# ================================================================
#  编译选项
# ================================================================
CFLAGS = $(MCU) $(C_DEFS) $(C_INCLUDES) \
         -O2 \
         -Wall -Wextra \
         -ffunction-sections \
         -fdata-sections

ASFLAGS = $(MCU) $(C_DEFS) $(C_INCLUDES) -Wall -fdata-sections -ffunction-sections

LDFLAGS = $(MCU) \
          -TSTM32G431KBTx_FLASH.ld \
          -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref \
          -Wl,--gc-sections \
          -specs=nano.specs \
          -lc -lm -lnosys

# ================================================================
#  构建目标
# ================================================================

all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex

# C 文件编译规则
$(BUILD_DIR)/%.o: %.c Makefile | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) $< -o $@

# 汇编文件编译规则
$(BUILD_DIR)/%.o: %.s Makefile | $(BUILD_DIR)
	$(AS) -c $(ASFLAGS) $< -o $@

# 链接规则
$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) Makefile
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@
	$(SZ) $@   # 输出 Flash/RAM 使用量

# HEX 生成规则（Intel HEX 格式，供烧录工具使用）
$(BUILD_DIR)/%.hex: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	$(CP) -O ihex $< $@

# 创建 build 目录
$(BUILD_DIR):
	mkdir -p $@

# 清理
clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean
