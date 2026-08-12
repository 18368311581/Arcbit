@echo off
setlocal
set "ARMCC=C:\Keil_v5\ARM\ARMCC\bin"

if not exist "%ARMCC%\armcc.exe" (
    echo Keil ARM Compiler 5 was not found.
    pause
    exit /b 1
)

if not exist "%~dp0build" mkdir "%~dp0build"

"%ARMCC%\armcc.exe" --cpu Cortex-M4 --thumb --c99 -O2 --apcs=/ropi/rwpi -I"%~dp0." -c "%~dp0FlashPrg.c" -o "%~dp0build\FlashPrg.o"
if errorlevel 1 goto failed

"%ARMCC%\armcc.exe" --cpu Cortex-M4 --thumb --c99 -O2 --apcs=/ropi/rwpi -I"%~dp0." -c "%~dp0FlashDev.c" -o "%~dp0build\FlashDev.o"
if errorlevel 1 goto failed

"%ARMCC%\armlink.exe" --cpu Cortex-M4 --entry Init --scatter "%~dp0Target.lin" --keep "FlashDev.o(.constdata)" --diag_suppress L6305 --output "%~dp0build\W25Q64_STM32F411_SPI2.FLM" "%~dp0build\FlashPrg.o" "%~dp0build\FlashDev.o"
if errorlevel 1 goto failed

echo Loader built: %~dp0build\W25Q64_STM32F411_SPI2.FLM
exit /b 0

:failed
echo Loader build failed.
pause
exit /b 1
