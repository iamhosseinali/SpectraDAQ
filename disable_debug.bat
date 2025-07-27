@echo off
echo Disabling debug mode in Monitor.pro...
echo.

REM Check if Monitor.pro exists
if not exist "Monitor.pro" (
    echo Error: Monitor.pro not found in current directory
    echo Please run this script from the project root directory
    pause
    exit /b 1
)

REM Create backup
copy "Monitor.pro" "Monitor.pro.backup" >nul
echo Created backup: Monitor.pro.backup

REM Disable debug mode by commenting the line
powershell -Command "(Get-Content 'Monitor.pro') -replace '^DEFINES \+= ENABLE_DEBUG', '# DEFINES += ENABLE_DEBUG' | Set-Content 'Monitor.pro'"

echo.
echo Debug mode disabled! 
echo.
echo To enable debug mode, run: enable_debug.bat
echo To rebuild without debug output: qmake && make
echo.
pause 