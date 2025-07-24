# SpectraDAQ - DAQ Monitor

## Overview
SpectraDAQ is a Qt-based application for real-time monitoring and control of DAQ (Data Acquisition) devices over UDP. It features flexible struct parsing, real-time plotting, FFT analysis, and a powerful custom command system for device control.

---

## Features
- **UDP Communication**: Send and receive UDP packets to/from DAQ devices.
- **Struct Parsing**: Paste C struct definitions to parse incoming binary data.
- **Real-Time Plotting**: Visualize selected struct fields as time series or FFT.
- **Presets**: Save/load all UI state, including struct, plotting, and custom commands, to/from JSON.
- **Custom Commands**: Define and send device-specific commands with flexible formatting.

---

## Custom Command System

### Types
- **Spinbox**: Command with a user-settable integer value (0 to 2,147,483,647, up to 64 bytes encoded).
- **Button**: Command with a fixed payload (string or hex).

### Spinbox Command Options
- **Header**: Optional 4-byte (hex) prefix.
- **Value Size**: 1–64 bytes. Value is encoded as unsigned integer, little-endian by default.
- **Swap Endianness (Value Only)**: If checked, the value bytes are reversed (endianness swapped) before sending. Header and trailer are unaffected.
- **Trailer**: Optional 4-byte (hex) suffix.

### Example: Spinbox Command JSON
```json
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
- `swap_endian` is a boolean. If true, the value bytes are reversed before sending.
- All fields are saved in the `presets.json` file under the `custom_commands` array for each preset.

### Example: Button Command JSON
```json
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

---

## How to Add/Edit Custom Commands
1. Click **Edit Commands**.
2. Add or edit a command:
   - For **Spinbox**: Set header, value size (1–64), trailer, and optionally enable swap endianness.
   - For **Button**: Enter the command payload (string or hex).
3. Save. The command will appear in the main UI for the current preset.

---

## Technical Notes
- **Value Encoding**: Spinbox values are encoded as unsigned integers, using the specified number of bytes. If swap endianness is enabled, only the value bytes are reversed.
- **Limits**: The UI spinbox supports values up to 2,147,483,647 (due to QSpinBox limits). For value sizes >4 bytes, only the lower bytes of the integer are used.
- **Persistence**: All custom command options, including swap endianness, are saved in `presets.json` and restored with the preset.

---

## File: `presets.json`
- Presets are stored as an array of objects, each with a `custom_commands` array.
- Each custom command object includes all fields described above.

---

## Changelog
- **vNext**: Added support for up to 64-byte spinbox values and per-command value endianness swapping.