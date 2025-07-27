@echo off
echo Enabling debug mode in Monitor.pro...
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

REM Enable debug mode by uncommenting the line
powershell -Command "(Get-Content 'Monitor.pro') -replace '# DEFINES \+= ENABLE_DEBUG', 'DEFINES += ENABLE_DEBUG' | Set-Content 'Monitor.pro'"

echo.
echo Debug mode enabled! 
echo.
echo To disable debug mode, run: disable_debug.bat
echo To rebuild with debug output: qmake && make
echo.
pause 