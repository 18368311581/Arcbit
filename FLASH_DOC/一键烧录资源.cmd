@echo off
chcp 65001 >nul
setlocal
set "PYTHONUTF8=1"
set "PYTHON312=C:\Users\jason\AppData\Local\Programs\Python\Python312\python.exe"

if exist "%PYTHON312%" (
    "%PYTHON312%" "%~dp0program_resources.py" --program
) else (
    python "%~dp0program_resources.py" --program
)

echo.
if errorlevel 1 (
    echo Programming did not complete. See the error above.
) else (
    echo All resources were programmed and verified.
)
pause
