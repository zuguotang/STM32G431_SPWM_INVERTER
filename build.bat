@echo off
setlocal

REM ================================================================
REM  STM32G431 SPWM 逆变器 一键编译脚本
REM ================================================================
REM
REM 前提条件：
REM   1. GNU Arm Embedded Toolchain 已安装，arm-none-eabi-gcc 在 PATH 中
REM   2. Drivers\CMSIS 和 Drivers\STM32G4xx_HAL_Driver 已从
REM      STM32CubeG4 软件包复制到本目录
REM   3. make 工具在 PATH 中（推荐 MSYS2 或 Git Bash 的 make）
REM
REM 编译产物：
REM   build\stm32g431_spwm_inverter.elf    ELF 文件（调试用）
REM   build\stm32g431_spwm_inverter.hex    Intel HEX（烧录用）
REM   build\stm32g431_spwm_inverter.map    链接映射文件
REM ================================================================

REM 检查 arm-none-eabi-gcc 是否可用
where arm-none-eabi-gcc >nul 2>nul
if errorlevel 1 (
  echo arm-none-eabi-gcc not found. Install GNU Arm Embedded Toolchain and add it to PATH.
  exit /b 1
)

REM 检查 HAL 驱动源码是否存在
if not exist Drivers\STM32G4xx_HAL_Driver\Src (
  echo STM32CubeG4 HAL driver not found.
  echo Copy Drivers\CMSIS and Drivers\STM32G4xx_HAL_Driver from a STM32CubeG4 package or CubeMX project into this directory.
  exit /b 1
)

REM 检查 make 是否可用
where make >nul 2>nul
if errorlevel 1 (
  echo make not found. Use STM32CubeIDE import, or install make.
  exit /b 1
)

REM 调用 Makefile 执行编译
make all
endlocal
