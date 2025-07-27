# Debug System

This project now uses a proper debug system controlled by the `ENABLE_DEBUG` macro instead of a UI checkbox. This provides better performance and follows best practices.

## How to Enable Debug Output

### Method 1: Using Batch Scripts (Recommended)

1. **Enable debug mode:**
   ```
   enable_debug.bat
   ```

2. **Disable debug mode:**
   ```
   disable_debug.bat
   ```

3. **Rebuild the project:**
   ```
   qmake && make
   ```

### Method 2: Manual Configuration

1. **Edit `Monitor.pro`:**
   - Find the line: `# DEFINES += ENABLE_DEBUG`
   - Uncomment it: `DEFINES += ENABLE_DEBUG`

2. **Rebuild the project:**
   ```
   qmake && make
   ```

## Debug Output

When debug mode is enabled, you will see detailed output including:

- **UDP Worker:**
  - Socket configuration and buffer sizes
  - Packet reception and processing
  - Data parsing and value extraction
  - Ring buffer status
  - Signal emissions

- **MainWindow:**
  - Struct parsing results
  - Field selection
  - Configuration updates
  - Data reception

- **LoggingManager:**
  - Binary/CSV logging status
  - Packet processing
  - File operations

## Performance Impact

- **Debug mode OFF:** Maximum performance, no debug output
- **Debug mode ON:** Reduced performance due to console output overhead

## Best Practices

1. **Development:** Use debug mode to troubleshoot issues
2. **Production:** Always disable debug mode for maximum performance
3. **High-rate data:** Debug output can cause packet drops at rates >100 Mb/s

## Troubleshooting

If you need to debug issues:

1. Run `enable_debug.bat`
2. Rebuild with `qmake && make`
3. Run the application and check console output
4. When done, run `disable_debug.bat` and rebuild

## Files Modified

The following files now use `#ifdef ENABLE_DEBUG`:

- `UdpWorker.cpp` - All debug output wrapped
- `mainwindow.cpp` - All debug output wrapped  
- `LoggingManager.cpp` - All debug output wrapped
- `Monitor.pro` - Debug macro definition
- `mainwindow.h` - Removed debug checkbox references

## Removed Features

- UI debug checkbox (no longer needed)
- `MainWindow::debugLogEnabled` static variable
- `MainWindow::setDebugLogEnabled()` method 