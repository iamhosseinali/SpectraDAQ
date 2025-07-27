@echo off
echo === UDP Performance System Check (Windows) ===
echo.

echo Checking Windows version...
ver
echo.

echo Checking if running as Administrator...
net session >nul 2>&1
if %errorLevel% == 0 (
    echo ✓ Running as Administrator - can set high buffer sizes
) else (
    echo ⚠ Not running as Administrator - buffer size may be limited
    echo   Try running as Administrator for maximum performance
)
echo.

echo Checking network adapter settings...
netsh interface tcp show global
echo.

echo Checking available memory...
wmic computersystem get TotalPhysicalMemory /format:value
echo.

echo Checking disk space...
wmic logicaldisk get size,freespace,caption /format:table
echo.

echo Checking CPU information...
wmic cpu get name,numberofcores,maxclockspeed /format:table
echo.

echo === Windows-Specific Recommendations ===
echo 1. Run as Administrator for maximum buffer sizes
echo 2. Disable Windows Defender real-time protection during logging
echo 3. Close other network-intensive applications
echo 4. Use SSD for logging if possible
echo 5. Set network adapter to "Full Duplex" mode
echo 6. Disable power saving features on network adapter
echo 7. Consider using Windows Server for critical applications
echo 8. Set process priority to "High" in Task Manager
echo.

echo === Registry Optimizations (run as Administrator) ===
echo To increase UDP buffer limits, add these registry keys:
echo HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\AFD\Parameters
echo - TcpReceiveWindowSize (REG_DWORD) = 65536
echo - TcpSendWindowSize (REG_DWORD) = 65536
echo - MaxUserPorts (REG_DWORD) = 65534
echo.

pause 