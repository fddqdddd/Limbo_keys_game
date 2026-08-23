@echo off
rem ===============================================
rem  Limbo Keys: full rebuild of all .exe
rem  (computer_repair.exe -> trap.exe -> limbo key.exe -> installer.exe)
rem  Requires: python 3.8+, g++ and windres (MinGW-w64) in PATH
rem ===============================================

setlocal
cd /d "%~dp0"

echo === [1/2] Checking tools ===
where python >nul 2>nul
if errorlevel 1 (
    echo [ERROR] Python not found in PATH.
    pause
    exit /b 1
)
where g++ >nul 2>nul
if errorlevel 1 (
    echo [ERROR] g++ ^(MinGW-w64^) not found in PATH.
    pause
    exit /b 1
)
where windres >nul 2>nul
if errorlevel 1 (
    echo [ERROR] windres ^(MinGW-w64^) not found in PATH.
    pause
    exit /b 1
)

echo === [2/2] Building ===
python installer.py
if errorlevel 1 (
    echo.
    echo [ERROR] Build FAILED. See messages above.
    pause
    exit /b 1
)

echo.
echo === Done. Fresh binaries: build\ ^& dist\ ===
pause
exit /b 0
