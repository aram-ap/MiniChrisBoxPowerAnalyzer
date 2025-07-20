# Mini Chris Box V5.2 - User Manual

## Table of Contents
1. [Introduction](#introduction)
2. [Hardware Requirements](#hardware-requirements)
3. [Interface Overview](#interface-overview)
4. [Navigation System](#navigation-system)
5. [Main Screen](#main-screen)
6. [Settings Page](#settings-page)
7. [Script System](#script-system)
8. [Graph System](#graph-system)
9. [Network Features](#network-features)
10. [Data Logging](#data-logging)
11. [Serial Commands](#serial-commands)
12. [Keypad Shortcuts](#keypad-shortcuts)
13. [Troubleshooting](#troubleshooting)

---

## Introduction

The Mini Chris Box V5.2 is a Teensy 4.1-based power analyzer and control system designed for monitoring and controlling up to 6 electrical devices. It features real-time power monitoring, automated script execution, data logging, network connectivity, and comprehensive graphing capabilities.

### Key Features
- **6 Device Channels**: GSE-1, GSE-2, TE-R, TE-1, TE-2, TE-3
- **Real-time Monitoring**: Voltage, current, and power measurement per channel
- **Automated Scripts**: Time-based device control with recording
- **Data Logging**: CSV and JSON format logging to SD card
- **Network Control**: TCP/UDP remote control and monitoring
- **Touch Interface**: 480x320 color touchscreen with physical keypad backup
- **Graph Visualization**: Real-time plotting with multiple data types
- **Safety Features**: Emergency stop, output locking, safety interlocks

---

## Hardware Requirements

### Essential Components
- **Teensy 4.1 microcontroller** (600MHz ARM Cortex-M7)
- **4.3" TFT Display** (480x320, ST7796S controller)
- **Touch Screen** (XPT2046 controller)
- **External SD Card (on enclosure)** (FAT32, **MUST be < 8GB for compatibility**)
- **Internal SD Card** (for script storage)
- **Teensy Ethernet Kit** (optional, for network features)

### Power Requirements
- 12V power supply for device outputs
- USB power for Teensy 4.1 operation

### SD Card Requirements
⚠️ **CRITICAL**: Use SD cards **smaller than 8GB** formatted as FAT32. Larger cards may not be detected properly and will cause the record button to remain gray/disabled.

---

## Interface Overview

### Display Layout
The device uses a **480x320 pixel touchscreen** with the following general layout:
- **Header Area**: Time display, system status
- **Main Content Area**: Context-dependent information
- **Footer Buttons**: Navigation and action buttons
- **Side Panel**: Additional controls (varies by page)

### Status Indicators
- **Power LED**: Solid = Normal operation, Blinking = Recording active
- **Lock LED**: Solid = System locked, Blinking = Script running, Off = Unlocked
- **Stop LED**: Solid = Emergency stop active, Off = Normal operation

---

## Navigation System

### Page Hierarchy
```
Main Screen
├── Settings (D key)
│   ├── Network Settings
│   │   └── Network Edit
│   ├── Date/Time Setup
│   └── About Page
├── Script Page (Star * key)
│   ├── Script Load
│   └── Script Edit (Hash # key)
│       ├── Edit Load
│       ├── Edit Save
│       └── Edit Name
├── Graph Page (A key)
│   ├── Graph Settings
│   └── Graph Display Settings
└── Edit Page (Hash # key from Main)
```

### Universal Navigation
- **B Key**: Back/Cancel (works on most pages)
- **Touch "Back" Button**: Return to previous screen
- **Emergency Stop**: Accessible from all pages via "STOP" button

---

## Main Screen

The main screen provides real-time monitoring and basic device control.

### Navigation to Main Screen
- **Startup**: Automatic after initialization
- **From any page**: Touch "Back" repeatedly or press B key

### Display Elements

#### Time Display
- **Script Not Running**: Current date and time
- **Script Running**: Script time (T+/-seconds from script start)

#### Device Status Table
Each row shows one device (GSE-1, GSE-2, TE-R, TE-1, TE-2, TE-3):
- **Device Name**: Channel identifier
- **Voltage**: Real-time voltage reading (V)
- **Current**: Real-time current reading (A) 
- **Power**: Real-time power reading (W)
- **Row Color**: 
  - Black background = Device OFF
  - Purple background = Device ON

#### Bus Total Row
- Shows combined readings for all devices
- Displays total system voltage, current, and power

### Control Buttons

#### Top Row Buttons
- **RECORD**: Start/stop data logging
  - Green = Ready to record
  - Red = Currently recording  
  - Gray = SD card not available/not detected
  - **Disabled when**: No SD card detected or script running
- **SD**: SD card status indicator
  - Green = SD card available
  - Red = SD card not detected
- **STOP**: Emergency stop toggle
  - Yellow = Normal operation
  - Purple = Emergency stop active

#### Bottom Row Buttons  
- **ALL ON**: Turn on all devices (locks system automatically)
- **ALL OFF**: Turn off all devices (locks system automatically)
- **Script**: Navigate to Script page (Main → Script)
- **Edit**: Navigate to Edit page (Main → Edit)
- **Graph**: Navigate to Graph page (Main → Graph)  
- **Settings**: Navigate to Settings page (Main → Settings)
- **LOCK**: Toggle system lock
  - Yellow = System unlocked (manual control allowed)
  - Red = System locked (manual control disabled)

### Device Control
- **Individual Device Control**: Touch device rows to toggle ON/OFF (when unlocked)
- **Automatic Locking**: ALL ON/ALL OFF buttons automatically lock the system
- **Script Protection**: Manual control disabled during script execution

---

## Settings Page

Configure system parameters and access advanced features.

### Navigation to Settings
- **From Main**: Touch "Settings" button or press D key
- **Path**: Main → Settings

### Settings Options

#### Fan Speed (0-255)
- **Purpose**: Control internal cooling fan speed
- **Range**: 0 (off) to 255 (full speed)
- **Default**: 255 (full speed)
- **Access**: Touch value field to open keypad

#### Update Rate (ms)  
- **Purpose**: Control sensor reading frequency
- **Range**: 50-5000 milliseconds
- **Default**: 100ms
- **Lower values**: More responsive but higher CPU usage

#### RTC Clock
- **Purpose**: Set system date and time
- **Access**: Touch "Set" button → Date/Time page
- **Format**: Configurable 12/24 hour display

#### Time Format Toggle
- **Options**: 12H (12-hour) or 24H (24-hour)
- **Affects**: Time display throughout system

#### Dark Mode Toggle  
- **Options**: ON (dark theme) or OFF (light theme)
- **Default**: ON
- **Affects**: Color scheme across all pages

#### Network Settings
- **Access**: Touch "Network" button → Network page
- **Features**: Ethernet configuration, IP settings, protocol setup

#### About Page
- **Access**: Touch "About" button
- **Contents**: Software version, system information, credits

### Settings Navigation
- **Back**: Return to Main screen
- **STOP**: Emergency stop (available on all pages)

---

## Script System

Automated time-based device control with optional data recording.

### Navigation to Script System
- **From Main**: Touch "Script" button or press * key
- **From Edit**: Press * key
- **Path**: Main → Script

### Script Page Overview

#### Script Information Display
- **Current Script Name**: Name of loaded script
- **Script Parameters**:
  - **Start Time**: When script begins (T+ seconds)
  - **End Time**: When script ends (T+ seconds) 
  - **Record**: Whether data logging is enabled during script
- **Device Table**: Shows ON/OFF times for each device

#### Device Status Panel (Right Side)
- **Real-time Status**: Current ON/OFF state per device
- **Live Readings**: Voltage and current for each active device
- **Color Coding**:
  - Green text = Device ON
  - Red text = Device OFF

#### Script Control Buttons
- **Load**: Open script selection (Script → Script Load)
- **Edit**: Open script editor (Script → Edit)  
- **Start/Pause**: 
  - **Start**: Begin script execution (when stopped)
  - **Pause**: Temporarily pause script (when running)
  - **Resume**: Continue paused script
- **STOP**: Emergency stop (terminates script and unlocks system)

### Script Load Page

#### Navigation
- **From Script**: Touch "Load" button or press * key
- **Path**: Main → Script → Script Load

#### Script Management
- **Script List**: Shows all available scripts with metadata
- **Sort Options**: Toggle between Name, Last Used, Date Created
- **Script Information**:
  - Script name
  - Creation date  
  - Last used date
- **Actions**:
  - **Select**: Touch script name to highlight
  - **Load**: Touch "Select" to load highlighted script
  - **Delete**: Touch "Delete" to remove highlighted script (with confirmation)

#### Script List Navigation
- **Scrolling**: Touch arrows or scroll through list
- **Selection**: Touch script name to highlight
- **Confirmation**: Delete requires confirmation dialog

### Script Execution

#### Automatic Behavior
- **System Lock**: Automatically locks all manual controls
- **Device Control**: Outputs controlled by script timing only
- **Data Recording**: Starts automatically if script has "useRecord" enabled
- **Safety Stop**: Can be activated during script execution

#### Script Timeline
- **Negative Time**: Pre-script preparation phase (T-xx)
- **Positive Time**: Active script execution (T+xx)  
- **Device Events**: Devices turn ON/OFF at specified times
- **End Condition**: Script stops automatically at end time

#### Script States
- **Stopped**: No script running, manual control available
- **Running**: Script executing, automatic control active
- **Paused**: Script paused, devices maintain current state
- **Emergency Stop**: Script terminated, all outputs OFF

---

## Edit Page (Script Editor)

Create and modify automated scripts.

### Navigation to Edit Page
- **From Main**: Touch "Edit" button or press # key
- **From Script**: Touch "Edit" button or press # key
- **Path**: Main → Edit or Main → Script → Edit

### Edit Page Layout

#### Script Configuration (Top Section)
- **Script Name**: Touch to edit name (Edit → Edit Name)
- **Start Time**: Script begin time in seconds (touch to edit)
- **End Time**: Script end time in seconds (touch to edit)  
- **Record**: Toggle data recording during script execution

#### Device Timing Table
For each device (GSE-1, GSE-2, TE-R, TE-1, TE-2, TE-3):
- **ON Time**: When device turns on (seconds from script start)
- **OFF Time**: When device turns off (seconds from script start)
- **Enabled**: Whether device is controlled by this script

#### Edit Controls
- **Load**: Load existing script for editing (Edit → Edit Load)
- **Save**: Save current script (Edit → Edit Save)
- **New**: Create new blank script (resets all fields)
- **STOP**: Emergency stop (available from all pages)

### Script Creation Workflow

1. **Create New Script**
   - Touch "New" button to clear all fields
   - Default name: "Untitled"
   - Default times: Start=0, End=120 seconds

2. **Configure Basic Parameters**
   - Touch script name field to edit name
   - Set start time (typically 0)
   - Set end time (duration in seconds)
   - Enable/disable recording

3. **Configure Device Timing**
   - For each device you want to control:
     - Set ON time (when device should turn on)
     - Set OFF time (when device should turn off)
     - Enable the device (check the box)

4. **Save Script**
   - Touch "Save" button
   - Confirm script name in save dialog
   - Script saved to internal SD card

### Example Script
```
Script Name: "Test Sequence"
Start: 0 seconds
End: 60 seconds  
Record: Yes

GSE-1: ON at 5s, OFF at 25s, Enabled
GSE-2: ON at 10s, OFF at 30s, Enabled
TE-R: ON at 0s, OFF at 60s, Enabled
TE-1: Disabled
TE-2: Disabled  
TE-3: Disabled
```

This script would:
- Start immediately (T=0)
- Turn on TE-R at start
- Turn on GSE-1 at 5 seconds
- Turn on GSE-2 at 10 seconds  
- Turn off GSE-1 at 25 seconds
- Turn off GSE-2 at 30 seconds
- Turn off TE-R and end at 60 seconds
- Record data throughout execution

---

## Graph System

Real-time visualization of voltage, current, and power data.

### Navigation to Graph System
- **From Main**: Touch "Graph" button or press A key
- **Path**: Main → Graph

### Graph Page Layout

#### Graph Tabs (Top)
- **All**: Display multiple devices on one graph
- **GSE1**: Individual graph for GSE-1 device
- **GSE2**: Individual graph for GSE-2 device  
- **TER**: Individual graph for TE-R device
- **TE1**: Individual graph for TE-1 device
- **TE2**: Individual graph for TE-2 device
- **TE3**: Individual graph for TE-3 device

#### Data Type Selection
- **Current** (Red button): Display current readings (Amperes)
- **Voltage** (Blue button): Display voltage readings (Volts)
- **Power** (Magenta button): Display power readings (Watts)

#### Graph Area
- **Real-time Plotting**: Continuous data visualization
- **Grid Lines**: Reference lines for easy reading
- **Color Coding**: Each device has unique color
- **Time Axis**: X-axis shows time progression
- **Value Axis**: Y-axis shows measurement values

#### Information Panel (Right Side)
- **Live Values**: Current readings for all devices
- **Device Status**: ON/OFF state indicators
- **Script Timer**: Current script time (if running)

### Graph Controls

#### Bottom Button Bar
- **Back**: Return to Main screen (Graph → Main)
- **Clear**: Clear all graph data and restart
- **Pause/Resume**: 
  - **Pause**: Stop data collection (preserves current view)
  - **Resume**: Restart data collection
- **Settings**: Open graph configuration (Graph → Graph Settings)
- **Data Type**: Cycle through Current/Voltage/Power
- **STOP**: Emergency stop

### Graph Settings Page

#### Navigation
- **From Graph**: Touch "Settings" button or press D key
- **Path**: Main → Graph → Graph Settings

#### Individual Device Settings
For each device channel:
- **Enable/Disable**: Include device in "All" tab
- **Data Type**: Current/Voltage/Power for this device
- **Y-Axis Range**: Minimum and maximum display values
- **Line Color**: Display color for this device

#### All-Devices Tab Settings
- **Data Type**: What to display when "All" tab selected
- **Device Selection**: Which devices to show simultaneously
- **Y-Axis Range**: Shared scale for all enabled devices
- **Line Thickness**: Width of plot lines

#### Display Settings
- **Time Range**: How much historical data to show (seconds)
- **Auto Scale**: Automatically adjust Y-axis to fit data
- **Show Grid**: Display reference grid lines

### Graph Display Settings Page

#### Navigation
- **From Graph Settings**: Touch "Display" button
- **Path**: Main → Graph → Graph Settings → Graph Display

#### Advanced Display Options
- **Antialiasing**: Smooth line rendering (slower but prettier)
- **Show Grids**: Enable/disable reference grid
- **Max Points**: Data buffer size (affects memory usage)
- **Refresh Rate**: How often graph updates (milliseconds)
- **Interpolation**: Smooth curves between data points
- **Tension**: Curve smoothness factor
- **Curve Scale**: Interpolation curve scaling
- **Subdivision**: Curve detail level

### Graph Data Types

#### Current (Amperes)
- **Range**: Typically 0-10A per channel
- **Accuracy**: ±0.1% with INA226 sensors
- **Units**: Displayed in Amperes (A)

#### Voltage (Volts)  
- **Range**: 0-30V typical
- **Accuracy**: ±0.1% with INA226 sensors
- **Units**: Displayed in Volts (V)

#### Power (Watts)
- **Calculation**: Voltage × Current per channel
- **Range**: 0-100W typical per channel
- **Units**: Displayed in Watts (W)

### Graph Keypad Shortcuts (on Graph page)
- **0-6 Keys**: Switch directly to corresponding tab
- **A Key**: Switch to "All" tab
- **# Key**: Cycle through data types
- **\* Key**: Pause/Resume data collection
- **D Key**: Open Graph Settings
- **C Key**: Clear graph data
- **B Key**: Back to Main screen

---

## Network Features

Remote monitoring and control via Ethernet connection.

### Network Setup

#### Hardware Requirements
- **Ethernet Cable**: Connect to Teensy 4.1 Ethernet port
- **Network Access**: Router/switch with DHCP or static IP capability

#### Network Configuration

##### Navigation to Network Settings
- **Path**: Main → Settings → Network

##### Connection Settings
- **Enable LAN**: Toggle Ethernet functionality ON/OFF
- **Connection Status**: Shows Connected/Disconnected
- **IP Address**: Displays current IP (when connected)
- **DHCP/Static**: Choose automatic or manual IP configuration

##### Advanced Network Configuration
- **Path**: Main → Settings → Network → Edit
- **Static IP Settings**:
  - IP Address (default: 192.168.1.100)
  - Subnet Mask (default: 255.255.255.0)  
  - Gateway (default: 192.168.1.1)
  - DNS Server (default: 8.8.8.8)
- **Port Configuration**:
  - TCP Port (default: 8080)
  - UDP Port (default: 8081)  
- **Timeout Settings**:
  - Network Timeout (default: 10000ms)
  - DHCP Timeout (default: 8000ms)

### Network Protocols

#### TCP Communication (Port 8080)
- **Persistent connections**: Multiple clients supported
- **JSON commands**: Structured command/response
- **Real-time streaming**: Live data feeds

#### UDP Communication (Port 8081)  
- **Single packet**: Request/response per packet
- **Broadcast capable**: Send to multiple listeners
- **Lower latency**: Faster than TCP for simple commands

### Network Commands (JSON Format)

#### Device Control
```json
{"cmd": "set_output", "device": "GSE-1", "state": true}
{"cmd": "all_outputs", "state": false}
```

#### System Control  
```json
{"cmd": "lock_system", "state": true}
{"cmd": "unlock_system"}
{"cmd": "emergency_stop"}
{"cmd": "start_recording"}
{"cmd": "stop_recording"}
```

#### Script Control
```json
{"cmd": "load_script", "name": "test_script"}
{"cmd": "start_script"}
{"cmd": "pause_script"}  
{"cmd": "stop_script"}
```

#### Data Requests
```json
{"cmd": "get_status"}
{"cmd": "get_scripts"}
{"cmd": "start_stream", "interval": 100}
{"cmd": "stop_stream"}
```

#### Network Response Format
```json
{
  "type": "command_response",
  "cmd": "set_output", 
  "success": true,
  "timestamp": "2025-01-14 10:30:45"
}
```

### Network Data Streaming

#### Live Data Stream
- **Format**: JSON objects with real-time sensor data
- **Configurable Rate**: 50-5000ms intervals
- **Selective Streaming**: Active devices only (optional)
- **Multiple Outputs**: USB Serial, TCP, or UDP

#### Example Stream Data
```json
{
  "type": "live_data",
  "timestamp": "2025-01-14 10:30:45",
  "script_running": true,
  "script_time": 45,
  "devices": [
    {
      "name": "GSE-1", 
      "state": true,
      "voltage": 12.34,
      "current": 2.45, 
      "power": 30.23
    }
  ]
}
```

---

## Data Logging

Record system data to SD card for analysis.

### SD Card Requirements
⚠️ **CRITICAL**: Use SD cards **smaller than 8GB** and format as **FAT32**
- **Compatible Sizes**: 128MB - 8GB
- **File System**: FAT32 only
- **Speed Class**: Class 4 or higher recommended

### Recording Controls

#### Starting Recording
- **From Main Screen**: Touch green "RECORD" button
- **From Scripts**: Enable "useRecord" in script configuration
- **Status Indicator**: 
  - Green button = Ready to record
  - Red button = Currently recording
  - Gray button = SD card not available

#### Stopping Recording  
- **Manual**: Touch "RECORD" button when recording
- **Automatic**: Recording stops when script ends
- **Emergency**: STOP button terminates recording

### Data File Formats

#### JSON Format (Default)
- **Filename**: `power_data_YYYYMMDD_HHMMSS.json`
- **Structure**: Timestamped array of device readings
- **Advantages**: Self-describing, easy to parse

#### CSV Format
- **Activation**: Serial command `csv on`
- **Filename**: `power_data_YYYYMMDD_HHMMSS.csv`  
- **Structure**: Columnar data with headers
- **Advantages**: Excel-compatible, smaller files

#### Script-Based Filenames
When recording triggered by script:
- **Format**: `scriptname_YYYYMMDD_HHMMSS.json`
- **Example**: `test_sequence_20250114_103045.json`

### Data Content

#### Per-Device Data (every log interval)
- **Timestamp**: Milliseconds since system start
- **Device State**: ON/OFF status
- **Voltage**: Real-time voltage reading (V)
- **Current**: Real-time current reading (mA)
- **Power**: Real-time power reading (W)

#### System Metadata
- **Recording Start**: System timestamp
- **Script Information**: Name, start/end times (if applicable)
- **Device Configuration**: Which devices were active
- **Sensor Configuration**: Update rates, calibration

#### Example JSON Log Entry
```json
{
  "timestamp": 123456789,
  "devices": [
    {
      "name": "GSE-1",
      "state": true,
      "voltage": 12.34,
      "current": 2450.0,
      "power": 30.23
    }
  ],
  "script_time": 45,
  "script_running": true
}
```

### File Management

#### Automatic Naming
- **Timestamp-based**: Files never overwrite
- **Script-aware**: Script name included when applicable
- **Sequential numbering**: If timestamp collision occurs

#### Storage Locations
- **Data Files**: External SD card (removable)
- **Script Files**: Internal SD card (permanent)
- **Separation**: Prevents script loss when changing data SD cards

---

## Serial Commands

Control system via USB serial connection at 2,000,000 baud.

### Connection Setup
- **Baud Rate**: 2,000,000 (2 Mbaud)
- **Data Bits**: 8
- **Parity**: None  
- **Stop Bits**: 1
- **Flow Control**: None

### Device Control Commands

#### Individual Device Control
```
gse1 on       # Turn on GSE-1
gse1 off      # Turn off GSE-1
gse2 on       # Turn on GSE-2  
gse2 off      # Turn off GSE-2
ter on        # Turn on TE-R
ter off       # Turn off TE-R
te1 on        # Turn on TE-1
te1 off       # Turn off TE-1
te2 on        # Turn on TE-2
te2 off       # Turn off TE-2
te3 on        # Turn on TE-3
te3 off       # Turn off TE-3
```

### System Control Commands

#### Lock/Unlock System
```
lock          # Lock all outputs (disable manual control)
unlock        # Unlock outputs (enable manual control)
```

#### Data Logging
```
start log     # Begin data recording  
stop log      # End data recording
```

#### System Maintenance
```
refresh sd    # Manually check SD card status
get temp      # Read internal temperature sensor
```

### Output Format Commands

#### CSV vs Human Readable
```
csv on        # Enable CSV output format
csv off       # Enable human readable format  
```

#### Status Information
```
status        # Show complete system status
help          # Display available commands
```

### Network Commands (JSON)

#### Send JSON commands via serial for network testing:
```json
{"cmd":"get_status"}
{"cmd":"start_stream","interval":100}
{"cmd":"set_output","device":"GSE-1","state":true}
```

### Status Command Output

#### Human Readable Format
```
=== Current Status ===
System Lock: UNLOCKED
Safety Stop: INACTIVE  
Recording: ACTIVE
Script Running: YES
Output Format: Human Readable
Dark Mode: ON
External SD: Available
Internal SD: Available
Ethernet Enabled: YES
Ethernet Connected: YES
IP Address: 192.168.1.100
TCP Port: 8080
UDP Port: 8081

GSE-1: ON | V=12.34V | I=2.450A | P=30.230W
GSE-2: OFF | V=0.00V | I=0.000A | P=0.000W
TE-R: ON | V=24.56V | I=1.200A | P=29.472W
TE-1: OFF | V=0.00V | I=0.000A | P=0.000W  
TE-2: OFF | V=0.00V | I=0.000A | P=0.000W
TE-3: OFF | V=0.00V | I=0.000A | P=0.000W
===================
```

#### CSV Format
```
Time,GSE-1_State,GSE-1_Voltage,GSE-1_Current,GSE-1_Power,GSE-2_State,GSE-2_Voltage,GSE-2_Current,GSE-2_Power...
123456,1,12.34,2.450,30.230,0,0.00,0.000,0.000...
```

---

## Keypad Shortcuts

Physical keypad provides backup control when touchscreen is unavailable.

### Global Shortcuts (Work on Most Pages)
- **B Key**: Back/Cancel - Return to previous page
- **A Key**: 
  - **From Main**: Go to Graph page
  - **From Script**: Start/Pause/Resume script
- **D Key**: 
  - **From Main**: Go to Settings page  
  - **From Graph**: Go to Graph Settings
- **\* Key**:
  - **From Main**: Go to Script page
  - **From Graph**: Pause/Resume data collection
- **# Key**:
  - **From Main**: Go to Edit page
  - **From Script**: Go to Edit page
  - **From Graph**: Cycle data types

### Main Screen Shortcuts
- **\* Key**: Navigate to Script page (Main → Script)
- **# Key**: Navigate to Edit page (Main → Edit)  
- **A Key**: Navigate to Graph page (Main → Graph)
- **D Key**: Navigate to Settings page (Main → Settings)

### Script Page Shortcuts
- **\* Key**: Load script (Script → Script Load)
- **# Key**: Edit script (Script → Edit)
- **A Key**: Start/Pause/Resume script execution

### Graph Page Shortcuts
- **0-6 Keys**: Switch directly to graph tabs
  - **0**: All devices tab
  - **1**: GSE-1 tab
  - **2**: GSE-2 tab  
  - **3**: TE-R tab
  - **4**: TE-1 tab
  - **5**: TE-2 tab
  - **6**: TE-3 tab
- **A Key**: Switch to "All" devices tab
- **# Key**: Cycle through Current/Voltage/Power data types
- **\* Key**: Pause/Resume data collection
- **D Key**: Open Graph Settings (Graph → Graph Settings)
- **C Key**: Clear all graph data

### Keypad Input Pages

#### Numeric Entry
- **0-9 Keys**: Enter digits
- **\* Key**: Backspace (delete last character)
- **# Key**: Toggle positive/negative sign
- **A Key**: Enter/Confirm value
- **B Key**: Cancel/Back
- **C Key**: Clear entire field
- **D Key**: Decimal point

#### Text Entry (Script Names)
- **0-9 Keys**: Multi-tap text entry (T9-style)
- **\* Key**: Clear field
- **A Key**: Confirm text
- **B Key**: Cancel/Back
- **# Key**: Switch between alpha/numeric modes

#### T9 Text Entry Reference
- **2 Key**: ABC (tap once=A, twice=B, three times=C)
- **3 Key**: DEF
- **4 Key**: GHI  
- **5 Key**: JKL
- **6 Key**: MNO
- **7 Key**: PQRS
- **8 Key**: TUV
- **9 Key**: WXYZ
- **0 Key**: Space

---

## Troubleshooting

### SD Card Issues

#### Record Button Stays Gray
**Problem**: Cannot start recording, button disabled
**Causes**:
- SD card not inserted
- SD card > 8GB (too large)
- SD card corrupted or wrong format
- Hardware connection issue

**Solutions**:
1. Check SD card is properly inserted
2. Verify SD card is < 8GB and formatted as FAT32
3. Try different SD card
4. Touch "SD" button to refresh status
5. Use serial command `refresh sd`

#### SD Card Not Detected
**Problem**: SD status shows red/unavailable
**Solutions**:
1. Remove and reinsert SD card
2. Format as FAT32 on computer
3. Check card size (must be < 8GB)
4. Clean card contacts
5. Try different SD card

### Network Issues

#### Ethernet Not Connected
**Problem**: Network status shows "Disconnected"
**Solutions**:
1. Check Ethernet cable connection
2. Verify cable is not damaged
3. Check router/switch is powered
4. Try different Ethernet cable
5. Restart device

#### Cannot Access Network Features  
**Problem**: Network commands fail or timeout
**Solutions**:
1. Verify IP address assignment (check Settings → Network)
2. Check firewall settings on computer
3. Try static IP instead of DHCP
4. Ping device IP address from computer
5. Check TCP/UDP port settings (default 8080/8081)

### Touch Screen Issues

#### Touch Not Responding
**Problem**: Touch screen unresponsive
**Solutions**:
1. Clean screen with soft cloth
2. Use physical keypad for navigation
3. Restart device
4. Check for interference from other devices

#### Touch Calibration Off
**Problem**: Touches register in wrong location
**Solutions**:
1. Note: Touch calibration is hardcoded in firmware
2. Use keypad for navigation instead
3. Contact developer for calibration adjustment

### Script Issues

#### Script Won't Start
**Problem**: Script start button has no effect
**Causes**:
- System in emergency stop mode
- System locked
- Script file corrupted

**Solutions**:
1. Check STOP button is not active (should be yellow, not purple)
2. Unlock system if locked
3. Try loading different script
4. Create new script to test

#### Script Stops Unexpectedly
**Problem**: Script terminates before end time
**Causes**:
- Emergency stop activated
- Power loss
- Hardware fault

**Solutions**:
1. Check system logs via serial
2. Verify power supply stability
3. Check for loose connections
4. Monitor serial output during script execution

### Performance Issues

#### Slow Response/Lagging
**Problem**: Interface sluggish or delayed
**Causes**:
- Low update rate setting
- Network congestion
- SD card write issues

**Solutions**:
1. Increase update rate in Settings (reduce ms value)
2. Pause unnecessary network streaming
3. Check SD card write speed
4. Restart device

#### Memory Issues
**Problem**: System crashes or resets
**Solutions**:
1. Reduce graph max points in Graph Display Settings
2. Clear graph data regularly
3. Avoid extremely high update rates
4. Restart device periodically

### Safety System Issues

#### Cannot Control Devices
**Problem**: Device controls have no effect
**Causes**:
- System locked
- Emergency stop active
- Script running
- Hardware fault

**Solutions**:
1. Check system lock status (should be unlocked for manual control)
2. Ensure STOP button is not active
3. Stop any running scripts
4. Check physical switch positions
5. Verify power supply to outputs

#### Emergency Stop Won't Release
**Problem**: Cannot exit emergency stop mode
**Solutions**:
1. Touch STOP button to toggle off
2. Use serial command interface
3. Check for stuck physical switches
4. Restart device if necessary

### Getting Help

#### Serial Diagnostics
1. Connect via USB serial (2,000,000 baud)
2. Type `status` for complete system information
3. Type `help` for available commands
4. Monitor serial output during problem reproduction

#### System Information
- **Software Version**: Mini Chris Box V5.2
- **Hardware**: Teensy 4.1 (600MHz ARM Cortex-M7)
- **Memory**: 512KB RAM1, 512KB RAM2
- **Storage**: Flash + dual SD cards

#### Reset Procedures
1. **Soft Reset**: Use STOP button and restart
2. **Hard Reset**: Power cycle device
3. **Factory Reset**: Reflash firmware (requires development setup)

---

## *Secret*

There’s something special hidden within your power analyzer for those who enjoy a bit of exploration.

Hint: Hidden secrets are often unlocked by simple sequences. Visit __Settings → About__ and carefully experiment with your keypad. A certain numeric progression combined with one letter will reveal a secret button, unlocking a timeless arcade game.

How to unlock: Begin your search with simplicity—perhaps counting upward and then choosing the first letter available. Watch closely; the secret button is subtle and appears only when the right keys are pressed.

Note: This hidden feature is just for fun and doesn’t affect the main functionality of your device.

---

## Appendix

### Device Specifications

#### Electrical Monitoring
- **Channels**: 6 independent + 1 bus total
- **Voltage Range**: 0-30V per channel
- **Current Range**: 0-8A per channel (with 0.01Ω shunt)
- **Power Calculation**: V × I per channel
- **Accuracy**: ±0.1% (INA226 sensor specification)
- **Update Rate**: 50-5000ms configurable

#### Control Outputs
- **Channels**: 6 independent relay/switch outputs
- **Rating**: Depends on connected switching hardware
- **Safety**: Emergency stop, system lock, safety interlocks
- **Manual Override**: Physical switch backup (when unlocked)

#### Data Storage
- **External SD**: Data logging (removable, < 8GB, FAT32)
- **Internal SD**: Script storage (permanent, built-in)
- **File Formats**: JSON (default), CSV (optional)
- **Automatic Naming**: Timestamp and script-based filenames

#### Network Capabilities
- **Protocol**: Ethernet (10/100 Mbps)
- **Configuration**: DHCP or static IP
- **Services**: TCP server (port 8080), UDP (port 8081)
- **Features**: Remote control, live streaming, status monitoring
- **Security**: Basic authentication via command structure

#### Performance
- **CPU**: 600MHz ARM Cortex-M7 (Teensy 4.1)
- **Memory**: 512KB RAM1 + 512KB RAM2
- **Display**: 480×320 color TFT with resistive touch
- **Update Rates**: 50ms sensor, 170ms display, configurable logging
- **Graph Buffer**: Up to 900 data points per channel per data type

### Quick Reference Card

#### Essential Button Functions
- **RECORD**: Start/stop data logging (green=ready, red=active, gray=no SD)
- **STOP**: Emergency stop toggle (yellow=normal, purple=stopped)  
- **LOCK**: System lock toggle (yellow=unlocked, red=locked)
- **ALL ON/OFF**: Control all devices simultaneously (auto-locks system)

#### Navigation Quick Keys
- **B**: Back/Cancel (universal)
- **A**: Graph page (from Main), Start script (from Script)
- **D**: Settings page (from Main), Graph settings (from Graph)
- **\***: Script page (from Main), Load script (from Script), Pause graph (from Graph)
- **#**: Edit page (from Main/Script), Cycle data type (from Graph)

#### Critical Safety Notes
- **Emergency Stop**: Always available via STOP button or serial command
- **System Lock**: Prevents manual control during scripts or when explicitly locked
- **SD Card Size**: Must be < 8GB and FAT32 format for proper operation
- **Power Supply**: Ensure stable 12V for outputs, USB for control system
- **Physical Switches**: Provide backup control when system unlocked

---

*This manual covers Mini Chris Box V5.2 with Network and Graph capabilities. For technical support or firmware updates, contact Aram Aprahamian, aram@apra.dev* 