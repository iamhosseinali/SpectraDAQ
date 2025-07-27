#!/bin/bash

echo "=== UDP Performance System Check ==="
echo

# Check kernel parameters
echo "Kernel Parameters:"
echo "net.core.rmem_max: $(sysctl -n net.core.rmem_max 2>/dev/null || echo 'Not available')"
echo "net.core.rmem_default: $(sysctl -n net.core.rmem_default 2>/dev/null || echo 'Not available')"
echo "net.core.netdev_max_backlog: $(sysctl -n net.core.netdev_max_backlog 2>/dev/null || echo 'Not available')"
echo

# Check if running as root
if [ "$EUID" -eq 0 ]; then
    echo "✓ Running as root - can set high buffer sizes"
else
    echo "⚠ Not running as root - buffer size may be limited"
    echo "  Try running with sudo for maximum performance"
fi
echo

# Check CPU frequency scaling
echo "CPU Frequency Scaling:"
if command -v cpufreq-info >/dev/null 2>&1; then
    cpufreq-info | grep "current CPU frequency" | head -1
else
    echo "cpufreq-info not available"
fi
echo

# Check available memory
echo "Memory:"
free -h
echo

# Check disk I/O
echo "Disk I/O (if available):"
if command -v iostat >/dev/null 2>&1; then
    iostat -x 1 1 | tail -2
else
    echo "iostat not available"
fi
echo

echo "=== Recommendations ==="
echo "1. Run with sudo for maximum buffer sizes"
echo "2. Set CPU governor to performance: sudo cpufreq-set -g performance"
echo "3. Use SSD for logging if possible"
echo "4. Close other network-intensive applications"
echo "5. Consider using real-time kernel for critical applications" 