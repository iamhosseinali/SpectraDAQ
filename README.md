# SpectraDAQ

## Overview
SpectraDAQ is a desktop application for real-time monitoring and analysis of data acquired over UDP. It provides oscilloscope-style time-domain visualization and FFT-based frequency-domain analysis. The application is implemented in C++ using the Qt framework and supports custom data struct parsing for flexible DAQ integration.

## Features
- Real-time UDP data acquisition and plotting
- Time-domain and frequency-domain (FFT) visualization
- Configurable FFT length (power of 2 only)
- Field selection and struct parsing for custom binary protocols
- Auto/manual Y-axis scaling
- Adjustable graph refresh rate
- Endianness control for data parsing
- Debug logging

## Requirements
- Qt 5.9 or later (Qt Widgets, Qt Charts, Qt Network modules)
- C++17 compatible compiler
- Tested on Windows 10 (should be portable to Linux/macOS with Qt)

## Build Instructions
1. Install Qt (https://www.qt.io/download)
2. Open the project in Qt Creator or run `qmake` from the command line:
   ```
   qmake
   make
   ```
   or on Windows:
   ```
   qmake
   nmake
   ```
3. Run the resulting executable (`SpectraDAQ` or `SpectraDAQ.exe`)

## Usage
- Set the DAQ device IP and port.
- Paste or type your C struct definition in the struct input area.
- Click "Parse Struct" to update the field table.
- Select the field to plot (only one at a time).
- For array fields, select the array index.
- Use the "Apply FFT" checkbox to switch between time and frequency domain.
- Set FFT Length (must be a power of 2, only applied when you press Enter).
- Adjust X-Div, Y-Div, and refresh rate as needed.
- Use "Auto Y-Scale" for automatic vertical scaling.
- Enable debug log for verbose output.

## Configuration
- All configuration is done via the GUI. No external config files are required.
- The application expects UDP packets matching the struct definition and field selection.
- Endianness can be toggled for compatibility with different DAQ devices.

## Architecture
- **UI:** Qt Widgets, designed in `mainwindow.ui`.
- **Core:** All logic in `mainwindow.cpp` and `mainwindow.h`.
- **UDP Handling:** Uses `QUdpSocket` for non-blocking packet reception.
- **Struct Parsing:** C struct definitions are parsed at runtime to extract field offsets and types.
- **Plotting:** Uses Qt Charts for both time-domain and FFT (magnitude spectrum) visualization.
- **FFT:** Custom radix-2 Cooley-Tukey implementation (single precision, real input).

## Limitations
- Only one field can be plotted at a time.
- FFT length must be a power of 2 and is only applied on Enter.
- No persistent settings; all configuration resets on restart.