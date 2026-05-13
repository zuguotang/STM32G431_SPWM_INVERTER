@echo off
chcp 65001 >nul
echo ================================================
echo  STM32CubeIDE 工程自动部署脚本
echo ================================================
echo.
echo 此脚本将把本目录的 Core/ 应用代码复制到
echo STM32CubeIDE 工程中，覆盖生成的空壳代码。
echo.
echo PRESS ANY KEY TO CONTINUE...
pause >nul

if "%1"=="" (
    echo.
    echo 用法: setup_for_cubeide.bat  [CubeIDE工程目录路径]
    echo.
    echo 例如: setup_for_cubeide.bat  C:\Users\zuguotang\STM32CubeIDE\workspace_1.15.0\stm32g431_spwm_inverter
    echo.
    pause
    exit /b 1
)

set CUBE_DIR=%1

if not exist "%CUBE_DIR%" (
    echo 错误: 目录不存在 "%CUBE_DIR%"
    pause
    exit /b 1
)

if not exist "%CUBE_DIR%\.project" (
    echo 错误: 目标目录没有 .project 文件，请确认是 CubeIDE 工程目录
    pause
    exit /b 1
)

echo.
echo 目标工程: %CUBE_DIR%
echo.
echo 正在复制应用层文件...

:: 复制头文件
xcopy /Y /Q "Core\Inc\*.h" "%CUBE_DIR%\Core\Inc\"

:: 复制源文件
xcopy /Y /Q "Core\Src\*.c" "%CUBE_DIR%\Core\Src\"

:: 复制必要工程文件
xcopy /Y /Q "startup_stm32g431xx.s" "%CUBE_DIR%\"
xcopy /Y /Q "STM32G431KBTx_FLASH.ld" "%CUBE_DIR%\"

:: 删除 CubeMX 自动生成的冗余文件
del /Q "%CUBE_DIR%\Core\Src\gpio.c" 2>nul

echo.
echo ================================================
echo  完成！
echo ================================================
echo.
echo 下一步:
echo   1. 打开 STM32CubeIDE
echo   2. File - Open Projects from File System
echo   3. 选择 "%CUBE_DIR%"
echo   4. Project - Build All (Ctrl+B)
echo   5. Run - Debug 烧录
echo.
pause
