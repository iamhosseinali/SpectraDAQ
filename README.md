# SpectraDAQ - DAQ Monitor

## Description
SpectraDAQ is a Qt-based application for real-time monitoring and control of data acquisition (DAQ) devices over UDP. It supports binary struct parsing, high-throughput logging, real-time plotting, FFT analysis, and a customizable command system for device control.

## Features
- UDP communication for sending and receiving packets to/from DAQ hardware.
- C struct parsing: user can paste C struct definitions to interpret incoming binary data.
- Real-time plotting of selected struct fields (time series and FFT modes).
- Preset system: save/load all UI state, including struct, plotting, and custom commands, to/from JSON.
- Custom command system: define and send device-specific commands with flexible formatting.
- High-speed logging: supports logging incoming data to CSV at rates up to 1 Gbps with zero data loss (requires Boost lockfree queue).

## Logging System
- Logging is initiated via the UI. User specifies duration (seconds) and output CSV file.
- During logging, all UI and plotting features are disabled for maximum performance.
- Data is buffered in a lock-free queue and written to disk in batches using a dedicated thread.
- CSV output is type-aware: each struct field is parsed and written as its correct value (signed/unsigned, float, etc.).
- Logging uses all available CPU cores for buffering and writing. UDP receive is single-threaded (Qt limitation).
- Boost (header-only) is required for lockfree queue. Set INCLUDEPATH in the .pro file accordingly.

## Custom Command System
- Two command types: Spinbox (integer value, 1–64 bytes, optional endianness swap) and Button (fixed payload, string or hex).
- Spinbox command options: header (4-byte hex), value size (1–64 bytes), swap endianness (boolean), trailer (4-byte hex).
- Button command options: header, trailer, command payload (string or hex).
- All command options are stored in `presets.json` under the `custom_commands` array for each preset.

### Example: Spinbox Command JSON
```
{
  "name": "Set Threshold",
  "type": "spinbox",
  "header": "0xAABBCCDD",
  "value_size": 4,
  "swap_endian": false,
  "trailer": "0xEEFF0011",
  "command": ""
}
```

### Example: Button Command JSON
```
{
  "name": "Start Acquisition",
  "type": "button",
  "header": "0",
  "value_size": 0,
  "swap_endian": false,
  "trailer": "0",
  "command": "0x12345678"
}
```

## Usage
- Paste C struct definition in the UI to parse incoming UDP data.
- Select fields for plotting or logging.
- Use the "Log to CSV" button to start high-speed logging. Specify duration and output file.
- All UI controls are disabled during logging. Logging is type-aware and lossless (subject to system RAM and disk speed).
- Use the "Edit Commands" button to define custom device commands. Commands are sent over UDP.

## Technical Notes
- Spinbox values are encoded as unsigned integers, using the specified number of bytes. Endianness swap applies only to value bytes.
- QSpinBox UI limit is 2,147,483,647. For value sizes >4 bytes, only the lower bytes of the integer are used.
- All custom command options, including endianness, are saved in `presets.json` and restored with the preset.
- Presets are stored as an array of objects, each with a `custom_commands` array.

## Build Requirements
- Qt 5.x or 6.x (tested with Qt 5.13+)
- Boost (header-only, for lockfree queue)
- C++17 or later
