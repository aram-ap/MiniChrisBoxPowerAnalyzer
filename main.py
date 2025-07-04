import json
import sys
import os
import socket
import threading
import queue
import time
from pathlib import Path
from datetime import datetime
import pyqtgraph as pg
from pyqtgraph.Qt import QtWidgets, QtCore, QtGui
import numpy as np
import re
from collections import deque
import traceback

# Optional serial import
try:
    import serial
    from serial.tools import list_ports

    SERIAL_AVAILABLE = True
except ImportError:
    SERIAL_AVAILABLE = False

# Enable OpenGL for better performance
try:
    import OpenGL.GL as gl

    pg.setConfigOptions(useOpenGL=True, antialias=True, enableExperimental=True)
    OPENGL_AVAILABLE = True
except ImportError:
    pg.setConfigOptions(antialias=True)
    OPENGL_AVAILABLE = False

# Optional imports for Excel export
try:
    import pandas as pd

    PANDAS_AVAILABLE = True
except ImportError:
    PANDAS_AVAILABLE = False

try:
    import openpyxl

    OPENPYXL_AVAILABLE = True
except ImportError:
    OPENPYXL_AVAILABLE = False

# Compatibility for pen style
try:
    dash_style = QtCore.Qt.PenStyle.DashLine  # PyQt6/PySide6
except AttributeError:
    dash_style = QtCore.Qt.DashLine  # PyQt5/PySide2

# Predefined device list from Teensy specification
DEVICES = ["GSE-1", "GSE-2", "TE-R", "TE-1", "TE-2", "TE-3"]
DATA_TYPES = ["volt", "curr", "pow", "stat"]

# Device name mapping for Teensy commands (display name -> command name)
DEVICE_COMMAND_MAP = {
    "GSE-1": "gse1",
    "GSE-2": "gse2",
    "TE-R": "ter",
    "TE-1": "te1",
    "TE-2": "te2",
    "TE-3": "te3"
}

# Reverse mapping for data parsing (command name -> display name)
DEVICE_DISPLAY_MAP = {v: k for k, v in DEVICE_COMMAND_MAP.items()}

# Default colors for devices
DEFAULT_DEVICE_COLORS = {
    'volt': [(31, 119, 180), (255, 127, 14), (44, 160, 44), (214, 39, 40), (148, 103, 189), (140, 86, 75)],
    'curr': [(255, 127, 14), (44, 160, 44), (214, 39, 40), (148, 103, 189), (140, 86, 75), (31, 119, 180)],
    'pow': [(44, 160, 44), (214, 39, 40), (148, 103, 189), (140, 86, 75), (31, 119, 180), (255, 127, 14)],
    'stat': [(214, 39, 40), (148, 103, 189), (140, 86, 75), (31, 119, 180), (255, 127, 14), (44, 160, 44)]
}

# FIXED: Better default Y-axis ranges
DEFAULT_Y_RANGES = {
    'volt': (0, 35),  # 0-35V range
    'curr': (0, 5),  # 0-5A range
    'pow': (0, 100),  # 0-100W range
    'stat': (-0.1, 1.1)  # Status range with padding
}


class DebugConsole(QtWidgets.QDialog):
    """Debug console for monitoring and sending commands"""

    def __init__(self, parent=None, teensy_controller=None):
        super().__init__(parent)
        self.teensy = teensy_controller
        self.setWindowTitle("Debug Console")
        self.setModal(False)
        self.resize(800, 600)
        self.auto_scroll = True

        layout = QtWidgets.QVBoxLayout(self)

        # Output area
        self.output_text = QtWidgets.QTextEdit()
        self.output_text.setReadOnly(True)
        self.output_text.setFont(QtGui.QFont("Consolas", 10))
        layout.addWidget(self.output_text)

        # Command input
        input_layout = QtWidgets.QHBoxLayout()
        self.command_input = QtWidgets.QLineEdit()
        self.command_input.setPlaceholderText("Enter JSON command or text command...")
        self.send_btn = QtWidgets.QPushButton("Send")

        self.command_input.returnPressed.connect(self.send_command)
        self.send_btn.clicked.connect(self.send_command)

        input_layout.addWidget(self.command_input)
        input_layout.addWidget(self.send_btn)
        layout.addLayout(input_layout)

        # Control buttons
        control_layout = QtWidgets.QHBoxLayout()
        self.clear_btn = QtWidgets.QPushButton("Clear")
        self.auto_scroll_cb = QtWidgets.QCheckBox("Auto-scroll")
        self.auto_scroll_cb.setChecked(True)

        self.clear_btn.clicked.connect(self.output_text.clear)
        self.auto_scroll_cb.stateChanged.connect(self.toggle_auto_scroll)

        control_layout.addWidget(self.clear_btn)
        control_layout.addWidget(self.auto_scroll_cb)
        control_layout.addStretch()
        layout.addLayout(control_layout)

        # Redirect stdout to capture print statements
        self.original_stdout = sys.stdout
        sys.stdout = self

    def toggle_auto_scroll(self):
        """Toggle auto-scroll"""
        self.auto_scroll = self.auto_scroll_cb.isChecked()

    def write(self, text):
        """Capture stdout output"""
        if text.strip():
            self.append_output(text.strip())
        self.original_stdout.write(text)

    def flush(self):
        """Required for stdout redirection"""
        self.original_stdout.flush()

    def append_output(self, text):
        """Add text to output area"""
        timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        self.output_text.append(f"[{timestamp}] {text}")

        # Auto-scroll to bottom if enabled
        if self.auto_scroll:
            scrollbar = self.output_text.verticalScrollBar()
            scrollbar.setValue(scrollbar.maximum())

    def send_command(self):
        """Send command to Teensy"""
        command = self.command_input.text().strip()
        if not command:
            return

        self.append_output(f">>> {command}")

        if self.teensy and self.teensy.connected:
            try:
                # Try to parse as JSON first
                if command.startswith('{'):
                    cmd_dict = json.loads(command)
                    success = self.teensy.send_command(cmd_dict)
                else:
                    # Send as raw text (legacy commands)
                    if self.teensy.connection_type == "serial":
                        self.teensy.serial_port.write((command + '\n').encode('utf-8'))
                        success = True
                    else:
                        self.append_output("Raw text commands only supported via serial")
                        success = False

                if success:
                    self.append_output("Command sent successfully")
                else:
                    self.append_output("Failed to send command")

            except json.JSONDecodeError:
                self.append_output("Invalid JSON format")
            except Exception as e:
                self.append_output(f"Error: {str(e)}")
        else:
            self.append_output("Not connected to Teensy")

        self.command_input.clear()

    def closeEvent(self, event):
        """Restore stdout on close"""
        sys.stdout = self.original_stdout
        event.accept()


class SettingsDialog(QtWidgets.QDialog):
    """Settings configuration dialog"""

    def __init__(self, parent=None, settings=None):
        super().__init__(parent)
        self.settings = settings
        self.setWindowTitle("Settings")
        self.setModal(True)
        self.resize(500, 700)

        layout = QtWidgets.QVBoxLayout(self)

        # Create tabs
        self.tab_widget = QtWidgets.QTabWidget()
        layout.addWidget(self.tab_widget)

        # Data tab
        self.create_data_tab()

        # Display tab
        self.create_display_tab()

        # Colors tab
        self.create_colors_tab()

        # Connection tab
        self.create_connection_tab()

        # Buttons
        button_layout = QtWidgets.QHBoxLayout()
        self.ok_btn = QtWidgets.QPushButton("OK")
        self.cancel_btn = QtWidgets.QPushButton("Cancel")
        self.apply_btn = QtWidgets.QPushButton("Apply")

        self.ok_btn.clicked.connect(self.accept)
        self.cancel_btn.clicked.connect(self.reject)
        self.apply_btn.clicked.connect(self.apply_settings)

        button_layout.addStretch()
        button_layout.addWidget(self.ok_btn)
        button_layout.addWidget(self.cancel_btn)
        button_layout.addWidget(self.apply_btn)
        layout.addLayout(button_layout)

        self.load_settings()

    def create_data_tab(self):
        """Create data settings tab"""
        data_widget = QtWidgets.QWidget()
        layout = QtWidgets.QFormLayout(data_widget)

        # Polling rate
        self.polling_rate_spin = QtWidgets.QSpinBox()
        self.polling_rate_spin.setRange(50, 5000)
        self.polling_rate_spin.setSuffix(" ms")
        layout.addRow("Polling Rate:", self.polling_rate_spin)

        # Max data points
        self.max_points_spin = QtWidgets.QSpinBox()
        self.max_points_spin.setRange(100, 100000)
        layout.addRow("Max Data Points:", self.max_points_spin)

        # Data handling mode
        self.data_mode_combo = QtWidgets.QComboBox()
        self.data_mode_combo.addItems(["Scroll (Remove Old)", "Keep All (Allow Scrolling)"])
        layout.addRow("Data Overflow Mode:", self.data_mode_combo)

        # Window mode
        window_group = QtWidgets.QGroupBox("Display Window Mode")
        window_layout = QtWidgets.QFormLayout(window_group)

        self.window_mode_combo = QtWidgets.QComboBox()
        self.window_mode_combo.addItems(["Growing Window", "Sliding Time Window"])
        window_layout.addRow("Window Mode:", self.window_mode_combo)

        self.window_max_points_spin = QtWidgets.QSpinBox()
        self.window_max_points_spin.setRange(-1, 100000)
        self.window_max_points_spin.setSpecialValueText("No Limit")
        window_layout.addRow("Growing Window Max Points:", self.window_max_points_spin)

        self.sliding_window_time_spin = QtWidgets.QDoubleSpinBox()
        self.sliding_window_time_spin.setRange(1.0, 300.0)
        self.sliding_window_time_spin.setSuffix(" seconds")
        window_layout.addRow("Sliding Window Duration:", self.sliding_window_time_spin)

        layout.addRow(window_group)

        # Analysis update rate
        self.analysis_update_spin = QtWidgets.QSpinBox()
        self.analysis_update_spin.setRange(100, 10000)
        self.analysis_update_spin.setSuffix(" ms")
        layout.addRow("Analysis Update Rate:", self.analysis_update_spin)

        # Data filtering
        filter_group = QtWidgets.QGroupBox("Data Filtering")
        filter_layout = QtWidgets.QFormLayout(filter_group)

        self.enable_filtering_cb = QtWidgets.QCheckBox()
        filter_layout.addRow("Enable Filtering:", self.enable_filtering_cb)

        self.moving_avg_spin = QtWidgets.QSpinBox()
        self.moving_avg_spin.setRange(1, 100)
        filter_layout.addRow("Moving Average Window:", self.moving_avg_spin)

        self.interpolation_cb = QtWidgets.QCheckBox()
        filter_layout.addRow("Enable Interpolation:", self.interpolation_cb)

        layout.addRow(filter_group)

        self.tab_widget.addTab(data_widget, "Data")

    def create_display_tab(self):
        """Create display settings tab"""
        display_widget = QtWidgets.QWidget()
        layout = QtWidgets.QFormLayout(display_widget)

        # Auto-resize behavior
        self.auto_resize_cb = QtWidgets.QCheckBox()
        layout.addRow("Auto-resize Plots:", self.auto_resize_cb)

        # Line thickness
        self.line_thickness_spin = QtWidgets.QSpinBox()
        self.line_thickness_spin.setRange(1, 10)
        layout.addRow("Line Thickness:", self.line_thickness_spin)

        # Grid settings
        self.show_grid_cb = QtWidgets.QCheckBox()
        layout.addRow("Show Grid:", self.show_grid_cb)

        # Crosshair settings
        self.enable_crosshair_cb = QtWidgets.QCheckBox()
        layout.addRow("Enable Crosshair:", self.enable_crosshair_cb)

        # Crosshair label visibility
        self.show_crosshair_label_cb = QtWidgets.QCheckBox()
        layout.addRow("Show Crosshair Label:", self.show_crosshair_label_cb)

        # Y-axis range settings
        y_range_group = QtWidgets.QGroupBox("Default Y-Axis Ranges")
        y_range_layout = QtWidgets.QFormLayout(y_range_group)

        self.y_range_spins = {}
        for data_type in DATA_TYPES:
            min_spin = QtWidgets.QDoubleSpinBox()
            max_spin = QtWidgets.QDoubleSpinBox()
            min_spin.setRange(-1000, 1000)
            max_spin.setRange(-1000, 1000)

            range_layout = QtWidgets.QHBoxLayout()
            range_layout.addWidget(QtWidgets.QLabel("Min:"))
            range_layout.addWidget(min_spin)
            range_layout.addWidget(QtWidgets.QLabel("Max:"))
            range_layout.addWidget(max_spin)

            self.y_range_spins[data_type] = {'min': min_spin, 'max': max_spin}
            y_range_layout.addRow(f"{self.format_type_name(data_type)}:", range_layout)

        layout.addRow(y_range_group)

        self.tab_widget.addTab(display_widget, "Display")

    def create_colors_tab(self):
        """Create color settings tab"""
        colors_widget = QtWidgets.QWidget()
        layout = QtWidgets.QVBoxLayout(colors_widget)

        # Device colors
        device_group = QtWidgets.QGroupBox("Device Colors")
        device_layout = QtWidgets.QGridLayout(device_group)

        self.color_buttons = {}
        for i, device in enumerate(DEVICES):
            label = QtWidgets.QLabel(device)
            color_btn = QtWidgets.QPushButton()
            color_btn.setFixedSize(50, 30)
            color_btn.clicked.connect(lambda checked, d=device: self.choose_color(d))

            device_layout.addWidget(label, i, 0)
            device_layout.addWidget(color_btn, i, 1)

            self.color_buttons[device] = color_btn

        layout.addWidget(device_group)
        layout.addStretch()

        self.tab_widget.addTab(colors_widget, "Colors")

    def create_connection_tab(self):
        """Create connection settings tab"""
        conn_widget = QtWidgets.QWidget()
        layout = QtWidgets.QFormLayout(conn_widget)

        # Serial baud rate
        self.baud_rate_combo = QtWidgets.QComboBox()
        self.baud_rate_combo.setEditable(True)
        self.baud_rate_combo.addItems(
            ["9600", "19200", "38400", "57600", "115200", "230400", "460800", "921600", "2000000"])
        layout.addRow("Serial Baud Rate:", self.baud_rate_combo)

        self.tab_widget.addTab(conn_widget, "Connection")

    def format_type_name(self, type_name):
        """Format type names for display"""
        type_mapping = {
            "volt": "Voltage",
            "curr": "Current",
            "pow": "Power",
            "stat": "Status"
        }
        return type_mapping.get(type_name, type_name.title())

    def choose_color(self, device):
        """Choose color for device"""
        current_color = self.color_buttons[device].palette().button().color()
        color = QtWidgets.QColorDialog.getColor(current_color, self)
        if color.isValid():
            self.color_buttons[device].setStyleSheet(f"background-color: {color.name()}")

    def load_settings(self):
        """Load current settings"""
        if not self.settings:
            return

        # Data settings
        self.polling_rate_spin.setValue(self.settings.value("polling_rate", 1000, int))
        self.max_points_spin.setValue(self.settings.value("max_points", 10000, int))
        self.data_mode_combo.setCurrentIndex(self.settings.value("data_mode", 0, int))
        self.analysis_update_spin.setValue(self.settings.value("analysis_update_rate", 2000, int))

        # Window settings
        self.window_mode_combo.setCurrentIndex(self.settings.value("window_mode", 0, int))
        self.window_max_points_spin.setValue(self.settings.value("window_max_points", -1, int))
        self.sliding_window_time_spin.setValue(self.settings.value("sliding_window_time", 10.0, float))

        # Filtering
        self.enable_filtering_cb.setChecked(self.settings.value("enable_filtering", False, bool))
        self.moving_avg_spin.setValue(self.settings.value("moving_avg_window", 5, int))
        self.interpolation_cb.setChecked(self.settings.value("enable_interpolation", False, bool))

        # Display settings
        self.auto_resize_cb.setChecked(self.settings.value("auto_resize", True, bool))
        self.line_thickness_spin.setValue(self.settings.value("line_thickness", 2, int))
        self.show_grid_cb.setChecked(self.settings.value("show_grid", True, bool))
        self.enable_crosshair_cb.setChecked(self.settings.value("enable_crosshair", True, bool))
        self.show_crosshair_label_cb.setChecked(self.settings.value("show_crosshair_label", True, bool))

        # Y-axis ranges
        for data_type in DATA_TYPES:
            default_range = DEFAULT_Y_RANGES[data_type]
            min_val = self.settings.value(f"y_range_{data_type}_min", default_range[0], float)
            max_val = self.settings.value(f"y_range_{data_type}_max", default_range[1], float)
            self.y_range_spins[data_type]['min'].setValue(min_val)
            self.y_range_spins[data_type]['max'].setValue(max_val)

        # Connection settings
        self.baud_rate_combo.setCurrentText(self.settings.value("serial_baud_rate", "2000000"))

        # Colors
        for i, device in enumerate(DEVICES):
            default_color = DEFAULT_DEVICE_COLORS['volt'][i % len(DEFAULT_DEVICE_COLORS['volt'])]
            color_str = self.settings.value(f"device_color_{device}",
                                            f"#{default_color[0]:02x}{default_color[1]:02x}{default_color[2]:02x}")
            self.color_buttons[device].setStyleSheet(f"background-color: {color_str}")

    def apply_settings(self):
        """Apply settings without closing"""
        if not self.settings:
            return

        # Data settings
        self.settings.setValue("polling_rate", self.polling_rate_spin.value())
        self.settings.setValue("max_points", self.max_points_spin.value())
        self.settings.setValue("data_mode", self.data_mode_combo.currentIndex())
        self.settings.setValue("analysis_update_rate", self.analysis_update_spin.value())

        # Window settings
        self.settings.setValue("window_mode", self.window_mode_combo.currentIndex())
        self.settings.setValue("window_max_points", self.window_max_points_spin.value())
        self.settings.setValue("sliding_window_time", self.sliding_window_time_spin.value())

        # Filtering
        self.settings.setValue("enable_filtering", self.enable_filtering_cb.isChecked())
        self.settings.setValue("moving_avg_window", self.moving_avg_spin.value())
        self.settings.setValue("enable_interpolation", self.interpolation_cb.isChecked())

        # Display settings
        self.settings.setValue("auto_resize", self.auto_resize_cb.isChecked())
        self.settings.setValue("line_thickness", self.line_thickness_spin.value())
        self.settings.setValue("show_grid", self.show_grid_cb.isChecked())
        self.settings.setValue("enable_crosshair", self.enable_crosshair_cb.isChecked())
        self.settings.setValue("show_crosshair_label", self.show_crosshair_label_cb.isChecked())

        # Y-axis ranges
        for data_type in DATA_TYPES:
            self.settings.setValue(f"y_range_{data_type}_min", self.y_range_spins[data_type]['min'].value())
            self.settings.setValue(f"y_range_{data_type}_max", self.y_range_spins[data_type]['max'].value())

        # Connection settings
        self.settings.setValue("serial_baud_rate", self.baud_rate_combo.currentText())

        # Colors
        for device in DEVICES:
            color_str = self.color_buttons[device].styleSheet().split("background-color: ")[1].split(";")[
                0] if "background-color:" in self.color_buttons[device].styleSheet() else "#1f77b4"
            self.settings.setValue(f"device_color_{device}", color_str)

    def accept(self):
        """Apply and close"""
        self.apply_settings()
        super().accept()


class TeensyController(QtCore.QObject):
    """Communication controller for Teensy 4.1 Power Controller"""

    # Signals for GUI updates
    data_received = QtCore.Signal(dict)
    status_received = QtCore.Signal(dict)
    error_occurred = QtCore.Signal(str)
    connection_changed = QtCore.Signal(bool)
    script_list_received = QtCore.Signal(list)

    def __init__(self):
        super().__init__()
        self.tcp_socket = None
        self.udp_socket = None
        self.serial_port = None
        self.connected = False
        self.streaming = False
        self.connection_type = "tcp"  # tcp, udp, serial

        # Communication threads
        self.tcp_thread = None
        self.serial_thread = None
        self.stop_threads = False

        # Data queues
        self.command_queue = queue.Queue()
        self.response_queue = queue.Queue()

        # Connection settings
        self.tcp_ip = "192.168.1.100"
        self.tcp_port = 8080
        self.udp_ip = "192.168.1.100"
        self.udp_port = 8081
        self.serial_port_name = ""
        self.serial_baud = 2000000

    def connect_tcp(self, ip, port):
        """Connect via TCP"""
        try:
            self.tcp_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.tcp_socket.settimeout(5.0)
            self.tcp_socket.connect((ip, port))
            self.tcp_socket.settimeout(None)

            self.connection_type = "tcp"
            self.connected = True
            self.tcp_ip = ip
            self.tcp_port = port

            # Start TCP listening thread
            self.stop_threads = False
            self.tcp_thread = threading.Thread(target=self._tcp_listener, daemon=True)
            self.tcp_thread.start()

            self.connection_changed.emit(True)
            return True

        except Exception as e:
            self.error_occurred.emit(f"TCP Connection failed: {str(e)}")
            self.connected = False
            self.connection_changed.emit(False)
            return False

    def connect_udp(self, ip, port):
        """Connect via UDP"""
        try:
            self.udp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.udp_socket.settimeout(1.0)

            # Test connection with a status request
            test_cmd = {"cmd": "get_status"}
            self._send_udp_command(test_cmd)

            self.connection_type = "udp"
            self.connected = True
            self.udp_ip = ip
            self.udp_port = port

            self.connection_changed.emit(True)
            return True

        except Exception as e:
            self.error_occurred.emit(f"UDP Connection failed: {str(e)}")
            self.connected = False
            self.connection_changed.emit(False)
            return False

    def connect_serial(self, port, baud=2000000):
        """Connect via Serial"""
        if not SERIAL_AVAILABLE:
            self.error_occurred.emit(
                "Serial communication requires pyserial library. Install with: pip install pyserial")
            return False

        try:
            self.serial_port = serial.Serial(port, baud, timeout=1)
            time.sleep(2)  # Give the connection time to establish

            self.connection_type = "serial"
            self.connected = True
            self.serial_port_name = port
            self.serial_baud = baud

            # Start serial listening thread
            self.stop_threads = False
            self.serial_thread = threading.Thread(target=self._serial_listener, daemon=True)
            self.serial_thread.start()

            self.connection_changed.emit(True)
            return True

        except Exception as e:
            self.error_occurred.emit(f"Serial Connection failed: {str(e)}")
            self.connected = False
            self.connection_changed.emit(False)
            return False

    def disconnect(self):
        """Disconnect from Teensy"""
        self.stop_threads = True
        self.connected = False
        self.streaming = False

        if self.tcp_socket:
            try:
                self.tcp_socket.close()
            except:
                pass
            self.tcp_socket = None

        if self.udp_socket:
            try:
                self.udp_socket.close()
            except:
                pass
            self.udp_socket = None

        if self.serial_port:
            try:
                self.serial_port.close()
            except:
                pass
            self.serial_port = None

        self.connection_changed.emit(False)

    def send_command(self, command_dict):
        """Send JSON command to Teensy"""
        if not self.connected:
            return False

        try:
            if self.connection_type == "tcp":
                return self._send_tcp_command(command_dict)
            elif self.connection_type == "udp":
                return self._send_udp_command(command_dict)
            elif self.connection_type == "serial":
                return self._send_serial_command(command_dict)
        except Exception as e:
            self.error_occurred.emit(f"Command send failed: {str(e)}")
            return False

        return False

    def _send_tcp_command(self, command_dict):
        """Send command via TCP"""
        if not self.tcp_socket:
            return False

        try:
            json_str = json.dumps(command_dict) + '\n'
            self.tcp_socket.send(json_str.encode('utf-8'))
            return True
        except Exception as e:
            self.error_occurred.emit(f"TCP send failed: {str(e)}")
            return False

    def _send_udp_command(self, command_dict):
        """Send command via UDP"""
        if not self.udp_socket:
            return False

        try:
            json_str = json.dumps(command_dict)
            self.udp_socket.sendto(json_str.encode('utf-8'), (self.udp_ip, self.udp_port))
            return True
        except Exception as e:
            self.error_occurred.emit(f"UDP send failed: {str(e)}")
            return False

    def _send_serial_command(self, command_dict):
        """Send command via Serial"""
        if not self.serial_port:
            return False

        try:
            json_str = json.dumps(command_dict) + '\n'
            print(f"Sending serial command: {json_str.strip()}")
            self.serial_port.write(json_str.encode('utf-8'))
            self.serial_port.flush()
            return True
        except Exception as e:
            self.error_occurred.emit(f"Serial send failed: {str(e)}")
            return False

    def _tcp_listener(self):
        """TCP listening thread"""
        buffer = ""
        while not self.stop_threads and self.connected:
            try:
                data = self.tcp_socket.recv(1024).decode('utf-8')
                if not data:
                    break

                buffer += data
                while '\n' in buffer:
                    line, buffer = buffer.split('\n', 1)
                    if line.strip():
                        self._process_received_data(line.strip())

            except socket.timeout:
                continue
            except Exception as e:
                if not self.stop_threads:
                    self.error_occurred.emit(f"TCP receive error: {str(e)}")
                break

        self.connected = False
        self.connection_changed.emit(False)

    def _serial_listener(self):
        """Serial listening thread"""
        buffer = ""
        while not self.stop_threads and self.connected:
            try:
                if self.serial_port.in_waiting > 0:
                    data = self.serial_port.read(self.serial_port.in_waiting).decode('utf-8', errors='ignore')
                    buffer += data

                    # Process complete lines
                    while '\n' in buffer:
                        line, buffer = buffer.split('\n', 1)
                        line = line.strip()
                        if line:
                            print(f"Received serial data: {line}")
                            self._process_received_data(line)
                else:
                    time.sleep(0.01)

            except Exception as e:
                if not self.stop_threads:
                    self.error_occurred.emit(f"Serial receive error: {str(e)}")
                break

        self.connected = False
        self.connection_changed.emit(False)

    def _process_received_data(self, json_str):
        """Process received JSON data"""
        try:
            # Try to parse as JSON
            data = json.loads(json_str)

            # Route data based on type
            data_type = data.get('type', '')

            if data_type == 'live_data':
                self.data_received.emit(data)
            elif data_type == 'status':
                self.status_received.emit(data)
            elif data_type == 'script_list':
                self.script_list_received.emit(data.get('scripts', []))
            elif data_type == 'error':
                self.error_occurred.emit(data.get('message', 'Unknown error'))
            elif data_type == 'command_response':
                print(f"Command response: {data}")
            elif data_type == 'connection':
                print(f"Connection message: {data}")
            elif data_type == 'heartbeat':
                pass
            else:
                print(f"Unknown data type: {data}")

        except json.JSONDecodeError:
            print(f"Non-JSON response: {json_str}")

    def start_streaming(self, interval=100):
        """Start data streaming"""
        command = {"cmd": "start_stream", "interval": interval}
        if self.send_command(command):
            self.streaming = True
            return True
        return False

    def stop_streaming(self):
        """Stop data streaming"""
        command = {"cmd": "stop_stream"}
        if self.send_command(command):
            self.streaming = False
            return True
        return False

    def get_status(self):
        """Request system status"""
        command = {"cmd": "get_status"}
        return self.send_command(command)

    def get_scripts(self):
        """Request script list"""
        command = {"cmd": "get_scripts"}
        return self.send_command(command)

    def set_output(self, device, state):
        """Set individual output state"""
        command_device = DEVICE_COMMAND_MAP.get(device, device.lower().replace("-", ""))
        command = {"cmd": "set_output", "device": command_device, "state": state}
        print(f"Setting device {device} ({command_device}) to {state}")
        return self.send_command(command)

    def set_all_outputs(self, state):
        """Set all outputs state"""
        command = {"cmd": "all_outputs", "state": state}
        return self.send_command(command)

    def set_lock(self, state):
        """Set system lock state"""
        command = {"cmd": "lock", "state": state}
        return self.send_command(command)

    def set_safety_stop(self, state):
        """Set safety stop state"""
        command = {"cmd": "safety_stop", "state": state}
        return self.send_command(command)

    def start_recording(self):
        """Start data recording"""
        command = {"cmd": "start_recording"}
        return self.send_command(command)

    def stop_recording(self):
        """Stop data recording"""
        command = {"cmd": "stop_recording"}
        return self.send_command(command)

    def load_script(self, script_name):
        """Load a script"""
        command = {"cmd": "load_script", "name": script_name}
        return self.send_command(command)

    def start_script(self):
        """Start script execution"""
        command = {"cmd": "start_script"}
        return self.send_command(command)

    def pause_script(self):
        """Pause script execution"""
        command = {"cmd": "pause_script"}
        return self.send_command(command)

    def stop_script(self):
        """Stop script execution"""
        command = {"cmd": "stop_script"}
        return self.send_command(command)

    def set_fan_speed(self, speed):
        """Set fan speed (0-255)"""
        command = {"cmd": "set_fan_speed", "value": speed}
        return self.send_command(command)

    def set_update_rate(self, rate):
        """Set update rate (50-5000ms)"""
        command = {"cmd": "set_update_rate", "value": rate}
        return self.send_command(command)


class PowerControllerGUI(QtWidgets.QMainWindow):
    """Main application class for Teensy 4.1 Power Controller GUI with FIXED smooth plotting"""

    def __init__(self):
        super().__init__()

        # Core data (keeping original structure)
        self.current_file_path = None
        self.data_json = None
        self.data_points = []
        self.times = []
        self.channels = {}

        # FIXED: Enhanced live data system with smooth updates
        self.live_data_points = deque()
        self.live_times = deque()
        self.live_channels = {}

        # NEW: Smooth update system
        self.plot_initialized = False
        self.data_buffer = deque(maxlen=50)  # Buffer for smooth updates
        self.last_plot_update = 0
        self.plot_curves_cache = {}  # Cache for plot curves
        self.plot_layout_stable = False  # Track if layout is stable

        # Common data structure
        self.all_fields = []
        self.devices = DEVICES.copy()
        self.types = DATA_TYPES.copy()

        # UI components
        self.plots = {}
        self.curves = {}
        self.field_checkboxes = {}
        self.device_controls = {}
        self.current_tab_index = 0

        # Teensy controller
        self.teensy = TeensyController()
        self.setup_teensy_signals()

        # Live data management
        self.live_mode = True
        self.max_live_points = 10000
        self.last_data_time = 0
        self.user_interacting = False
        self.plot_update_pending = False

        # System status
        self.system_status = {}
        self.available_scripts = []

        # Original analysis features
        self.script_info = {}
        self.corrupted_indices = []
        self.original_data_points_count = 0

        # Crosshair management
        self.crosshair_items = {}

        # Settings
        self.settings = QtCore.QSettings("TeensyPowerController", "TeensyPowerController")
        self.max_recent_files = 10

        # Analysis update timer
        self.analysis_timer = QtCore.QTimer()
        self.analysis_timer.timeout.connect(self.update_side_panel_for_current_tab)

        # FIXED: Smooth plot update system
        self.plot_update_timer = QtCore.QTimer()
        self.plot_update_timer.setSingleShot(False)  # Continuous timer for smooth updates
        self.plot_update_timer.timeout.connect(self._process_data_buffer)
        self.plot_update_timer.start(50)  # 20 FPS for smooth updates

        # Debug console
        self.debug_console = None

        self.init_ui()
        self.create_menus()
        self.load_settings()
        self.check_dependencies()

        # Start analysis timer
        self.analysis_timer.start(self.settings.value("analysis_update_rate", 2000, int))

    def check_dependencies(self):
        """Check for missing dependencies and show warnings"""
        missing_deps = []

        if not SERIAL_AVAILABLE:
            missing_deps.append("pyserial (for USB Serial communication)")

        if missing_deps:
            dep_list = "\n• ".join(missing_deps)
            QtWidgets.QMessageBox.information(
                self,
                "Optional Dependencies Missing",
                f"Some optional features require additional libraries:\n\n• {dep_list}\n\n"
                f"Install with: pip install pyserial\n\n"
                f"The application will work with network connections only."
            )

    def setup_teensy_signals(self):
        """Connect Teensy controller signals"""
        self.teensy.data_received.connect(self.on_live_data_received)
        self.teensy.status_received.connect(self.on_status_received)
        self.teensy.error_occurred.connect(self.on_error_occurred)
        self.teensy.connection_changed.connect(self.on_connection_changed)
        self.teensy.script_list_received.connect(self.on_script_list_received)

    def init_ui(self):
        """Initialize the user interface"""
        self.setWindowTitle("Teensy 4.1 Power Controller - Network Interface")
        self.setWindowIcon(QtGui.QIcon("resources/icons/app_icon.ico") if os.path.exists(
            "resources/icons/app_icon.ico") else self.style().standardIcon(
            QtWidgets.QStyle.StandardPixmap.SP_ComputerIcon))

        # Central widget
        centralWidget = QtWidgets.QWidget()
        self.setCentralWidget(centralWidget)

        # Main layout using QSplitter
        mainLayout = QtWidgets.QVBoxLayout(centralWidget)

        # Connection panel at top
        self.create_connection_panel()
        self.connectionPanel.setMaximumHeight(160)
        self.connectionPanel.setMinimumHeight(160)
        self.connectionPanel.setSizePolicy(QtWidgets.QSizePolicy.Policy.Expanding, QtWidgets.QSizePolicy.Policy.Fixed)
        mainLayout.addWidget(self.connectionPanel)

        # Main content splitter
        self.mainSplitter = QtWidgets.QSplitter(QtCore.Qt.Orientation.Horizontal)
        mainLayout.addWidget(self.mainSplitter)

        # Left: Control and field selection panel
        self.create_field_panel()

        # Middle: Tabbed plot area
        self.create_plot_tabs()

        # Right: Analysis side panel
        self.create_side_panel()

        # Add panels to splitter
        self.mainSplitter.addWidget(self.fieldPanel)
        self.mainSplitter.addWidget(self.plotTabWidget)
        self.mainSplitter.addWidget(self.sidePanel)

        # Set initial splitter sizes
        self.mainSplitter.setSizes([300, 700, 300])

        # Status bar
        self.statusBar().showMessage("Ready - Connect to Teensy or open a file")

    def create_connection_panel(self):
        """Create the connection control panel"""
        self.connectionPanel = QtWidgets.QGroupBox("Teensy Connection")
        layout = QtWidgets.QHBoxLayout(self.connectionPanel)

        # Connection type selection
        type_group = QtWidgets.QGroupBox("Type")
        type_layout = QtWidgets.QHBoxLayout(type_group)

        self.tcp_radio = QtWidgets.QRadioButton("TCP")
        self.udp_radio = QtWidgets.QRadioButton("UDP")
        self.serial_radio = QtWidgets.QRadioButton("Serial")
        self.tcp_radio.setChecked(True)

        self.tcp_radio.setMinimumHeight(25)
        self.udp_radio.setMinimumHeight(25)
        self.serial_radio.setMinimumHeight(25)

        if not SERIAL_AVAILABLE:
            self.serial_radio.setEnabled(False)
            self.serial_radio.setToolTip("Requires pyserial library")

        type_layout.addWidget(self.tcp_radio)
        type_layout.addWidget(self.udp_radio)
        type_layout.addWidget(self.serial_radio)

        # Connection settings
        settings_group = QtWidgets.QGroupBox("Settings")
        settings_layout = QtWidgets.QGridLayout(settings_group)

        # IP Address
        settings_layout.addWidget(QtWidgets.QLabel("IP:"), 0, 0)
        self.ip_edit = QtWidgets.QLineEdit("192.168.1.100")
        self.ip_edit.setMinimumHeight(25)
        settings_layout.addWidget(self.ip_edit, 0, 1)

        # TCP Port
        settings_layout.addWidget(QtWidgets.QLabel("TCP Port:"), 0, 2)
        self.tcp_port_edit = QtWidgets.QLineEdit("8080")
        self.tcp_port_edit.setMinimumHeight(25)
        settings_layout.addWidget(self.tcp_port_edit, 0, 3)

        # UDP Port
        settings_layout.addWidget(QtWidgets.QLabel("UDP Port:"), 1, 2)
        self.udp_port_edit = QtWidgets.QLineEdit("8081")
        self.udp_port_edit.setMinimumHeight(25)
        settings_layout.addWidget(self.udp_port_edit, 1, 3)

        # Serial Port
        settings_layout.addWidget(QtWidgets.QLabel("Serial Port:"), 1, 0)
        self.serial_combo = QtWidgets.QComboBox()
        self.serial_combo.setMinimumHeight(25)
        if SERIAL_AVAILABLE:
            self.refresh_serial_ports()
        else:
            self.serial_combo.addItem("Serial not available")
            self.serial_combo.setEnabled(False)
        settings_layout.addWidget(self.serial_combo, 1, 1)

        # Baud rate
        settings_layout.addWidget(QtWidgets.QLabel("Baud Rate:"), 2, 0)
        self.baud_combo = QtWidgets.QComboBox()
        self.baud_combo.setEditable(True)
        self.baud_combo.addItems(["9600", "19200", "38400", "57600", "115200", "230400", "460800", "921600", "2000000"])
        self.baud_combo.setCurrentText("2000000")
        self.baud_combo.setMinimumHeight(25)
        settings_layout.addWidget(self.baud_combo, 2, 1)

        # Connection controls
        controls_group = QtWidgets.QGroupBox("Control")
        controls_layout = QtWidgets.QVBoxLayout(controls_group)

        self.connect_btn = QtWidgets.QPushButton("Connect")
        self.disconnect_btn = QtWidgets.QPushButton("Disconnect")
        self.refresh_btn = QtWidgets.QPushButton("Refresh Ports")

        self.connect_btn.setMinimumHeight(30)
        self.disconnect_btn.setMinimumHeight(30)
        self.refresh_btn.setMinimumHeight(30)

        self.connect_btn.clicked.connect(self.connect_to_teensy)
        self.disconnect_btn.clicked.connect(self.disconnect_from_teensy)
        self.refresh_btn.clicked.connect(self.refresh_serial_ports)

        self.disconnect_btn.setEnabled(False)
        if not SERIAL_AVAILABLE:
            self.refresh_btn.setEnabled(False)
            self.refresh_btn.setToolTip("Requires pyserial library")

        controls_layout.addWidget(self.connect_btn)
        controls_layout.addWidget(self.disconnect_btn)
        controls_layout.addWidget(self.refresh_btn)

        # Connection status
        status_group = QtWidgets.QGroupBox("Status")
        status_layout = QtWidgets.QVBoxLayout(status_group)

        self.connection_status_label = QtWidgets.QLabel("Disconnected")
        self.connection_status_label.setAlignment(QtCore.Qt.AlignmentFlag.AlignCenter)
        self.connection_status_label.setStyleSheet(
            "QLabel { background-color: #ff6b6b; color: white; padding: 8px; border-radius: 3px; font-weight: bold; min-height: 20px; }")
        status_layout.addWidget(self.connection_status_label)

        # Quick controls
        device_controls_group = QtWidgets.QGroupBox("Quick Controls")
        device_controls_layout = QtWidgets.QGridLayout(device_controls_group)

        # Master controls
        self.all_on_btn = QtWidgets.QPushButton("ALL ON")
        self.all_off_btn = QtWidgets.QPushButton("ALL OFF")

        button_style = "QPushButton { min-height: 35px; font-weight: bold; }"
        self.all_on_btn.setStyleSheet(button_style + "QPushButton { background-color: #51cf66; color: white; }")
        self.all_off_btn.setStyleSheet(button_style + "QPushButton { background-color: #ff6b6b; color: white; }")

        self.all_on_btn.clicked.connect(lambda: self.teensy.set_all_outputs(True))
        self.all_off_btn.clicked.connect(lambda: self.teensy.set_all_outputs(False))
        device_controls_layout.addWidget(self.all_on_btn, 0, 0)
        device_controls_layout.addWidget(self.all_off_btn, 0, 1)

        # System controls
        self.lock_btn = QtWidgets.QPushButton("UNLOCK")
        self.lock_btn.setCheckable(True)
        self.lock_btn.setStyleSheet(button_style)
        self.lock_btn.clicked.connect(self.toggle_lock)
        device_controls_layout.addWidget(self.lock_btn, 1, 0)

        self.safety_btn = QtWidgets.QPushButton("NORMAL")
        self.safety_btn.setCheckable(True)
        self.safety_btn.setStyleSheet(button_style)
        self.safety_btn.clicked.connect(self.toggle_safety_stop)
        device_controls_layout.addWidget(self.safety_btn, 1, 1)

        # Stream and clear controls
        self.stream_btn = QtWidgets.QPushButton("Start Stream")
        self.stream_btn.setCheckable(True)
        self.stream_btn.setStyleSheet(button_style)
        self.stream_btn.clicked.connect(self.toggle_streaming)
        device_controls_layout.addWidget(self.stream_btn, 2, 0)

        self.clear_data_btn = QtWidgets.QPushButton("Clear Data")
        self.clear_data_btn.clicked.connect(self.clear_live_data)
        self.clear_data_btn.setStyleSheet(button_style + "QPushButton { background-color: #ffd43b; color: black; }")
        device_controls_layout.addWidget(self.clear_data_btn, 2, 1)

        # Add to main layout
        layout.addWidget(type_group)
        layout.addWidget(settings_group)
        layout.addWidget(controls_group)
        layout.addWidget(status_group)
        layout.addWidget(device_controls_group)

    def create_field_panel(self):
        """Create the field selection panel"""
        self.fieldPanel = QtWidgets.QWidget()
        self.fieldLayout = QtWidgets.QVBoxLayout(self.fieldPanel)
        self.fieldLayout.setContentsMargins(5, 5, 5, 5)
        self.fieldLayout.setSpacing(5)

        # File info section
        self.file_info_label = QtWidgets.QLabel("<b>Live Mode Active</b>")
        self.file_info_label.setWordWrap(True)
        self.fieldLayout.addWidget(self.file_info_label)

        # Mode selection
        mode_group = QtWidgets.QGroupBox("Data Mode")
        mode_layout = QtWidgets.QVBoxLayout(mode_group)

        self.live_mode_radio = QtWidgets.QRadioButton("Live Data")
        self.file_mode_radio = QtWidgets.QRadioButton("File Analysis")
        self.live_mode_radio.setChecked(True)
        self.live_mode_radio.toggled.connect(self.on_mode_changed)

        mode_layout.addWidget(self.live_mode_radio)
        mode_layout.addWidget(self.file_mode_radio)
        self.fieldLayout.addWidget(mode_group)

        # Individual device controls
        device_group = QtWidgets.QGroupBox("Device Control")
        device_layout = QtWidgets.QVBoxLayout(device_group)

        self.device_controls = {}
        for device in DEVICES:
            device_frame = QtWidgets.QFrame()
            device_frame.setFrameStyle(QtWidgets.QFrame.Shape.Box)
            device_frame_layout = QtWidgets.QGridLayout(device_frame)
            device_frame_layout.setContentsMargins(3, 3, 3, 3)

            device_label = QtWidgets.QLabel(device)
            device_label.setStyleSheet("font-weight: bold;")
            device_label.setFixedWidth(50)

            device_toggle = QtWidgets.QPushButton("OFF")
            device_toggle.setCheckable(True)
            device_toggle.setFixedWidth(40)
            device_toggle.clicked.connect(lambda checked, d=device: self.toggle_device(d, checked))

            status_label = QtWidgets.QLabel("0.0V 0.0A")
            status_label.setStyleSheet("font-size: 10px;")
            status_label.setFixedWidth(80)

            device_frame_layout.addWidget(device_label, 0, 0)
            device_frame_layout.addWidget(device_toggle, 0, 1)
            device_frame_layout.addWidget(status_label, 0, 2)

            self.device_controls[device] = {
                'toggle': device_toggle,
                'status': status_label,
                'frame': device_frame
            }

            device_layout.addWidget(device_frame)

        self.fieldLayout.addWidget(device_group)

        # Script controls
        script_group = QtWidgets.QGroupBox("Script Control")
        script_layout = QtWidgets.QVBoxLayout(script_group)

        self.script_combo = QtWidgets.QComboBox()
        script_layout.addWidget(self.script_combo)

        script_btn_layout = QtWidgets.QHBoxLayout()
        self.load_script_btn = QtWidgets.QPushButton("Load")
        self.start_script_btn = QtWidgets.QPushButton("Start")
        self.stop_script_btn = QtWidgets.QPushButton("Stop")

        self.load_script_btn.clicked.connect(self.load_selected_script)
        self.start_script_btn.clicked.connect(self.teensy.start_script)
        self.stop_script_btn.clicked.connect(self.teensy.stop_script)

        script_btn_layout.addWidget(self.load_script_btn)
        script_btn_layout.addWidget(self.start_script_btn)
        script_btn_layout.addWidget(self.stop_script_btn)
        script_layout.addLayout(script_btn_layout)

        self.fieldLayout.addWidget(script_group)

        # Field selection section
        self.field_selection_label = QtWidgets.QLabel("<b>Select data types to plot:</b>")
        self.fieldLayout.addWidget(self.field_selection_label)

        # Field checkboxes
        self.create_field_checkboxes()

        # Display controls
        display_group = QtWidgets.QGroupBox("Display Controls")
        display_layout = QtWidgets.QVBoxLayout(display_group)

        # Auto-resize toggle
        self.auto_resize_cb = QtWidgets.QCheckBox("Auto-resize plots")
        self.auto_resize_cb.setChecked(self.settings.value("auto_resize", True, bool))
        self.auto_resize_cb.stateChanged.connect(self.on_auto_resize_changed)
        display_layout.addWidget(self.auto_resize_cb)

        # Crosshair toggle
        self.crosshair_cb = QtWidgets.QCheckBox("Show crosshair")
        self.crosshair_cb.setChecked(self.settings.value("enable_crosshair", True, bool))
        self.crosshair_cb.stateChanged.connect(self.on_crosshair_changed)
        display_layout.addWidget(self.crosshair_cb)

        self.fieldLayout.addWidget(display_group)

        # Toggle side panel button
        self.create_toggle_button()

        self.fieldLayout.addStretch(1)

    def create_field_checkboxes(self):
        """Create checkboxes for data type selection"""
        self.field_checkboxes = {}

        for typ in self.types:
            cb = QtWidgets.QCheckBox(self.format_type_name(typ))
            cb.setChecked(True)
            self.field_checkboxes[typ] = cb
            cb.stateChanged.connect(self.schedule_plot_update)
            self.fieldLayout.addWidget(cb)

    def format_type_name(self, type_name):
        """Format type names for display"""
        type_mapping = {
            "volt": "Voltage",
            "curr": "Current",
            "pow": "Power",
            "stat": "Status"
        }
        return type_mapping.get(type_name, type_name.title())

    def format_axis_label(self, device, data_type):
        """Format axis labels for graphs"""
        device_formatted = device.replace("-", "-")

        type_mapping = {
            "volt": f"{device_formatted} Voltage (V)",
            "curr": f"{device_formatted} Current (A)",
            "pow": f"{device_formatted} Power (W)",
            "stat": f"{device_formatted} Status"
        }
        return type_mapping.get(data_type, f"{device_formatted} {data_type.title()}")

    def create_toggle_button(self):
        """Create the toggle side panel button"""
        self.toggleSidePanelBtn = QtWidgets.QPushButton()

        icon_path = "resources/icons/sidebar.svg"
        if os.path.exists(icon_path):
            self.toggleSidePanelBtn.setIcon(QtGui.QIcon(icon_path))
        else:
            self.toggleSidePanelBtn.setIcon(
                self.style().standardIcon(QtWidgets.QStyle.StandardPixmap.SP_FileDialogDetailedView))

        self.toggleSidePanelBtn.setIconSize(QtCore.QSize(24, 24))
        self.toggleSidePanelBtn.setToolTip("Toggle Side Panel (F9)")
        self.toggleSidePanelBtn.setFixedSize(40, 40)

        self.toggleSidePanelBtn.setStyleSheet("""
            QPushButton {
                border: 1px solid #ccc;
                border-radius: 4px;
                background-color: #f0f0f0;
            }
            QPushButton:hover {
                background-color: #e0e0e0;
            }
            QPushButton:pressed {
                background-color: #d0d0d0;
            }
        """)

        self.toggleSidePanelBtn.clicked.connect(self.toggle_side_panel)
        self.fieldLayout.addWidget(self.toggleSidePanelBtn)

    def create_plot_tabs(self):
        """Create the tabbed plot area"""
        self.plotTabWidget = QtWidgets.QTabWidget()
        self.plotTabWidget.currentChanged.connect(self.on_tab_changed)

        # Create All tab
        self.all_plot_widget = pg.GraphicsLayoutWidget()
        self.plotTabWidget.addTab(self.all_plot_widget, "All")

        # Create individual device tabs
        self.device_plot_widgets = {}
        for device in self.devices:
            plot_widget = pg.GraphicsLayoutWidget()
            self.device_plot_widgets[device] = plot_widget
            self.plotTabWidget.addTab(plot_widget, device)

    def create_side_panel(self):
        """Create the side panel"""
        self.sidePanel = QtWidgets.QWidget()
        self.sidePanelLayout = QtWidgets.QVBoxLayout(self.sidePanel)
        self.sidePanelLayout.setContentsMargins(5, 5, 5, 5)
        self.sidePanelLayout.setSpacing(5)

        # Title
        title_label = QtWidgets.QLabel("<b>Analysis Panel</b>")
        self.sidePanelLayout.addWidget(title_label)

    def create_menus(self):
        """Create the menu bar"""
        menubar = self.menuBar()

        # File Menu
        file_menu = menubar.addMenu('&File')

        # Open File
        open_action = QtGui.QAction(QtGui.QIcon.fromTheme("document-open"), '&Open File...', self)
        open_action.setShortcut('Ctrl+O')
        open_action.setStatusTip('Open a JSON data file')
        open_action.triggered.connect(self.open_file)
        file_menu.addAction(open_action)

        # Recent Files
        self.recent_menu = file_menu.addMenu('&Recent Files')
        self.update_recent_files_menu()

        file_menu.addSeparator()

        # Save live data
        save_data_action = QtGui.QAction('Save &Live Data...', self)
        save_data_action.setShortcut('Ctrl+Shift+S')
        save_data_action.setStatusTip('Save current live data')
        save_data_action.triggered.connect(self.save_live_data)
        file_menu.addAction(save_data_action)

        # Save/Export
        save_action = QtGui.QAction('&Save Analysis...', self)
        save_action.setShortcut('Ctrl+S')
        save_action.setStatusTip('Save analysis data')
        save_action.triggered.connect(self.show_save_dialog)
        file_menu.addAction(save_action)

        # Export Menu
        export_menu = file_menu.addMenu('&Export Analysis')

        # Export to Text
        export_text_action = QtGui.QAction('Export to &Text...', self)
        export_text_action.setStatusTip('Export analysis to text file')
        export_text_action.triggered.connect(lambda: self.export_analysis('text'))
        export_menu.addAction(export_text_action)

        # Export to CSV
        export_csv_action = QtGui.QAction('Export to &CSV...', self)
        export_csv_action.setStatusTip('Export analysis to CSV file')
        export_csv_action.triggered.connect(lambda: self.export_analysis('csv'))
        export_menu.addAction(export_csv_action)

        # Export to Excel (if available)
        if PANDAS_AVAILABLE and OPENPYXL_AVAILABLE:
            export_excel_action = QtGui.QAction('Export to &Excel...', self)
            export_excel_action.setStatusTip('Export analysis to Excel file')
            export_excel_action.triggered.connect(lambda: self.export_analysis('excel'))
            export_menu.addAction(export_excel_action)

        file_menu.addSeparator()

        # Exit
        exit_action = QtGui.QAction('E&xit', self)
        exit_action.setShortcut('Ctrl+Q')
        exit_action.setStatusTip('Exit application')
        exit_action.triggered.connect(self.close)
        file_menu.addAction(exit_action)

        # Teensy Menu
        teensy_menu = menubar.addMenu('&Teensy')

        # Get Status
        status_action = QtGui.QAction('Get &Status', self)
        status_action.setShortcut('F5')
        status_action.triggered.connect(self.teensy.get_status)
        teensy_menu.addAction(status_action)

        # Get Scripts
        scripts_action = QtGui.QAction('Refresh &Scripts', self)
        scripts_action.triggered.connect(self.teensy.get_scripts)
        teensy_menu.addAction(scripts_action)

        # Settings
        settings_action = QtGui.QAction('&Settings...', self)
        settings_action.setShortcut('Ctrl+,')
        settings_action.triggered.connect(self.show_settings)
        teensy_menu.addAction(settings_action)

        # Tools Menu
        tools_menu = menubar.addMenu('&Tools')

        # Debug Console
        debug_action = QtGui.QAction('&Debug Console', self)
        debug_action.setShortcut('Ctrl+Shift+X')
        debug_action.triggered.connect(self.show_debug_console)
        tools_menu.addAction(debug_action)

        # View Menu
        view_menu = menubar.addMenu('&View')

        # Toggle Side Panel
        toggle_side_action = QtGui.QAction('Toggle &Side Panel', self)
        toggle_side_action.setShortcut('F9')
        toggle_side_action.setStatusTip('Toggle side panel visibility')
        toggle_side_action.triggered.connect(self.toggle_side_panel)
        view_menu.addAction(toggle_side_action)

        # Tab shortcuts
        for i, device in enumerate(self.devices):
            tab_action = QtGui.QAction(f'Switch to {device}', self)
            tab_action.setShortcut(f'{i + 1}')
            tab_action.triggered.connect(lambda checked, idx=i + 1: self.plotTabWidget.setCurrentIndex(idx))
            view_menu.addAction(tab_action)

        # All tab shortcut
        all_tab_action = QtGui.QAction('Switch to All', self)
        all_tab_action.setShortcut('0')
        all_tab_action.setShortcuts(('0', 'A'))
        all_tab_action.triggered.connect(lambda: self.plotTabWidget.setCurrentIndex(0))
        view_menu.addAction(all_tab_action)

    def show_settings(self):
        """Show settings dialog"""
        dialog = SettingsDialog(self, self.settings)
        if dialog.exec() == QtWidgets.QDialog.DialogCode.Accepted:
            self.apply_new_settings()

    def apply_new_settings(self):
        """Apply new settings from dialog"""
        self.max_live_points = self.settings.value("max_points", 10000, int)
        self.analysis_timer.setInterval(self.settings.value("analysis_update_rate", 2000, int))
        self.auto_resize_cb.setChecked(self.settings.value("auto_resize", True, bool))
        self.crosshair_cb.setChecked(self.settings.value("enable_crosshair", True, bool))
        self.baud_combo.setCurrentText(self.settings.value("serial_baud_rate", "2000000"))
        self.schedule_plot_update()

    def show_debug_console(self):
        """Show debug console"""
        if self.debug_console is None:
            self.debug_console = DebugConsole(self, self.teensy)
        self.debug_console.show()
        self.debug_console.raise_()

    def save_live_data(self):
        """Save current live data"""
        data_to_save = self.live_data_points if self.live_mode else self.data_points
        if not data_to_save:
            QtWidgets.QMessageBox.warning(self, "No Data", "No data to save")
            return

        file_path, _ = QtWidgets.QFileDialog.getSaveFileName(
            self, "Save Data", "",
            "JSON files (*.json);;CSV files (*.csv);;Excel files (*.xlsx);;All files (*.*)"
        )

        if file_path:
            try:
                if file_path.endswith('.csv'):
                    self.export_live_data_csv(file_path)
                elif file_path.endswith('.xlsx'):
                    self.export_live_data_excel(file_path)
                else:
                    self.export_live_data_json(file_path)
                QtWidgets.QMessageBox.information(self, "Success", f"Data saved to {file_path}")
            except Exception as e:
                QtWidgets.QMessageBox.critical(self, "Error", f"Failed to save data: {str(e)}")

    def export_live_data_json(self, file_path):
        """Export live data to JSON format"""
        data_to_export = list(self.live_data_points) if self.live_mode else self.data_points
        times_to_export = list(self.live_times) if self.live_mode else self.times

        export_data = {
            'timestamp': datetime.now().isoformat(),
            'duration_sec': int(times_to_export[-1] - times_to_export[0]) if len(times_to_export) > 1 else 0,
            'data': data_to_export
        }

        with open(file_path, 'w', encoding='utf-8') as f:
            json.dump(export_data, f, indent=2)

    def export_live_data_csv(self, file_path):
        """Export live data to CSV format"""
        import csv

        data_to_export = list(self.live_data_points) if self.live_mode else self.data_points

        with open(file_path, 'w', newline='', encoding='utf-8') as f:
            if not data_to_export:
                return

            writer = csv.DictWriter(f, fieldnames=data_to_export[0].keys())
            writer.writeheader()
            writer.writerows(data_to_export)

    def export_live_data_excel(self, file_path):
        """Export live data to Excel format"""
        if not PANDAS_AVAILABLE:
            raise Exception("Excel export requires pandas library")

        data_to_export = list(self.live_data_points) if self.live_mode else self.data_points
        df = pd.DataFrame(data_to_export)
        df.to_excel(file_path, index=False)

    def apply_data_filtering(self, data_array):
        """Apply data filtering based on settings"""
        if not self.settings.value("enable_filtering", False, bool):
            return data_array

        filtered_data = np.array(data_array)

        # Moving average
        window = self.settings.value("moving_avg_window", 5, int)
        if window > 1 and len(filtered_data) >= window:
            filtered_data = np.convolve(filtered_data, np.ones(window) / window, mode='same')

        # Interpolation for missing values
        if self.settings.value("enable_interpolation", False, bool):
            mask = np.isfinite(filtered_data)
            if np.any(~mask) and np.any(mask):
                indices = np.arange(len(filtered_data))
                filtered_data[~mask] = np.interp(indices[~mask], indices[mask], filtered_data[mask])

        return filtered_data.tolist()

    def on_auto_resize_changed(self):
        """Handle auto-resize setting change"""
        self.settings.setValue("auto_resize", self.auto_resize_cb.isChecked())

    def on_crosshair_changed(self):
        """Handle crosshair toggle change"""
        enabled = self.crosshair_cb.isChecked()
        self.settings.setValue("enable_crosshair", enabled)
        self.clear_crosshairs()
        self.schedule_plot_update()

    def clear_crosshairs(self):
        """Clear all crosshair items from plots"""
        for plot_key, items in self.crosshair_items.items():
            if plot_key in self.plots:
                plot = self.plots[plot_key]
                for item in items:
                    try:
                        plot.removeItem(item)
                    except:
                        pass
        self.crosshair_items.clear()

    def schedule_plot_update(self):
        """Schedule a plot update"""
        if not self.plot_update_pending:
            self.plot_update_pending = True
            # Use a single-shot timer for immediate update when user changes settings
            QtCore.QTimer.singleShot(100, self._update_plots_now)

    def _update_plots_now(self):
        """Immediate plot update for user interaction"""
        self.plot_update_pending = False
        self.update_plots()

    # Connection methods
    def refresh_serial_ports(self):
        """Refresh the list of available serial ports"""
        if not SERIAL_AVAILABLE:
            return

        self.serial_combo.clear()
        try:
            ports = list_ports.comports()
            for port in ports:
                self.serial_combo.addItem(f"{port.device} - {port.description}")

            if not ports:
                self.serial_combo.addItem("No serial ports found")
        except Exception as e:
            self.serial_combo.addItem("Error scanning ports")

    def connect_to_teensy(self):
        """Connect to the Teensy controller"""
        if self.tcp_radio.isChecked():
            ip = self.ip_edit.text()
            port = int(self.tcp_port_edit.text())
            success = self.teensy.connect_tcp(ip, port)
        elif self.udp_radio.isChecked():
            ip = self.ip_edit.text()
            port = int(self.udp_port_edit.text())
            success = self.teensy.connect_udp(ip, port)
        else:  # Serial
            if not SERIAL_AVAILABLE:
                QtWidgets.QMessageBox.warning(
                    self,
                    "Serial Not Available",
                    "Serial communication requires the pyserial library.\n\nInstall with: pip install pyserial"
                )
                return

            if self.serial_combo.currentText() and "No serial ports" not in self.serial_combo.currentText():
                port = self.serial_combo.currentText().split(' - ')[0]
                baud = int(self.baud_combo.currentText())
                success = self.teensy.connect_serial(port, baud)
            else:
                QtWidgets.QMessageBox.warning(self, "No Port", "Please select a serial port")
                return

        if success:
            QtCore.QTimer.singleShot(500, self.teensy.get_status)
            QtCore.QTimer.singleShot(1000, self.teensy.get_scripts)
            self.live_mode_radio.setChecked(True)
            self.on_mode_changed()

    def disconnect_from_teensy(self):
        """Disconnect from the Teensy controller"""
        if self.teensy.streaming:
            self.teensy.stop_streaming()
        self.teensy.disconnect()

    def on_connection_changed(self, connected):
        """Handle connection status change"""
        self.connect_btn.setEnabled(not connected)
        self.disconnect_btn.setEnabled(connected)

        if connected:
            self.connection_status_label.setText("Connected")
            self.connection_status_label.setStyleSheet(
                "QLabel { background-color: #51cf66; color: white; padding: 8px; border-radius: 3px; font-weight: bold; min-height: 20px; }")
            self.statusBar().showMessage("Connected to Teensy")
        else:
            self.connection_status_label.setText("Disconnected")
            self.connection_status_label.setStyleSheet(
                "QLabel { background-color: #ff6b6b; color: white; padding: 8px; border-radius: 3px; font-weight: bold; min-height: 20px; }")
            self.statusBar().showMessage("Disconnected from Teensy")

            # Reset streaming button
            self.stream_btn.setText("Start Stream")
            self.stream_btn.setChecked(False)

    def on_error_occurred(self, error_msg):
        """Handle error messages"""
        self.statusBar().showMessage(f"Error: {error_msg}")
        if "pyserial" not in error_msg.lower():
            print(f"Error occurred: {error_msg}")

    # Device control methods
    def toggle_device(self, device, state):
        """Toggle individual device state"""
        if self.teensy.connected:
            success = self.teensy.set_output(device, state)
            print(f"Toggle {device} to {state}, success: {success}")

        button = self.device_controls[device]['toggle']
        if state:
            button.setText("ON")
            button.setStyleSheet("QPushButton { background-color: #51cf66; color: white; font-weight: bold; }")
        else:
            button.setText("OFF")
            button.setStyleSheet("QPushButton { background-color: #ff6b6b; color: white; font-weight: bold; }")

    def toggle_lock(self):
        """Toggle system lock"""
        state = self.lock_btn.isChecked()
        if self.teensy.connected:
            self.teensy.set_lock(state)

        if state:
            self.lock_btn.setText("LOCKED")
            self.lock_btn.setStyleSheet(
                "QPushButton { background-color: #ff6b6b; color: white; font-weight: bold; min-height: 35px; }")
        else:
            self.lock_btn.setText("UNLOCK")
            self.lock_btn.setStyleSheet("QPushButton { min-height: 35px; font-weight: bold; }")

    def toggle_safety_stop(self):
        """Toggle safety stop"""
        state = self.safety_btn.isChecked()
        if self.teensy.connected:
            self.teensy.set_safety_stop(state)

        if state:
            self.safety_btn.setText("STOPPED")
            self.safety_btn.setStyleSheet(
                "QPushButton { background-color: #ff4444; color: white; font-weight: bold; min-height: 35px; }")
        else:
            self.safety_btn.setText("NORMAL")
            self.safety_btn.setStyleSheet("QPushButton { min-height: 35px; font-weight: bold; }")

    def toggle_streaming(self):
        """Toggle data streaming - FIXED to initialize plots"""
        if self.stream_btn.isChecked():
            if self.teensy.connected:
                interval = self.settings.value("polling_rate", 1000, int)
                if self.teensy.start_streaming(interval):
                    self.stream_btn.setText("Stop Stream")
                    self.live_mode_radio.setChecked(True)
                    self.on_mode_changed()
                    self.clear_live_data()

                    # FIXED: Force plot initialization when streaming starts
                    self.plot_initialized = False
                    self.plot_layout_stable = False

                    # Create initial empty plots to prevent black screen
                    QtCore.QTimer.singleShot(100, self._initialize_streaming_plots)
                else:
                    self.stream_btn.setChecked(False)
            else:
                QtWidgets.QMessageBox.warning(self, "Not Connected", "Please connect to Teensy first")
                self.stream_btn.setChecked(False)
        else:
            if self.teensy.connected:
                self.teensy.stop_streaming()
            self.stream_btn.setText("Start Stream")

            def _initialize_streaming_plots(self):
                """Initialize plots when streaming starts - NEW METHOD"""
                # Force creation of initial plots with dummy data
                selected_types = self.get_selected_types()
                if not selected_types:
                    return

                current_tab = self.plotTabWidget.currentIndex()

                # Create initial plots structure
                if current_tab == 0:  # All tab
                    self.all_plot_widget.clear()
                    self.plots.clear()
                    self.curves.clear()

                    line_thickness = self.settings.value("line_thickness", 2, int)
                    show_grid = self.settings.value("show_grid", True, bool)

                    # Create empty plots for each data type
                    for i, data_type in enumerate(selected_types):
                        p = self.all_plot_widget.addPlot(row=i, col=0)
                        p.setContentsMargins(10, 10, 10, 10)
                        p.showGrid(y=show_grid, x=show_grid, alpha=0.3)
                        p.setLabel('left', self.format_type_name(data_type))
                        if i == len(selected_types) - 1:
                            p.setLabel('bottom', 'Time (s)')

                        # Set default Y-axis range
                        if data_type == 'stat':
                            p.setYRange(-0.1, 1.1, padding=0)
                        else:
                            default_range = self.get_y_range_for_type(data_type)
                            p.setYRange(default_range[0], default_range[1], padding=0)

                        # Set initial X-axis range
                        p.setXRange(0, 10, padding=0)  # Start with 10 second window

                        # Create empty curves for each device
                        for j, device in enumerate(self.devices):
                            color = self.get_device_color(device, data_type)
                            curve = p.plot(
                                [],  # Empty data initially
                                [],
                                pen=pg.mkPen(color=color, width=line_thickness),
                                name=device
                            )
                            self.curves[f"{device}_{data_type}"] = curve

                        # Add legend to first plot
                        if i == 0:
                            legend = p.addLegend(offset=(10, 10))
                            for device in self.devices:
                                curve_key = f"{device}_{data_type}"
                                if curve_key in self.curves:
                                    curve = self.curves[curve_key]
                                    legend.addItem(curve, device)

                        # Link x-axes
                        if i > 0:
                            first_plot_key = f"all_{selected_types[0]}"
                            if first_plot_key in self.plots:
                                p.setXLink(self.plots[first_plot_key])

                        p.enableAutoRange(axis='x', enable=False)
                        p.enableAutoRange(axis='y', enable=False)
                        p.sigRangeChanged.connect(self.on_plot_range_changed)

                        self.plots[f"all_{data_type}"] = p

                else:  # Individual device tab
                    device = self.devices[current_tab - 1]
                    plot_widget = self.device_plot_widgets[device]
                    plot_widget.clear()

                    # Clear device-specific plots
                    keys_to_remove = [k for k in self.plots.keys() if k.startswith(f"{device}_")]
                    for key in keys_to_remove:
                        del self.plots[key]
                    keys_to_remove = [k for k in self.curves.keys() if k.startswith(f"{device}_")]
                    for key in keys_to_remove:
                        del self.curves[key]

                    line_thickness = self.settings.value("line_thickness", 2, int)
                    show_grid = self.settings.value("show_grid", True, bool)
                    color_pool = [(31, 119, 180), (255, 127, 14), (44, 160, 44), (214, 39, 40)]

                    valid_plots = 0
                    for i, data_type in enumerate(selected_types):
                        field_key = f"{device}_{data_type}"

                        p = plot_widget.addPlot(row=valid_plots, col=0)
                        p.setContentsMargins(10, 10, 10, 10)
                        p.showGrid(y=show_grid, x=show_grid, alpha=0.3)

                        color = color_pool[valid_plots % len(color_pool)]

                        curve = p.plot(
                            [],  # Empty data initially
                            [],
                            pen=pg.mkPen(color=color, width=line_thickness),
                            name=field_key
                        )

                        # Set default ranges
                        if data_type == 'stat':
                            p.setYRange(-0.1, 1.1, padding=0)
                        else:
                            default_range = self.get_y_range_for_type(data_type)
                            p.setYRange(default_range[0], default_range[1], padding=0)

                        p.setXRange(0, 10, padding=0)
                        p.enableAutoRange(axis='x', enable=False)
                        p.enableAutoRange(axis='y', enable=False)

                        p.setLabel('left', self.format_axis_label(device, data_type))
                        if valid_plots == len(selected_types) - 1:
                            p.setLabel('bottom', 'Time (s)')

                        # Link x-axes
                        if valid_plots > 0:
                            first_plot_key = f"{device}_{selected_types[0]}"
                            if first_plot_key in self.plots:
                                p.setXLink(self.plots[first_plot_key])

                        p.sigRangeChanged.connect(self.on_plot_range_changed)

                        self.plots[f"{device}_{data_type}"] = p
                        self.curves[field_key] = curve
                        valid_plots += 1

                # Mark as initialized
                self.plot_initialized = True
                self.plot_layout_stable = True
                print("Streaming plots initialized")  # Debug

    def load_selected_script(self):
        """Load the selected script"""
        script_name = self.script_combo.currentText()
        if script_name and self.teensy.connected:
            self.teensy.load_script(script_name)

    def on_mode_changed(self):
        """Handle data mode change"""
        self.live_mode = self.live_mode_radio.isChecked()
        if self.live_mode:
            self.clear_live_data()
            self.file_info_label.setText("<b>Live Mode Active</b>")
        else:
            self.file_info_label.setText("<b>File Analysis Mode</b>")
        self.schedule_plot_update()

    # FIXED: New smooth data handling methods
    def clear_live_data(self):
        """Clear live data arrays"""
        if self.live_mode:
            self.live_data_points.clear()
            self.live_times.clear()
            self.live_channels.clear()
            self.all_fields = []
            self.last_data_time = time.time()
            self.data_buffer.clear()  # Clear buffer too
            self.plot_curves_cache.clear()  # Clear curve cache
            self.plot_initialized = False  # Reset initialization flag
        else:
            self.data_points = []
            self.times = []
            self.channels = {}
            self.all_fields = []
            self.current_file_path = None
            self.data_json = None
            self.file_info_label.setText("<b>File Analysis Mode</b>")

        self.schedule_plot_update()

    def insert_data_by_timestamp(self, new_data_point, new_time):
        """Insert data point in correct timestamp order"""
        if not self.live_times:
            self.live_data_points.append(new_data_point)
            self.live_times.append(new_time)
            return

        # For performance, just append if it's newer than the last point
        if new_time >= self.live_times[-1]:
            self.live_data_points.append(new_data_point)
            self.live_times.append(new_time)
        else:
            # Find insertion point (rare case)
            insertion_idx = 0
            for i, existing_time in enumerate(self.live_times):
                if new_time <= existing_time:
                    insertion_idx = i
                    break
            else:
                insertion_idx = len(self.live_times)

            if insertion_idx == len(self.live_times):
                self.live_data_points.append(new_data_point)
                self.live_times.append(new_time)
            else:
                self.live_data_points.insert(insertion_idx, new_data_point)
                self.live_times.insert(insertion_idx, new_time)

    def on_live_data_received(self, data):
        """Handle incoming live data - BUFFERED for smooth updates"""
        if not self.live_mode:
            return

        try:
            timestamp = data.get('timestamp', '')
            devices_data = data.get('devices', [])

            current_time = time.time()
            if self.last_data_time == 0:
                self.last_data_time = current_time
            time_sec = current_time - self.last_data_time

            data_point = {'time': time_sec * 1000}

            # Process device data
            for device_data in devices_data:
                device_name = device_data.get('name', '')
                display_name = DEVICE_DISPLAY_MAP.get(device_name, device_name)

                if display_name in DEVICES or device_name == 'Total':
                    device_key = display_name if display_name in DEVICES else device_name

                    voltage = float(device_data.get('voltage', 0.0))
                    current = float(device_data.get('current', 0.0))
                    power = float(device_data.get('power', 0.0))
                    state = 1.0 if device_data.get('state', False) else 0.0

                    data_point[f"{device_key}_volt"] = voltage
                    data_point[f"{device_key}_curr"] = current
                    data_point[f"{device_key}_pow"] = power
                    data_point[f"{device_key}_stat"] = state

                    # Update device controls
                    if display_name in self.device_controls:
                        controls = self.device_controls[display_name]
                        controls['status'].setText(f"{voltage:.1f}V {current:.3f}A")

                        device_state = device_data.get('state', False)
                        controls['toggle'].blockSignals(True)
                        controls['toggle'].setChecked(device_state)
                        if device_state:
                            controls['toggle'].setText("ON")
                            controls['toggle'].setStyleSheet(
                                "QPushButton { background-color: #51cf66; color: white; font-weight: bold; }")
                        else:
                            controls['toggle'].setText("OFF")
                            controls['toggle'].setStyleSheet(
                                "QPushButton { background-color: #ff6b6b; color: white; font-weight: bold; }")
                        controls['toggle'].blockSignals(False)

            # FIXED: Add to buffer instead of immediate processing
            self.data_buffer.append((data_point, time_sec))

            # Update info label less frequently
            if len(self.data_buffer) % 5 == 0:
                num_points = len(self.live_data_points)
                duration = self.live_times[-1] if self.live_times else 0
                self.update_file_info_live(num_points, duration)

        except Exception as e:
            error_msg = f"Error processing live data: {str(e)}"
            print(f"Full traceback: {traceback.format_exc()}")
            self.on_error_occurred(error_msg)

    def _process_data_buffer(self):
        """Process buffered data for smooth updates - NEW METHOD"""
        if not self.data_buffer or not self.live_mode:
            return

        # Process all buffered data
        while self.data_buffer:
            data_point, time_sec = self.data_buffer.popleft()

            # Insert data
            self.insert_data_by_timestamp(data_point, time_sec)

            # Initialize channels if needed
            if not self.all_fields:
                self.all_fields = [k for k in data_point.keys() if k != 'time']
                for field in self.all_fields:
                    self.live_channels[field] = deque()

            # Update channels
            for field in self.all_fields:
                if field not in self.live_channels:
                    self.live_channels[field] = deque()

                value = data_point.get(field, 0.0)
                self.live_channels[field].append(value)

            # Handle data overflow
            data_mode = self.settings.value("data_mode", 0, int)
            if len(self.live_data_points) > self.max_live_points:
                if data_mode == 0:  # Scroll mode
                    self.live_data_points.popleft()
                    self.live_times.popleft()
                    for field in self.all_fields:
                        if len(self.live_channels[field]) > 0:
                            self.live_channels[field].popleft()

        # FIXED: Update plots with incremental data
        if len(self.live_data_points) > 0:
            self._update_plots_incremental()

    def _update_plots_incremental(self):
        """FIXED: Incremental plot updates for smooth animation"""
        if not self.live_times or not self.live_channels:
            return

        selected_types = self.get_selected_types()
        if not selected_types:
            return

        current_tab = self.plotTabWidget.currentIndex()

        # Initialize plots if needed
        if not self.plot_initialized:
            self.update_plots()
            self.plot_initialized = True
            self.plot_layout_stable = True
            return

        # Only do incremental updates if we have stable layout
        if not self.plot_layout_stable:
            return

        # Apply window mode to get current data window
        times = list(self.live_times)
        channels = {}
        for field, data in self.live_channels.items():
            channels[field] = list(data)

        # FIXED: Apply window mode here for incremental updates too
        times, channels = self.apply_window_mode(times, channels)
        times_np = np.array(times)

        try:
            if current_tab == 0:  # All tab
                self._update_all_plots_incremental(times_np, channels, selected_types)
            else:  # Individual device tab
                device = self.devices[current_tab - 1]
                self._update_device_plots_incremental(device, times_np, channels, selected_types)
        except Exception as e:
            # Fallback to full update if incremental fails
            print(f"Incremental update failed, falling back to full update: {e}")
            self.plot_initialized = False
            self.plot_layout_stable = False

    def _update_all_plots_incremental(self, times_np, selected_types):
        """Incrementally update All tab plots"""
        for data_type in selected_types:
            plot_key = f"all_{data_type}"
            if plot_key not in self.plots:
                continue

            for device in self.devices:
                device_key = device
                field_key = f"{device_key}_{data_type}"
                curve_key = f"{device}_{data_type}"

                if field_key in self.live_channels and curve_key in self.curves:
                    y_data = np.array(list(self.live_channels[field_key]))

                    if len(y_data) == len(times_np) and len(y_data) > 0:
                        # Update curve data
                        curve = self.curves[curve_key]
                        curve.setData(times_np, y_data)

                        # Update X-axis range only
                        plot = self.plots[plot_key]
                        if len(times_np) > 1:
                            x_min, x_max = times_np[0], times_np[-1]
                            x_padding = (x_max - x_min) * 0.02
                            plot.setXRange(x_min - x_padding, x_max + x_padding, padding=0)

    def _update_device_plots_incremental(self, device, times_np, selected_types):
        """Incrementally update device tab plots"""
        device_key = device

        for data_type in selected_types:
            plot_key = f"{device}_{data_type}"
            field_key = f"{device_key}_{data_type}"

            if plot_key in self.plots and field_key in self.curves and field_key in self.live_channels:
                y_data = np.array(list(self.live_channels[field_key]))

                if len(y_data) == len(times_np) and len(y_data) > 0:
                    # Update curve data
                    curve = self.curves[field_key]
                    curve.setData(times_np, y_data)

                    # Update X-axis range only
                    plot = self.plots[plot_key]
                    if len(times_np) > 1:
                        x_min, x_max = times_np[0], times_np[-1]
                        x_padding = (x_max - x_min) * 0.02
                        plot.setXRange(x_min - x_padding, x_max + x_padding, padding=0)

    def update_file_info_live(self, num_points, duration):
        """Update file info for live data"""
        info_text = f"""<b>Live Data Stream:</b><br>
Data Points: {num_points:,}<br>
Duration: {duration:.1f}s<br>
Connected: {'Yes' if self.teensy.connected else 'No'}"""

        self.file_info_label.setText(info_text)

    def on_status_received(self, status):
        """Handle system status updates"""
        self.system_status = status

        try:
            # Lock status
            locked = status.get('locked', False)
            self.lock_btn.blockSignals(True)
            self.lock_btn.setChecked(locked)
            if locked:
                self.lock_btn.setText("LOCKED")
                self.lock_btn.setStyleSheet(
                    "QPushButton { background-color: #ff6b6b; color: white; font-weight: bold; min-height: 35px; }")
            else:
                self.lock_btn.setText("UNLOCK")
                self.lock_btn.setStyleSheet("QPushButton { min-height: 35px; font-weight: bold; }")
            self.lock_btn.blockSignals(False)

            # Safety stop status
            safety_stop = status.get('safety_stop', False)
            self.safety_btn.blockSignals(True)
            self.safety_btn.setChecked(safety_stop)
            if safety_stop:
                self.safety_btn.setText("STOPPED")
                self.safety_btn.setStyleSheet(
                    "QPushButton { background-color: #ff4444; color: white; font-weight: bold; min-height: 35px; }")
            else:
                self.safety_btn.setText("NORMAL")
                self.safety_btn.setStyleSheet("QPushButton { min-height: 35px; font-weight: bold; }")
            self.safety_btn.blockSignals(False)

            # Update info display
            version = status.get('version', 'Unknown')
            ip_address = status.get('ip_address', 'Unknown')
            self.statusBar().showMessage(f"Connected to {version} at {ip_address}")

        except Exception as e:
            self.on_error_occurred(f"Error processing status: {str(e)}")

    def on_script_list_received(self, scripts):
        """Handle script list updates"""
        self.available_scripts = scripts

        self.script_combo.clear()
        for script in scripts:
            script_name = script.get('name', 'Unknown')
            self.script_combo.addItem(script_name)

    # File operations and analysis methods (keeping original implementations)
    def open_file(self):
        """Open a JSON data file"""
        last_dir = self.settings.value("last_directory", str(Path.home()))

        file_path, _ = QtWidgets.QFileDialog.getOpenFileName(
            self,
            "Open Power Data File",
            last_dir,
            "JSON files (*.json);;All files (*.*)"
        )

        if file_path:
            self.load_file(file_path)

    def load_file(self, file_path):
        """Load and validate a JSON data file"""
        try:
            self.statusBar().showMessage(f"Loading {os.path.basename(file_path)}...")

            if not os.path.exists(file_path):
                raise FileNotFoundError(f"File not found: {file_path}")

            if not os.access(file_path, os.R_OK):
                raise PermissionError(f"Cannot read file: {file_path}")

            with open(file_path, "r", encoding='utf-8') as f:
                file_content = f.read()

            if self.detect_json_corruption(file_content):
                file_content = self.attempt_json_repair(file_content)

            try:
                self.data_json = json.loads(file_content)
            except json.JSONDecodeError as e:
                raise ValueError(f"Invalid JSON format: {str(e)}")

            self.extract_script_info(self.data_json)
            corruption_info = self.validate_and_check_corruption(self.data_json)

            if corruption_info['has_corruption']:
                if self.show_corruption_dialog(corruption_info):
                    self.fix_corrupted_data(file_path, corruption_info)

            if not self.validate_json_structure(self.data_json):
                raise ValueError("Invalid JSON structure - missing required fields or devices")

            self.process_data()

            # Switch to file mode
            self.file_mode_radio.setChecked(True)
            self.on_mode_changed()

            self.current_file_path = file_path
            self.update_file_info()
            self.schedule_plot_update()

            self.settings.setValue("last_directory", os.path.dirname(file_path))
            self.add_to_recent_files(file_path)

            self.update_side_panel_for_current_tab()

            status_msg = f"Loaded {os.path.basename(file_path)} successfully"
            if self.corrupted_indices:
                status_msg += f" ({len(self.corrupted_indices)} corrupted points found)"
            self.statusBar().showMessage(status_msg)

        except Exception as e:
            QtWidgets.QMessageBox.critical(
                self,
                "Error Loading File",
                f"Failed to load file: {file_path}\n\nError: {str(e)}"
            )
            self.statusBar().showMessage("Failed to load file")

    def detect_json_corruption(self, content):
        """Detect common JSON corruption patterns"""
        if content.count('{') != content.count('}'):
            return True
        if content.count('[') != content.count(']'):
            return True
        if re.search(r',\s*[}\]]', content):
            return True
        data_start = content.find('"data":[')
        if data_start != -1:
            after_data_start = content[data_start:]
            if re.search(r'[,{]\s*$', after_data_start.rstrip().rstrip('}')):
                return True
        return False

    def attempt_json_repair(self, content):
        """Attempt to repair common JSON corruption issues"""
        content = re.sub(r',(\s*[}\]])', r'\1', content)
        if '"data":[' in content and not content.rstrip().endswith(']}'):
            last_brace = content.rfind('}')
            if last_brace != -1:
                content = content[:last_brace + 1] + '\n],\n"duration_sec": 0\n}'
        return content

    def extract_script_info(self, data):
        """Extract script information from JSON data"""
        self.script_info = {}
        if isinstance(data, dict):
            self.script_info['using_script'] = data.get('using_script', 0)
            self.script_info['timestamp'] = data.get('timestamp', 'Unknown')
            self.script_info['duration_sec'] = data.get('duration_sec', 0)
            script_config = data.get('script_config', {})
            if script_config:
                self.script_info['script_name'] = script_config.get('name', 'Unknown')
                self.script_info['t_start'] = script_config.get('tstart', 0)
                self.script_info['t_end'] = script_config.get('tend', 0)
                self.script_info['auto_record'] = script_config.get('record', False)
            else:
                self.script_info['script_name'] = 'No script used'
                self.script_info['t_start'] = 0
                self.script_info['t_end'] = 0
                self.script_info['auto_record'] = False

    def validate_and_check_corruption(self, data):
        """Validate data and check for corruption"""
        corruption_info = {
            'has_corruption': False,
            'corrupted_indices': [],
            'total_points': 0,
            'corruption_details': []
        }
        if not isinstance(data, dict) or 'data' not in data:
            return corruption_info
        data_points = data['data']
        if not isinstance(data_points, list):
            return corruption_info
        corruption_info['total_points'] = len(data_points)
        self.original_data_points_count = len(data_points)
        corrupted_indices = []
        corruption_details = []
        for i, point in enumerate(data_points):
            if not isinstance(point, dict):
                corrupted_indices.append(i)
                corruption_details.append(f"Point {i}: Not a valid object")
                continue
            if 'time' not in point:
                corrupted_indices.append(i)
                corruption_details.append(f"Point {i}: Missing 'time' field")
                continue
            try:
                time_val = float(point['time'])
                if time_val < 0 or not np.isfinite(time_val):
                    corrupted_indices.append(i)
                    corruption_details.append(f"Point {i}: Invalid time value: {time_val}")
                    continue
            except (ValueError, TypeError):
                corrupted_indices.append(i)
                corruption_details.append(f"Point {i}: Time field is not numeric")
                continue
            for device in DEVICES:
                for data_type in DATA_TYPES:
                    field_key = f"{device}_{data_type}"
                    if field_key in point:
                        try:
                            val = float(point[field_key])
                            if not np.isfinite(val):
                                corrupted_indices.append(i)
                                corruption_details.append(f"Point {i}: Invalid {field_key} value: {val}")
                                break
                        except (ValueError, TypeError):
                            corrupted_indices.append(i)
                            corruption_details.append(f"Point {i}: {field_key} field is not numeric")
                            break
                else:
                    continue
                break
        corrupted_indices = list(dict.fromkeys(corrupted_indices))
        if corrupted_indices:
            corruption_info['has_corruption'] = True
            corruption_info['corrupted_indices'] = corrupted_indices
            corruption_info['corruption_details'] = corruption_details[:10]
        self.corrupted_indices = corrupted_indices
        return corruption_info

    def show_corruption_dialog(self, corruption_info):
        """Show dialog asking user if they want to fix corrupted data"""
        num_corrupted = len(corruption_info['corrupted_indices'])
        total_points = corruption_info['total_points']
        dialog = QtWidgets.QMessageBox(self)
        dialog.setWindowTitle("Data Corruption Detected")
        dialog.setIcon(QtWidgets.QMessageBox.Icon.Warning)
        text = f"""Data corruption detected in the file:

• Total data points: {total_points:,}
• Corrupted data points: {num_corrupted:,} ({num_corrupted / total_points * 100:.1f}%)

Corruption details (first 10):"""
        for detail in corruption_info['corruption_details']:
            text += f"\n• {detail}"
        if len(corruption_info['corruption_details']) < len(corruption_info['corrupted_indices']):
            remaining = len(corruption_info['corrupted_indices']) - len(corruption_info['corruption_details'])
            text += f"\n• ... and {remaining} more corrupted points"
        text += f"""

Would you like to:
• Fix the file by removing corrupted data points and save the cleaned data back to the original file?
• Or continue with the corrupted data (not recommended)?"""
        dialog.setText(text)
        dialog.setStandardButtons(QtWidgets.QMessageBox.StandardButton.Yes | QtWidgets.QMessageBox.StandardButton.No)
        dialog.button(QtWidgets.QMessageBox.StandardButton.Yes).setText("Fix and Save")
        dialog.button(QtWidgets.QMessageBox.StandardButton.No).setText("Continue with Corrupted Data")
        result = dialog.exec()
        return result == QtWidgets.QMessageBox.StandardButton.Yes

    def fix_corrupted_data(self, file_path, corruption_info):
        """Fix corrupted data by removing bad points and save back to file"""
        try:
            backup_path = file_path + ".backup"
            import shutil
            shutil.copy2(file_path, backup_path)
            data_points = self.data_json['data']
            for idx in reversed(corruption_info['corrupted_indices']):
                if 0 <= idx < len(data_points):
                    del data_points[idx]
            if 'duration_sec' in self.data_json:
                if data_points and len(data_points) > 1:
                    start_time = data_points[0].get('time', 0)
                    end_time = data_points[-1].get('time', 0)
                    self.data_json['duration_sec'] = int((end_time - start_time) / 1000)
            with open(file_path, 'w', encoding='utf-8') as f:
                json.dump(self.data_json, f, indent=2)
            num_removed = len(corruption_info['corrupted_indices'])
            remaining_points = len(data_points)
            QtWidgets.QMessageBox.information(
                self, "Data Fixed Successfully",
                f"""Data corruption fixed successfully!

• Removed {num_removed:,} corrupted data points
• Remaining data points: {remaining_points:,}
• Original file backed up as: {os.path.basename(backup_path)}
• Cleaned data saved to original file

The file has been automatically reloaded with the cleaned data."""
            )
            self.corrupted_indices = []
        except Exception as e:
            QtWidgets.QMessageBox.critical(
                self, "Error Fixing Data",
                f"Failed to fix corrupted data:\n{str(e)}"
            )

    def validate_json_structure(self, data):
        """Validate the JSON data structure"""
        try:
            if not isinstance(data, dict):
                return False
            if "data" not in data:
                return False
            data_points = data["data"]
            if not isinstance(data_points, list) or len(data_points) == 0:
                return False
            sample = data_points[0]
            if not isinstance(sample, dict):
                return False
            if "time" not in sample:
                return False
            expected_fields = []
            for device in self.devices:
                device_key = device
                for data_type in self.types:
                    expected_fields.append(f"{device_key}_{data_type}")
            found_fields = [field for field in expected_fields if field in sample]
            if len(found_fields) == 0:
                return False
            return True
        except Exception:
            return False

    def process_data(self):
        """Process the loaded JSON data"""
        self.data_points = self.data_json["data"]
        sample = self.data_points[0]
        self.all_fields = [k for k in sample.keys() if k != "time"]
        self.times = [dp["time"] / 1000.0 for dp in self.data_points]
        self.channels = {}
        for k in self.all_fields:
            data_array = [dp[k] for dp in self.data_points]
            if k.endswith('_curr'):
                data_array = [val / 1000.0 for val in data_array]
            data_array = self.apply_data_filtering(data_array)
            self.channels[k] = data_array

    def update_file_info(self):
        """Update the file information display"""
        if self.current_file_path:
            file_name = os.path.basename(self.current_file_path)
            file_size = os.path.getsize(self.current_file_path)
            file_size_mb = file_size / (1024 * 1024)
            info_text = f"""<b>Current File:</b><br>
{file_name}<br>
<b>Size:</b> {file_size_mb:.2f} MB<br>
<b>Data Points:</b> {len(self.data_points):,}"""
            if self.corrupted_indices:
                info_text += f"<br><b>Corrupted:</b> {len(self.corrupted_indices):,}"
            info_text += f"<br><b>Duration:</b> {self.times[-1] - self.times[0]:.1f}s"
            self.file_info_label.setText(info_text)
        else:
            self.file_info_label.setText("<b>No file loaded</b>")

    def get_current_data(self):
        """Get current data arrays based on mode"""
        if self.live_mode:
            times = list(self.live_times)
            channels = {}
            for field, data in self.live_channels.items():
                filtered_data = self.apply_data_filtering(list(data))
                channels[field] = filtered_data
            return times, channels
        else:
            return self.times, self.channels

    def get_selected_types(self):
        """Get currently selected data types"""
        return [typ for typ, cb in self.field_checkboxes.items() if cb.isChecked()]

    def on_tab_changed(self, index):
        """Handle tab change"""
        self.current_tab_index = index
        # Force full update when tab changes
        self.plot_initialized = False
        self.plot_layout_stable = False
        self.schedule_plot_update()
        self.update_side_panel_for_current_tab()

    def update_plots(self):
        """Update all plot displays - FIXED for stable layout"""
        times, channels = self.get_current_data()

        if not times or not channels:
            return

        selected_types = self.get_selected_types()
        if not selected_types:
            return

        current_tab = self.plotTabWidget.currentIndex()

        if current_tab == 0:  # All tab
            self.update_all_plots(times, channels, selected_types)
        else:  # Individual device tab
            device = self.devices[current_tab - 1]
            self.update_device_plots(device, times, channels, selected_types)

        # Mark as initialized and stable
        self.plot_initialized = True
        self.plot_layout_stable = True

    def get_device_color(self, device, data_type):
        """Get color for device from settings"""
        color_str = self.settings.value(f"device_color_{device}", "#1f77b4")
        if color_str.startswith('#'):
            hex_color = color_str[1:]
            r = int(hex_color[0:2], 16)
            g = int(hex_color[2:4], 16)
            b = int(hex_color[4:6], 16)
            return (r, g, b)
        else:
            device_index = self.devices.index(device) if device in self.devices else 0
            return DEFAULT_DEVICE_COLORS[data_type][device_index % len(DEFAULT_DEVICE_COLORS[data_type])]

    def get_y_range_for_type(self, data_type):
        """Get Y-axis range for data type from settings"""
        default_range = DEFAULT_Y_RANGES[data_type]
        min_val = self.settings.value(f"y_range_{data_type}_min", default_range[0], float)
        max_val = self.settings.value(f"y_range_{data_type}_max", default_range[1], float)
        return (min_val, max_val)

    def apply_window_mode(self, times, channels):
        """Apply window mode settings to data - FIXED sliding window logic"""
        window_mode = self.settings.value("window_mode", 0, int)

        if not times or len(times) < 2:
            return times, channels

        if window_mode == 0:  # Growing window
            max_points = self.settings.value("window_max_points", -1, int)
            if max_points > 0 and len(times) > max_points:
                # Keep only the last max_points
                times = times[-max_points:]
                filtered_channels = {}
                for field, data in channels.items():
                    if isinstance(data, (list, deque)):
                        filtered_channels[field] = list(data)[-max_points:]
                    else:
                        filtered_channels[field] = data[-max_points:]
                return times, filtered_channels

        elif window_mode == 1:  # Sliding time window - FIXED
            window_duration = self.settings.value("sliding_window_time", 10.0, float)

            if len(times) > 1:
                # Convert times to seconds if they're in milliseconds
                times_array = np.array(times)
                if times_array[-1] > 1000:  # Assume milliseconds if last time > 1000
                    times_seconds = times_array / 1000.0
                else:
                    times_seconds = times_array

                latest_time = times_seconds[-1]

                # Only apply sliding window if we have more data than the window duration
                if (latest_time - times_seconds[0]) > window_duration:
                    cutoff_time = latest_time - window_duration

                    # Find start index - FIXED to use seconds
                    start_idx = 0
                    for i, t in enumerate(times_seconds):
                        if t >= cutoff_time:
                            start_idx = i
                            break

                    # Debug output
                    print(
                        f"Sliding window: latest={latest_time:.1f}s, cutoff={cutoff_time:.1f}s, start_idx={start_idx}, total_points={len(times)}")

                    # Slice data
                    if start_idx > 0:
                        times = times[start_idx:]
                        filtered_channels = {}
                        for field, data in channels.items():
                            if isinstance(data, (list, deque)):
                                filtered_channels[field] = list(data)[start_idx:]
                            else:
                                filtered_channels[field] = data[start_idx:]
                        return times, filtered_channels

        return times, channels

    def update_all_plots(self, times, channels, selected_types):
        """Update the 'All' tab plots with combined device data - FIXED for stability"""
        if not times or len(times) < 2:
            return

        times, channels = self.apply_window_mode(times, channels)

        # FIXED: Clear and rebuild for stable layout
        self.all_plot_widget.clear()

        # Clear only All tab plots
        keys_to_remove = [k for k in self.plots.keys() if k.startswith("all_")]
        for key in keys_to_remove:
            del self.plots[key]

        keys_to_remove = [k for k in self.curves.keys() if
                          any(device in k for device in DEVICES) and any(typ in k for typ in selected_types)]
        for key in keys_to_remove:
            if key in self.curves:
                del self.curves[key]

        self.clear_crosshairs()

        line_thickness = self.settings.value("line_thickness", 2, int)
        show_grid = self.settings.value("show_grid", True, bool)
        enable_crosshair = self.settings.value("enable_crosshair", True, bool) and self.crosshair_cb.isChecked()

        times_np = np.array(times)

        # FIXED: Create plots with stable sizing
        for i, data_type in enumerate(selected_types):
            p = self.all_plot_widget.addPlot(row=i, col=0)

            # FIXED: Set fixed plot properties to prevent resizing (corrected method)
            p.setContentsMargins(10, 10, 10, 10)
            # Don't set height constraints - let PyQtGraph handle it naturally

            p.showGrid(y=show_grid, x=show_grid, alpha=0.3)
            p.setLabel('left', self.format_type_name(data_type))
            if i == len(selected_types) - 1:
                p.setLabel('bottom', 'Time (s)')

            # Collect data for Y-range calculation
            all_y_data = []
            valid_devices = []

            # Plot each device's data
            for j, device in enumerate(self.devices):
                device_key = device
                field_key = f"{device_key}_{data_type}"

                if field_key in channels and len(channels[field_key]) > 0:
                    y_data = np.array(channels[field_key])

                    if len(y_data) == len(times_np) and np.any(np.isfinite(y_data)):
                        color = self.get_device_color(device, data_type)

                        curve = p.plot(
                            times_np,
                            y_data,
                            pen=pg.mkPen(color=color, width=line_thickness),
                            name=device
                        )

                        self.curves[f"{device}_{data_type}"] = curve

                        finite_data = y_data[np.isfinite(y_data)]
                        if len(finite_data) > 0:
                            all_y_data.extend(finite_data)
                            valid_devices.append(device)

            # FIXED: Set Y-axis range with proper scaling
            if all_y_data:
                if data_type == 'stat':
                    p.setYRange(-0.1, 1.1, padding=0)
                else:
                    y_min = np.min(all_y_data)
                    y_max = np.max(all_y_data)

                    y_range = y_max - y_min
                    if y_range == 0:
                        y_range = abs(y_max) * 0.1 if y_max != 0 else 1.0

                    padding = y_range * 0.1
                    p.setYRange(y_min - padding, y_max + padding, padding=0)
            else:
                default_range = self.get_y_range_for_type(data_type)
                p.setYRange(default_range[0], default_range[1], padding=0)

            # Set X-axis range
            if len(times_np) > 1:
                x_min, x_max = times_np[0], times_np[-1]
                x_padding = (x_max - x_min) * 0.02
                p.setXRange(x_min - x_padding, x_max + x_padding, padding=0)

            # FIXED: Add legend only to the first plot
            if i == 0 and valid_devices:
                legend = p.addLegend(offset=(10, 10))
                for device in valid_devices:
                    curve_key = f"{device}_{data_type}"
                    if curve_key in self.curves:
                        curve = self.curves[curve_key]
                        legend.addItem(curve, device)

            # Link x-axes
            if i > 0:
                first_plot_key = f"all_{selected_types[0]}"
                if first_plot_key in self.plots:
                    p.setXLink(self.plots[first_plot_key])

            # FIXED: Disable auto-range to prevent layout jumping
            p.enableAutoRange(axis='x', enable=False)
            p.enableAutoRange(axis='y', enable=False)

            # Track user interaction for incremental updates
            p.sigRangeChanged.connect(self.on_plot_range_changed)

            self.plots[f"all_{data_type}"] = p

        # Add crosshair if enabled
        if selected_types and enable_crosshair:
            self.add_crosshair_to_all_plot(times, channels, selected_types)

    def on_plot_range_changed(self):
        """Handle plot range changes to track user interaction"""
        self.user_interacting = True
        QtCore.QTimer.singleShot(2000, lambda: setattr(self, 'user_interacting', False))

    def update_device_plots(self, device, times, channels, selected_types):
        """Update individual device tab plots - FIXED for stability"""
        if not times or len(times) < 2:
            return

        times, channels = self.apply_window_mode(times, channels)

        plot_widget = self.device_plot_widgets[device]
        plot_widget.clear()

        # Clear device-specific plots
        keys_to_remove = [k for k in self.plots.keys() if k.startswith(f"{device}_")]
        for key in keys_to_remove:
            del self.plots[key]

        keys_to_remove = [k for k in self.curves.keys() if k.startswith(f"{device}_")]
        for key in keys_to_remove:
            del self.curves[key]

        self.clear_crosshairs()

        device_key = device
        line_thickness = self.settings.value("line_thickness", 2, int)
        show_grid = self.settings.value("show_grid", True, bool)
        enable_crosshair = self.settings.value("enable_crosshair", True, bool) and self.crosshair_cb.isChecked()

        color_pool = [(31, 119, 180), (255, 127, 14), (44, 160, 44), (214, 39, 40)]
        times_np = np.array(times)

        valid_plots = 0
        for i, data_type in enumerate(selected_types):
            field_key = f"{device_key}_{data_type}"

            if field_key in channels and len(channels[field_key]) > 0:
                y_data = np.array(channels[field_key])

                if len(y_data) != len(times_np) or not np.any(np.isfinite(y_data)):
                    continue

                # Create plot - removed height constraints
                p = plot_widget.addPlot(row=valid_plots, col=0)

                # FIXED: Set fixed plot properties (removed problematic height setting)
                p.setContentsMargins(10, 10, 10, 10)

                p.showGrid(y=show_grid, x=show_grid, alpha=0.3)

                color = color_pool[valid_plots % len(color_pool)]

                curve = p.plot(
                    times_np,
                    y_data,
                    pen=pg.mkPen(color=color, width=line_thickness),
                    name=field_key
                )

                # FIXED: Set Y-axis range properly
                finite_data = y_data[np.isfinite(y_data)]
                if len(finite_data) > 0:
                    if data_type == 'stat':
                        p.setYRange(-0.1, 1.1, padding=0)
                    else:
                        y_min = np.min(finite_data)
                        y_max = np.max(finite_data)

                        y_range = y_max - y_min
                        if y_range == 0:
                            y_range = abs(y_max) * 0.1 if y_max != 0 else 1.0

                        padding = y_range * 0.1
                        p.setYRange(y_min - padding, y_max + padding, padding=0)

                # Set X-axis range
                if len(times_np) > 1:
                    x_min, x_max = times_np[0], times_np[-1]
                    x_padding = (x_max - x_min) * 0.02
                    p.setXRange(x_min - x_padding, x_max + x_padding, padding=0)

                # Disable auto-range
                p.enableAutoRange(axis='x', enable=False)
                p.enableAutoRange(axis='y', enable=False)

                p.setLabel('left', self.format_axis_label(device, data_type))
                if valid_plots == len([t for t in selected_types if f"{device_key}_{t}" in channels]) - 1:
                    p.setLabel('bottom', 'Time (s)')

                # Link x-axes
                if valid_plots > 0:
                    first_plot_key = f"{device}_{selected_types[0]}"
                    if first_plot_key in self.plots:
                        p.setXLink(self.plots[first_plot_key])

                p.sigRangeChanged.connect(self.on_plot_range_changed)

                self.plots[f"{device}_{data_type}"] = p
                self.curves[field_key] = curve

                valid_plots += 1

        # Add crosshair if enabled
        if valid_plots > 0 and enable_crosshair:
            self.add_crosshair_to_device_plot(device, times, channels, selected_types, color_pool)

    def add_crosshair_to_all_plot(self, times, channels, selected_types):
        """Add crosshair and floating label to the All tab"""
        if not selected_types:
            return

        first_type = selected_types[0]
        plot_key = f"all_{first_type}"

        if plot_key not in self.plots:
            return

        p0 = self.plots[plot_key]

        # Create crosshair items
        vLine = pg.InfiniteLine(angle=90, movable=False, pen=pg.mkPen('k', width=1, style=dash_style))
        hLine = pg.InfiniteLine(angle=0, movable=False, pen=pg.mkPen('k', width=1, style=dash_style))

        vLine.setVisible(False)
        hLine.setVisible(False)

        p0.addItem(vLine, ignoreBounds=True)
        p0.addItem(hLine, ignoreBounds=True)

        label = pg.TextItem("", anchor=(0, 1), border='w', fill=(0, 0, 0, 150))
        label.setVisible(False)
        p0.addItem(label, ignoreBounds=True)

        self.crosshair_items[plot_key] = [vLine, hLine, label]

        times_np = np.array(times)
        show_label = self.settings.value("show_crosshair_label", True, bool)

        def mouseMoved(evt):
            pos = evt[0]
            if p0.sceneBoundingRect().contains(pos):
                mousePoint = p0.vb.mapSceneToView(pos)
                x = mousePoint.x()
                y = mousePoint.y()

                vLine.setVisible(True)
                hLine.setVisible(True)
                vLine.setPos(x)
                hLine.setPos(y)

                if show_label:
                    idx = np.searchsorted(times_np, x)
                    if idx >= len(times_np):
                        idx = len(times_np) - 1
                    if idx < 0:
                        idx = 0

                    time_val_sec = times_np[idx]
                    text = f"<span style='font-size: 12pt'>Time: {time_val_sec:.3f} s</span><br>"

                    for data_type in selected_types:
                        text += f"<br><b>{self.format_type_name(data_type)}:</b><br>"

                        for j, device in enumerate(self.devices):
                            device_key = device
                            field_key = f"{device_key}_{data_type}"

                            if field_key in channels and idx < len(channels[field_key]):
                                yval = channels[field_key][idx]
                                color = self.get_device_color(device, data_type)
                                color_hex = '#%02x%02x%02x' % color
                                text += f"<span style='color: {color_hex}'>{device}: {yval:.3f}</span><br>"

                    label.setHtml(text)
                    label.setVisible(True)

                    view_range = p0.viewRange()
                    x_range = view_range[0]
                    y_range = view_range[1]

                    label_x = min(x + (x_range[1] - x_range[0]) * 0.02, x_range[1] - (x_range[1] - x_range[0]) * 0.3)
                    label_y = max(y, y_range[0] + (y_range[1] - y_range[0]) * 0.1)

                    label.setPos(label_x, label_y)
            else:
                vLine.setVisible(False)
                hLine.setVisible(False)
                if show_label:
                    label.setVisible(False)

        for i, data_type in enumerate(selected_types):
            current_plot_key = f"all_{data_type}"
            if current_plot_key in self.plots:
                plot = self.plots[current_plot_key]
                proxy = pg.SignalProxy(plot.scene().sigMouseMoved, rateLimit=60, slot=mouseMoved)
                plot._crosshair_proxy = proxy

    def add_crosshair_to_device_plot(self, device, times, channels, selected_types, color_pool):
        """Add crosshair and floating label to device plot"""
        if not selected_types:
            return

        first_type = selected_types[0]
        plot_key = f"{device}_{first_type}"

        if plot_key not in self.plots:
            return

        p0 = self.plots[plot_key]

        vLine = pg.InfiniteLine(angle=90, movable=False, pen=pg.mkPen('k', width=1, style=dash_style))
        hLine = pg.InfiniteLine(angle=0, movable=False, pen=pg.mkPen('k', width=1, style=dash_style))

        vLine.setVisible(False)
        hLine.setVisible(False)

        p0.addItem(vLine, ignoreBounds=True)
        p0.addItem(hLine, ignoreBounds=True)

        label = pg.TextItem("", anchor=(0, 1), border='w', fill=(0, 0, 0, 150))
        label.setVisible(False)
        p0.addItem(label, ignoreBounds=True)

        self.crosshair_items[plot_key] = [vLine, hLine, label]

        times_np = np.array(times)
        device_key = device
        show_label = self.settings.value("show_crosshair_label", True, bool)

        def mouseMoved(evt):
            pos = evt[0]
            if p0.sceneBoundingRect().contains(pos):
                mousePoint = p0.vb.mapSceneToView(pos)
                x = mousePoint.x()
                y = mousePoint.y()

                vLine.setVisible(True)
                hLine.setVisible(True)
                vLine.setPos(x)
                hLine.setPos(y)

                if show_label:
                    idx = np.searchsorted(times_np, x)
                    if idx >= len(times_np):
                        idx = len(times_np) - 1
                    if idx < 0:
                        idx = 0

                    time_val_sec = times_np[idx]
                    text = f"<span style='font-size: 12pt'>Time: {time_val_sec:.3f} s</span><br>"

                    for i, data_type in enumerate(selected_types):
                        field_key = f"{device_key}_{data_type}"
                        if field_key in channels and idx < len(channels[field_key]):
                            yval = channels[field_key][idx]
                            color = color_pool[i % len(color_pool)]
                            color_hex = '#%02x%02x%02x' % color
                            text += f"<span style='color: {color_hex}'>{self.format_axis_label(device, data_type)}: {yval:.3f}</span><br>"

                    label.setHtml(text)
                    label.setVisible(True)

                    view_range = p0.viewRange()
                    x_range = view_range[0]
                    y_range = view_range[1]

                    label_x = min(x + (x_range[1] - x_range[0]) * 0.02, x_range[1] - (x_range[1] - x_range[0]) * 0.3)
                    label_y = max(y, y_range[0] + (y_range[1] - y_range[0]) * 0.1)

                    label.setPos(label_x, label_y)
            else:
                vLine.setVisible(False)
                hLine.setVisible(False)
                if show_label:
                    label.setVisible(False)

        for i, data_type in enumerate(selected_types):
            current_plot_key = f"{device}_{data_type}"
            if current_plot_key in self.plots:
                plot = self.plots[current_plot_key]
                proxy = pg.SignalProxy(plot.scene().sigMouseMoved, rateLimit=60, slot=mouseMoved)
                plot._crosshair_proxy = proxy

    def toggle_side_panel(self):
        """Toggle the visibility of the side panel"""
        if self.sidePanel.isVisible():
            self.sidePanel.hide()
        else:
            self.sidePanel.show()

    def update_side_panel_for_current_tab(self):
        """Update side panel content based on current tab"""
        self.clear_side_panel_content()

        times, channels = self.get_current_data()
        if not times:
            return

        current_tab = self.plotTabWidget.currentIndex()

        if current_tab == 0:  # All tab
            self.show_summary_in_side_panel()
        else:  # Individual device tab
            device = self.devices[current_tab - 1]
            self.show_device_analysis_in_side_panel(device)

    def show_summary_in_side_panel(self):
        """Show summary analysis in side panel with script information"""
        analysis_data = self.get_full_device_analysis()
        if not analysis_data or "Summary" not in analysis_data:
            return

        script_data = []
        script_data.append(["=== SCRIPT INFORMATION ===", ""])

        if self.live_mode:
            script_data.append(["Mode", "Live Data Stream"])
            script_data.append(["Connected", "Yes" if self.teensy.connected else "No"])
            script_data.append(["Streaming", "Yes" if self.teensy.streaming else "No"])
        else:
            script_data.append(["Script Used", "Yes" if self.script_info.get('using_script', 0) else "No"])
            if self.script_info.get('using_script', 0):
                script_data.append(["Script Name", self.script_info.get('script_name', 'Unknown')])
                script_data.append(["Start Time (T_START)", f"{self.script_info.get('t_start', 0)} seconds"])
                script_data.append(["End Time (T_END)", f"{self.script_info.get('t_end', 0)} seconds"])
                script_data.append(["Auto Recording", "Yes" if self.script_info.get('auto_record', False) else "No"])
            else:
                script_data.append(["Recording Type", "Manual Recording"])
            script_data.append(["Recording Start", self.script_info.get('timestamp', 'Unknown')])
            if self.script_info.get('duration_sec', 0) > 0:
                duration = self.script_info.get('duration_sec', 0)
                script_data.append(["Recording Duration", f"{duration} seconds ({duration / 60:.1f} minutes)"])

        script_data.append(["", ""])

        summary_data = script_data.copy()

        for category, category_data in analysis_data["Summary"].items():
            summary_data.append([f"=== {category} ===", ""])
            if isinstance(category_data, dict):
                for key, value in category_data.items():
                    summary_data.append([f"  {key}", str(value)])
            else:
                summary_data.append([category, str(category_data)])
            summary_data.append(["", ""])

        headers = ["Parameter", "Value"]
        self.add_table_to_side_panel(summary_data, headers)

    def show_device_analysis_in_side_panel(self, device):
        """Show individual device analysis in side panel"""
        analysis_data = self.get_full_device_analysis()
        if not analysis_data or device not in analysis_data:
            return

        device_key = device
        device_data = analysis_data[device_key]

        device_table_data = []
        for key, value in device_data.items():
            if key != "Device":
                device_table_data.append([key, str(value)])

        headers = ["Parameter", "Value"]
        self.add_table_to_side_panel(device_table_data, headers)

    def add_table_to_side_panel(self, data, headers):
        """Add a table to the side panel"""
        table = QtWidgets.QTableWidget(len(data), len(headers))
        table.setHorizontalHeaderLabels(headers)
        table.setEditTriggers(QtWidgets.QAbstractItemView.EditTrigger.NoEditTriggers)
        table.setAlternatingRowColors(True)
        table.setSelectionBehavior(QtWidgets.QAbstractItemView.SelectionBehavior.SelectRows)

        for i, row in enumerate(data):
            for j, cell in enumerate(row):
                item = QtWidgets.QTableWidgetItem(str(cell))
                if str(cell).startswith("===") and str(cell).endswith("==="):
                    font = item.font()
                    font.setBold(True)
                    item.setFont(font)
                table.setItem(i, j, item)

        table.resizeColumnsToContents()
        self.sidePanelLayout.addWidget(table)

    def clear_side_panel_content(self):
        """Clear all content from the side panel (except the title)"""
        while self.sidePanelLayout.count() > 1:
            child = self.sidePanelLayout.takeAt(1)
            if child.widget():
                child.widget().deleteLater()

    def get_full_device_analysis(self):
        """Generate a comprehensive summary of the data analysis"""
        times, channels = self.get_current_data()

        if not times or not self.devices:
            return {}

        data = {}
        time_duration_seconds = times[-1] - times[0] if len(times) > 1 else 0
        times_array = np.array(times)

        for device in self.devices:
            device_key = device
            volt_key = f"{device_key}_volt"
            curr_key = f"{device_key}_curr"
            pow_key = f"{device_key}_pow"
            stat_key = f"{device_key}_stat"

            if volt_key not in channels or curr_key not in channels:
                continue

            voltages = np.array(channels[volt_key])
            currents = np.array(channels[curr_key])

            if len(voltages) != len(currents) or len(voltages) != len(times):
                continue

            power_watts = voltages * currents

            time_hours = times_array / 3600.0
            amp_hours = np.trapz(currents, time_hours) if len(currents) > 1 else 0.0

            watt_hours = np.trapz(power_watts, time_hours) if len(power_watts) > 1 else 0.0

            data[device_key] = {
                "Device": device,
                "Total Time (s)": round(time_duration_seconds, 2),
                "Max Voltage (V)": round(np.max(voltages), 3),
                "Min Voltage (V)": round(np.min(voltages), 3),
                "Average Voltage (V)": round(np.mean(voltages), 3),
                "Max Current (A)": round(np.max(currents), 3),
                "Min Current (A)": round(np.min(currents), 3),
                "Average Current (A)": round(np.mean(currents), 3),
                "Max Power (W)": round(np.max(power_watts), 3),
                "Min Power (W)": round(np.min(power_watts), 3),
                "Average Power (W)": round(np.mean(power_watts), 3),
                "Calculated Amp Hours (Ah)": round(amp_hours, 4),
                "Energy Consumed (Wh)": round(watt_hours, 3),
                "Total Data Points": len(times),
                "Avg Polling Rate (Hz)": round(len(times) / time_duration_seconds,
                                               2) if time_duration_seconds > 0 else 0,
                "Data Quality": "Good" if len(voltages) == len(times) else "Issues Detected"
            }

        if data:
            device_keys = [key for key in data.keys() if key != "Summary"]

            if device_keys:
                all_max_voltages = [data[dev]["Max Voltage (V)"] for dev in device_keys]
                all_avg_voltages = [data[dev]["Average Voltage (V)"] for dev in device_keys]
                all_max_currents = [data[dev]["Max Current (A)"] for dev in device_keys]
                all_avg_currents = [data[dev]["Average Current (A)"] for dev in device_keys]
                all_max_powers = [data[dev]["Max Power (W)"] for dev in device_keys]
                all_avg_powers = [data[dev]["Average Power (W)"] for dev in device_keys]
                all_amp_hours = [data[dev]["Calculated Amp Hours (Ah)"] for dev in device_keys]
                all_energy = [data[dev]["Energy Consumed (Wh)"] for dev in device_keys]

                max_current_device = max(device_keys, key=lambda d: data[d]["Max Current (A)"])
                max_power_device = max(device_keys, key=lambda d: data[d]["Max Power (W)"])
                max_energy_device = max(device_keys, key=lambda d: data[d]["Energy Consumed (Wh)"])

                max_total_current = 0.0
                max_total_power = 0.0
                for dev in device_keys:
                    curr_key = f"{dev}_curr"
                    pow_key = f"{dev}_pow"
                    if curr_key in channels:
                        max_total_current = max(max_total_current, np.max(channels[curr_key]))
                    if pow_key in channels:
                        max_total_power = max(max_total_power, np.max(channels[pow_key]))

                data["Summary"] = {
                    "Analysis Info": {
                        "Total Devices Analyzed": len(device_keys),
                        "Total Channels": len(channels) + 1,
                        "Total Data Points": len(times) * len(device_keys),
                        "Corrupted Points": len(self.corrupted_indices) if hasattr(self, 'corrupted_indices') else 0,
                        "Analysis Duration (s)": round(time_duration_seconds, 2),
                        "Analysis Duration (min)": round(time_duration_seconds / 60.0, 2),
                        "Average Polling Rate (Hz)": round(len(times) / time_duration_seconds,
                                                           2) if time_duration_seconds > 0 else 0
                    },
                    "System Voltage": {
                        "Maximum (V)": round(np.max(all_max_voltages), 3),
                        "Average Maximum (V)": round(np.mean(all_max_voltages), 3),
                        "Overall Average (V)": round(np.mean(all_avg_voltages), 3)
                    },
                    "System Current": {
                        "Maximum (A)": round(np.max(all_max_currents), 3),
                        "Maximum Total Current (A)": round(max_total_current, 3),
                        "Average Maximum (A)": round(np.mean(all_max_currents), 3),
                        "Total Average (A)": round(np.sum(all_avg_currents), 3),
                        "Device with Max Current": data[max_current_device]["Device"]
                    },
                    "System Power": {
                        "Maximum (W)": round(np.max(all_max_powers), 3),
                        "Maximum Total Power (W)": round(max_total_power, 3),
                        "Average Maximum (W)": round(np.mean(all_max_powers), 3),
                        "Total Average (W)": round(np.sum(all_avg_powers), 3),
                        "Device with Max Power": data[max_power_device]["Device"]
                    },
                    "Energy Analysis": {
                        "Total Amp Hours (Ah)": round(np.sum(all_amp_hours), 4),
                        "Total Energy (Wh)": round(np.sum(all_energy), 3),
                        "Total Energy (kWh)": round(np.sum(all_energy) / 1000.0, 6),
                        "Device with Most Energy": data[max_energy_device]["Device"]
                    }
                }

        return data

    def add_to_recent_files(self, file_path):
        """Add a file to the recent files list"""
        recent_files = self.settings.value("recent_files", [])
        if not isinstance(recent_files, list):
            recent_files = []
        if file_path in recent_files:
            recent_files.remove(file_path)
        recent_files.insert(0, file_path)
        recent_files = recent_files[:self.max_recent_files]
        self.settings.setValue("recent_files", recent_files)
        self.update_recent_files_menu()

    def update_recent_files_menu(self):
        """Update the recent files menu"""
        self.recent_menu.clear()
        recent_files = self.settings.value("recent_files", [])
        if not isinstance(recent_files, list):
            recent_files = []
        existing_files = [f for f in recent_files if os.path.exists(f)]
        if len(existing_files) != len(recent_files):
            self.settings.setValue("recent_files", existing_files)
            recent_files = existing_files
        if recent_files:
            for i, file_path in enumerate(recent_files):
                action = QtGui.QAction(f"&{i + 1} {os.path.basename(file_path)}", self)
                action.setStatusTip(file_path)
                action.triggered.connect(lambda checked, path=file_path: self.load_file(path))
                self.recent_menu.addAction(action)
            self.recent_menu.addSeparator()
            clear_action = QtGui.QAction("&Clear Recent Files", self)
            clear_action.triggered.connect(self.clear_recent_files)
            self.recent_menu.addAction(clear_action)
        else:
            no_files_action = QtGui.QAction("No recent files", self)
            no_files_action.setEnabled(False)
            self.recent_menu.addAction(no_files_action)

    def clear_recent_files(self):
        """Clear the recent files list"""
        self.settings.setValue("recent_files", [])
        self.update_recent_files_menu()

    def show_save_dialog(self):
        """Show save format selection dialog"""
        times, channels = self.get_current_data()
        if not times:
            QtWidgets.QMessageBox.warning(self, "No Data",
                                          "No data loaded. Please open a file first or start live streaming.")
            return
        dialog = QtWidgets.QDialog(self)
        dialog.setWindowTitle("Save Analysis")
        dialog.setModal(True)
        layout = QtWidgets.QVBoxLayout(dialog)
        layout.addWidget(QtWidgets.QLabel("Choose export format:"))
        text_btn = QtWidgets.QPushButton("Text (.txt)")
        csv_btn = QtWidgets.QPushButton("CSV (.csv)")
        excel_btn = QtWidgets.QPushButton("Excel (.xlsx)")
        cancel_btn = QtWidgets.QPushButton("Cancel")
        text_btn.clicked.connect(lambda: self.export_from_dialog(dialog, 'text'))
        csv_btn.clicked.connect(lambda: self.export_from_dialog(dialog, 'csv'))
        excel_btn.clicked.connect(lambda: self.export_from_dialog(dialog, 'excel'))
        cancel_btn.clicked.connect(dialog.reject)
        if not (PANDAS_AVAILABLE and OPENPYXL_AVAILABLE):
            excel_btn.setEnabled(False)
            excel_btn.setToolTip("Requires pandas and openpyxl")
        button_layout = QtWidgets.QHBoxLayout()
        button_layout.addWidget(text_btn)
        button_layout.addWidget(csv_btn)
        button_layout.addWidget(excel_btn)
        button_layout.addWidget(cancel_btn)
        layout.addLayout(button_layout)
        dialog.exec()

    def export_from_dialog(self, dialog, format_type):
        """Export from save dialog"""
        dialog.accept()
        self.export_analysis(format_type)

    def export_analysis(self, format_type):
        """Export analysis data to various formats"""
        times, channels = self.get_current_data()
        if not times:
            QtWidgets.QMessageBox.warning(
                self, "No Data",
                "No data loaded. Please open a file first or start live streaming."
            )
            return
        analysis_data = self.get_full_device_analysis()
        if not analysis_data:
            QtWidgets.QMessageBox.warning(
                self, "No Analysis Data",
                "No analysis data available. Please ensure data is properly loaded."
            )
            return
        last_dir = self.settings.value("last_export_directory", str(Path.home()))
        if format_type == 'text':
            file_path, _ = QtWidgets.QFileDialog.getSaveFileName(
                self, "Export Analysis to Text",
                os.path.join(last_dir, "analysis.txt"),
                "Text files (*.txt);;All files (*.*)"
            )
            if file_path:
                self.export_to_text(analysis_data, file_path)
        elif format_type == 'csv':
            file_path, _ = QtWidgets.QFileDialog.getSaveFileName(
                self, "Export Analysis to CSV",
                os.path.join(last_dir, "analysis.csv"),
                "CSV files (*.csv);;All files (*.*)"
            )
            if file_path:
                self.export_to_csv(analysis_data, file_path)
        elif format_type == 'excel':
            if not (PANDAS_AVAILABLE and OPENPYXL_AVAILABLE):
                QtWidgets.QMessageBox.warning(
                    self, "Missing Dependencies",
                    "Excel export requires pandas and openpyxl.\nInstall with: pip install pandas openpyxl"
                )
                return
            file_path, _ = QtWidgets.QFileDialog.getSaveFileName(
                self, "Export Analysis to Excel",
                os.path.join(last_dir, "analysis.xlsx"),
                "Excel files (*.xlsx);;All files (*.*)"
            )
            if file_path:
                self.export_to_excel(analysis_data, file_path)
        if 'file_path' in locals() and file_path:
            self.settings.setValue("last_export_directory", os.path.dirname(file_path))

    def export_to_text(self, analysis_data, file_path):
        """Export analysis data to text file"""
        try:
            with open(file_path, 'w', encoding='utf-8') as f:
                f.write("Power Data Analysis Report\n")
                f.write("=" * 50 + "\n")
                f.write(f"Generated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
                if self.current_file_path:
                    f.write(f"Source File: {os.path.basename(self.current_file_path)}\n")
                elif self.live_mode:
                    f.write("Source: Live Data Stream\n")
                f.write("\n")
                f.write("SCRIPT INFORMATION\n")
                f.write("=" * 30 + "\n")
                if self.live_mode:
                    f.write("Mode: Live Data Stream\n")
                    f.write(f"Connected: {'Yes' if self.teensy.connected else 'No'}\n")
                    f.write(f"Streaming: {'Yes' if self.teensy.streaming else 'No'}\n")
                else:
                    f.write(f"Script Used: {'Yes' if self.script_info.get('using_script', 0) else 'No'}\n")
                    if self.script_info.get('using_script', 0):
                        f.write(f"Script Name: {self.script_info.get('script_name', 'Unknown')}\n")
                        f.write(f"Start Time (T_START): {self.script_info.get('t_start', 0)} seconds\n")
                        f.write(f"End Time (T_END): {self.script_info.get('t_end', 0)} seconds\n")
                        f.write(f"Auto Recording: {'Yes' if self.script_info.get('auto_record', False) else 'No'}\n")
                    else:
                        f.write("Recording Type: Manual Recording\n")
                    f.write(f"Recording Start: {self.script_info.get('timestamp', 'Unknown')}\n")
                    if self.script_info.get('duration_sec', 0) > 0:
                        duration = self.script_info.get('duration_sec', 0)
                        f.write(f"Recording Duration: {duration} seconds ({duration / 60:.1f} minutes)\n")
                f.write("\n")
                for device_key, data in analysis_data.items():
                    if device_key == "Summary":
                        continue
                    f.write(f"Device: {data.get('Device', device_key)}\n")
                    f.write("-" * 30 + "\n")
                    for key, value in data.items():
                        if key != "Device":
                            f.write(f"{key}: {value}\n")
                    f.write("\n")
                if "Summary" in analysis_data:
                    f.write("SUMMARY\n")
                    f.write("=" * 30 + "\n")
                    for category, category_data in analysis_data["Summary"].items():
                        f.write(f"\n{category}:\n")
                        f.write("-" * len(category) + "\n")
                        if isinstance(category_data, dict):
                            for key, value in category_data.items():
                                f.write(f"  {key}: {value}\n")
                        else:
                            f.write(f"  {category_data}\n")
            QtWidgets.QMessageBox.information(
                self, "Export Successful",
                f"Analysis exported to:\n{file_path}"
            )
        except Exception as e:
            QtWidgets.QMessageBox.critical(
                self, "Export Error",
                f"Failed to export to text file:\n{str(e)}"
            )

    def export_to_csv(self, analysis_data, file_path):
        """Export analysis data to CSV file"""
        try:
            import csv
            with open(file_path, 'w', newline='', encoding='utf-8') as f:
                writer = csv.writer(f)
                writer.writerow(["Category", "Parameter", "Value"])
                if self.live_mode:
                    writer.writerow(["SCRIPT", "Mode", "Live Data Stream"])
                    writer.writerow(["SCRIPT", "Connected", "Yes" if self.teensy.connected else "No"])
                    writer.writerow(["SCRIPT", "Streaming", "Yes" if self.teensy.streaming else "No"])
                else:
                    writer.writerow(
                        ["SCRIPT", "Script Used", "Yes" if self.script_info.get('using_script', 0) else "No"])
                    if self.script_info.get('using_script', 0):
                        writer.writerow(["SCRIPT", "Script Name", self.script_info.get('script_name', 'Unknown')])
                        writer.writerow(
                            ["SCRIPT", "Start Time (T_START)", f"{self.script_info.get('t_start', 0)} seconds"])
                        writer.writerow(["SCRIPT", "End Time (T_END)", f"{self.script_info.get('t_end', 0)} seconds"])
                        writer.writerow(
                            ["SCRIPT", "Auto Recording", "Yes" if self.script_info.get('auto_record', False) else "No"])
                    else:
                        writer.writerow(["SCRIPT", "Recording Type", "Manual Recording"])
                    writer.writerow(["SCRIPT", "Recording Start", self.script_info.get('timestamp', 'Unknown')])
                    if self.script_info.get('duration_sec', 0) > 0:
                        duration = self.script_info.get('duration_sec', 0)
                        writer.writerow(
                            ["SCRIPT", "Recording Duration", f"{duration} seconds ({duration / 60:.1f} minutes)"])
                for device_key, data in analysis_data.items():
                    if device_key == "Summary":
                        continue
                    device_name = data.get('Device', device_key)
                    for key, value in data.items():
                        if key != "Device":
                            writer.writerow([device_name, key, value])
                if "Summary" in analysis_data:
                    for category, category_data in analysis_data["Summary"].items():
                        if isinstance(category_data, dict):
                            for key, value in category_data.items():
                                writer.writerow(["SUMMARY", f"{category} - {key}", value])
                        else:
                            writer.writerow(["SUMMARY", category, category_data])
            QtWidgets.QMessageBox.information(
                self, "Export Successful",
                f"Analysis exported to:\n{file_path}"
            )
        except Exception as e:
            QtWidgets.QMessageBox.critical(
                self, "Export Error",
                f"Failed to export to CSV file:\n{str(e)}"
            )

    def export_to_excel(self, analysis_data, file_path):
        """Export analysis data to Excel file"""
        try:
            with pd.ExcelWriter(file_path, engine='openpyxl') as writer:
                script_data = []
                if self.live_mode:
                    script_data.append({"Parameter": "Mode", "Value": "Live Data Stream"})
                    script_data.append(
                        {"Parameter": "Connected", "Value": "Yes" if self.teensy.connected else "No"})
                    script_data.append(
                        {"Parameter": "Streaming", "Value": "Yes" if self.teensy.streaming else "No"})
                else:
                    script_data.append(
                        {"Parameter": "Script Used",
                         "Value": "Yes" if self.script_info.get('using_script', 0) else "No"})
                    if self.script_info.get('using_script', 0):
                        script_data.append(
                            {"Parameter": "Script Name", "Value": self.script_info.get('script_name', 'Unknown')})
                        script_data.append(
                            {"Parameter": "Start Time (T_START)",
                             "Value": f"{self.script_info.get('t_start', 0)} seconds"})
                        script_data.append(
                            {"Parameter": "End Time (T_END)", "Value": f"{self.script_info.get('t_end', 0)} seconds"})
                        script_data.append({"Parameter": "Auto Recording",
                                            "Value": "Yes" if self.script_info.get('auto_record', False) else "No"})
                    else:
                        script_data.append({"Parameter": "Recording Type", "Value": "Manual Recording"})
                    script_data.append(
                        {"Parameter": "Recording Start", "Value": self.script_info.get('timestamp', 'Unknown')})
                    if self.script_info.get('duration_sec', 0) > 0:
                        duration = self.script_info.get('duration_sec', 0)
                        script_data.append({"Parameter": "Recording Duration",
                                            "Value": f"{duration} seconds ({duration / 60:.1f} minutes)"})
                df_script = pd.DataFrame(script_data)
                df_script.to_excel(writer, sheet_name="Script Info", index=False)
                for device_key, data in analysis_data.items():
                    if device_key == "Summary":
                        continue
                    df_data = []
                    device_name = data.get('Device', device_key)
                    for key, value in data.items():
                        if key != "Device":
                            df_data.append({"Parameter": key, "Value": value})
                    df = pd.DataFrame(df_data)
                    sheet_name = device_name[:31]
                    df.to_excel(writer, sheet_name=sheet_name, index=False)
                if "Summary" in analysis_data:
                    summary_data = []
                    for category, category_data in analysis_data["Summary"].items():
                        if isinstance(category_data, dict):
                            for key, value in category_data.items():
                                summary_data.append({
                                    "Category": category,
                                    "Parameter": key,
                                    "Value": value
                                })
                        else:
                            summary_data.append({
                                "Category": "General",
                                "Parameter": category,
                                "Value": category_data
                            })
                    df_summary = pd.DataFrame(summary_data)
                    df_summary.to_excel(writer, sheet_name="Summary", index=False)
            QtWidgets.QMessageBox.information(
                self, "Export Successful",
                f"Analysis exported to:\n{file_path}"
            )
        except Exception as e:
            QtWidgets.QMessageBox.critical(
                self, "Export Error",
                f"Failed to export to Excel file:\n{str(e)}"
            )

    def load_settings(self):
        """Load application settings"""
        self.resize(self.settings.value("window_size", QtCore.QSize(1400, 700)))
        self.move(self.settings.value("window_position", QtCore.QPoint(100, 100)))
        splitter_sizes = self.settings.value("splitter_sizes", [200, 600, 300])
        if isinstance(splitter_sizes, list) and len(splitter_sizes) == 3:
            self.mainSplitter.setSizes([int(x) for x in splitter_sizes])
        self.ip_edit.setText(self.settings.value("tcp_ip", "192.168.1.100"))
        self.tcp_port_edit.setText(self.settings.value("tcp_port", "8080"))
        self.udp_port_edit.setText(self.settings.value("udp_port", "8081"))
        self.baud_combo.setCurrentText(self.settings.value("serial_baud_rate", "2000000"))

        if hasattr(self, 'crosshair_cb'):
            self.crosshair_cb.setChecked(self.settings.value("enable_crosshair", True, bool))

    def save_settings(self):
        """Save application settings"""
        self.settings.setValue("window_size", self.size())
        self.settings.setValue("window_position", self.pos())
        self.settings.setValue("splitter_sizes", self.mainSplitter.sizes())
        self.settings.setValue("tcp_ip", self.ip_edit.text())
        self.settings.setValue("tcp_port", self.tcp_port_edit.text())
        self.settings.setValue("udp_port", self.udp_port_edit.text())
        self.settings.setValue("serial_baud_rate", self.baud_combo.currentText())

        if hasattr(self, 'crosshair_cb'):
            self.settings.setValue("enable_crosshair", self.crosshair_cb.isChecked())

    def closeEvent(self, event):
        """Handle application close event"""
        if self.teensy.connected:
            self.disconnect_from_teensy()
        if self.debug_console:
            self.debug_console.close()
        self.save_settings()
        event.accept()


def main():
    """Main application entry point"""
    app = QtWidgets.QApplication(sys.argv)
    app.setApplicationName("Teensy 4.1 Power Controller")
    app.setApplicationVersion("2.1")
    app.setOrganizationName("TeensyPowerController")

    icon_path = "resources/icons/app_icon.ico"
    if os.path.exists(icon_path):
        app.setWindowIcon(QtGui.QIcon(icon_path))

    window = PowerControllerGUI()
    window.show()

    if len(sys.argv) > 1:
        default_file = sys.argv[1]
        if os.path.exists(default_file):
            window.load_file(default_file)

    sys.exit(app.exec())


if __name__ == "__main__":
    main()
