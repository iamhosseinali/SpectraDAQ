# SpectraDAQ - UDP Data Acquisition Monitor

## Overview
SpectraDAQ is a Qt-based vibe coded real-time data acquisition monitor designed for UDP packet processing. The application implements zero-copy data handling, lock-free ring buffers, and binary logging for maximum performance.

## Core Architecture

### UDP Communication Layer
- Single-threaded UDP socket with configurable buffer sizes (64MB default)
- OS-level socket buffer optimization for Windows/Linux
- Thread priority elevation for real-time performance
- Batch processing of pending datagrams (1000 packet batches)

### Data Parsing Engine
- Dynamic C struct parser with field offset precomputation
- Type-aware value extraction (int8_t through uint64_t, float, double)
- Endianness handling with compile-time optimized conversion functions
- Array field support with configurable indexing

### Ring Buffer Implementation
- Lock-free single-producer, single-consumer (SPSC) design
- Preallocated memory pool (65536 packets × 64KB each)
- Packet dropping strategy for high-rate scenarios
- Zero-copy data transfer using raw pointers

## Performance Optimizations

### Memory Management
- Precomputed field offsets, sizes, and alignments
- std::function lambdas for type conversion (eliminates runtime branching)
- QByteArray::fromRawData for zero-copy packet handling
- Memory pool with std::unique_ptr<char[]> for packet storage

### Threading Model
- Dedicated UDP worker thread with high priority
- Separate logging thread with buffered disk I/O
- UI thread isolation for responsive plotting
- QMetaObject::invokeMethod for thread-safe communication

### Logging System
- Binary logging mode for maximum throughput
- CSV logging with type-aware field extraction
- Automatic post-processing: binary → CSV conversion
- Buffered writes with configurable batch sizes

## Binary Logging Protocol

### Conversion Process
- Header validation and metadata extraction
- Batch processing with 64KB read buffers
- Field-by-field parsing using stored field definitions
- CSV output with proper type conversion

## Configuration

### Socket Buffer Tuning
```cpp
// Windows
setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &bufSize, sizeof(bufSize));

// Linux
setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &bufSize, sizeof(bufSize));
```

### Thread Priority
```cpp
// Windows
SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

// Linux
pthread_setschedparam(threadHandle, SCHED_FIFO, &sch_params);
```

## Build Configuration

### Debug Mode
```bash
# Enable debug output
qmake "DEFINES += ENABLE_DEBUG"
make clean && make

# Disable debug output (default)
qmake
make clean && make
```

### Dependencies
- Qt 5.13+ or Qt 6.x
- C++17 standard
- Windows: ws2_32 library
- Linux: pthread, sched libraries

## Usage Workflow

### Data Capture
1. Define C struct in UI
2. Enable binary logging checkbox
3. Set logging duration and filename
4. Start capture (binary file created)
5. Automatic conversion to CSV post-capture

### Performance Monitoring
- Ring buffer utilization tracking
- Packet drop statistics
- Thread priority verification
- Socket buffer size validation

## Technical Specifications

### Console Output Control
- Debug output controlled by ENABLE_DEBUG macro
- Windows console allocation only in debug mode
- Performance impact eliminated in release builds

### Monitoring Commands
```bash
# Enable debug mode
./enable_debug.bat

# Disable debug mode  
./disable_debug.bat

# Test high-rate performance
python test_high_rate.py 100 10  # 100 Mbps for 10 seconds
```
