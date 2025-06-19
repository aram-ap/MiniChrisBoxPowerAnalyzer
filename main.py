import json
import sys
import os
from pathlib import Path
from datetime import datetime
import pyqtgraph as pg
from pyqtgraph.Qt import QtWidgets, QtCore, QtGui
import numpy as np

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

# Predefined device list
DEVICES = ["GSE-1", "GSE-2", "TE-1", "TE-2", "TE-3", "TE-R"]
DATA_TYPES = ["volt", "curr", "pow", "stat"]


class PowerDataAnalyzer(QtWidgets.QMainWindow):
    """Main application class for Power Data Multi-Panel Viewer"""

    def __init__(self):
        super().__init__()
        self.current_file_path = None
        self.data_json = None
        self.data_points = []
        self.times = []
        self.channels = {}
        self.all_fields = []
        self.devices = DEVICES.copy()
        self.types = DATA_TYPES.copy()
        self.plots = {}
        self.curves = {}
        self.field_checkboxes = {}
        self.current_tab_index = 0

        # Settings
        self.settings = QtCore.QSettings("PowerDataAnalyzer", "PowerDataAnalyzer")
        self.max_recent_files = 10

        self.init_ui()
        self.create_menus()
        self.load_settings()

    def init_ui(self):
        """Initialize the user interface"""
        self.setWindowTitle("Power Data Multi-Panel Viewer")
        self.setWindowIcon(QtGui.QIcon("resources/icons/app_icon.ico") if os.path.exists(
            "resources/icons/app_icon.ico") else self.style().standardIcon(
            QtWidgets.QStyle.StandardPixmap.SP_ComputerIcon))

        # Central widget
        centralWidget = QtWidgets.QWidget()
        self.setCentralWidget(centralWidget)

        # Main layout using QSplitter for better panel management
        mainLayout = QtWidgets.QHBoxLayout(centralWidget)
        self.mainSplitter = QtWidgets.QSplitter(QtCore.Qt.Orientation.Horizontal)
        mainLayout.addWidget(self.mainSplitter)

        # Left: Field selection panel
        self.create_field_panel()

        # Middle: Tabbed plot area
        self.create_plot_tabs()

        # Right: Toggleable side panel
        self.create_side_panel()

        # Add panels to splitter
        self.mainSplitter.addWidget(self.fieldPanel)
        self.mainSplitter.addWidget(self.plotTabWidget)
        self.mainSplitter.addWidget(self.sidePanel)

        # Set initial splitter sizes
        self.mainSplitter.setSizes([200, 600, 300])

        # Status bar
        self.statusBar().showMessage("Ready - Open a file to begin analysis")

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

    def on_tab_changed(self, index):
        """Handle tab change"""
        self.current_tab_index = index
        self.update_plots()
        self.update_side_panel_for_current_tab()

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

        # View Menu
        view_menu = menubar.addMenu('&View')

        # Toggle Side Panel
        toggle_side_action = QtGui.QAction('Toggle &Side Panel', self)
        toggle_side_action.setShortcut('X')
        toggle_side_action.setStatusTip('Toggle side panel visibility')
        toggle_side_action.triggered.connect(self.toggle_side_panel)
        view_menu.addAction(toggle_side_action)

        # Tab shortcuts
        for i, device in enumerate(self.devices):
            tab_action = QtGui.QAction(f'Switch to {device}', self)
            tab_action.setShortcut(f'{i + 1}')
            tab_action.triggered.connect(lambda checked, idx=i + 1: self.plotTabWidget.setCurrentIndex(idx))
            view_menu.addAction(tab_action)

        # Summary tab shortcut
        # summary_action = QtGui.QAction('Switch to Summary', self)
        # summary_action.setShortcut('Ctrl+A')
        # summary_action.triggered.connect(lambda: self.show_summary_in_side_panel())
        # view_menu.addAction(summary_action)

        # All tab shortcut
        all_tab_action = QtGui.QAction('Switch to All', self)
        all_tab_action.setShortcut('0')
        all_tab_action.setShortcuts(('0','A'))
        all_tab_action.triggered.connect(lambda: self.plotTabWidget.setCurrentIndex(0))
        view_menu.addAction(all_tab_action)

    def show_save_dialog(self):
        """Show save format selection dialog"""
        if not self.data_points:
            QtWidgets.QMessageBox.warning(self, "No Data", "No data loaded. Please open a file first.")
            return

        dialog = QtWidgets.QDialog(self)
        dialog.setWindowTitle("Save Analysis")
        dialog.setModal(True)

        layout = QtWidgets.QVBoxLayout(dialog)
        layout.addWidget(QtWidgets.QLabel("Choose export format:"))

        # Format buttons
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

    def create_field_panel(self):
        """Create the field selection panel"""
        self.fieldPanel = QtWidgets.QWidget()
        self.fieldLayout = QtWidgets.QVBoxLayout(self.fieldPanel)
        self.fieldLayout.setContentsMargins(5, 5, 5, 5)
        self.fieldLayout.setSpacing(5)

        # File info section
        self.file_info_label = QtWidgets.QLabel("<b>No file loaded</b>")
        self.file_info_label.setWordWrap(True)
        self.fieldLayout.addWidget(self.file_info_label)

        # Field selection section
        self.field_selection_label = QtWidgets.QLabel("<b>Select data types to plot:</b>")
        self.fieldLayout.addWidget(self.field_selection_label)

        # Field checkboxes
        self.create_field_checkboxes()

        # Toggle side panel button
        self.create_toggle_button()

        self.fieldLayout.addStretch(1)

    def create_field_checkboxes(self):
        """Create checkboxes for data type selection"""
        self.field_checkboxes = {}

        for typ in self.types:
            cb = QtWidgets.QCheckBox(self.format_type_name(typ))
            cb.setChecked(True)  # All types checked by default
            self.field_checkboxes[typ] = cb
            cb.stateChanged.connect(self.update_plots)
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
        device_formatted = device.replace("-", "-")  # Keep device format

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

        # Try to load custom icon, fallback to standard icon
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

    def create_side_panel(self):
        """Create the side panel"""
        self.sidePanel = QtWidgets.QWidget()
        self.sidePanelLayout = QtWidgets.QVBoxLayout(self.sidePanel)
        self.sidePanelLayout.setContentsMargins(5, 5, 5, 5)
        self.sidePanelLayout.setSpacing(5)

        # Title
        title_label = QtWidgets.QLabel("<b>Analysis Panel</b>")
        self.sidePanelLayout.addWidget(title_label)

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

            # Validate file exists and is readable
            if not os.path.exists(file_path):
                raise FileNotFoundError(f"File not found: {file_path}")

            if not os.access(file_path, os.R_OK):
                raise PermissionError(f"Cannot read file: {file_path}")

            # Load JSON data
            with open(file_path, "r", encoding='utf-8') as f:
                self.data_json = json.load(f)

            # Validate JSON structure
            if not self.validate_json_structure(self.data_json):
                raise ValueError("Invalid JSON structure - missing required fields or devices")

            # Process the data
            self.process_data()

            # Update UI
            self.current_file_path = file_path
            self.update_file_info()
            self.update_plots()

            # Update settings
            self.settings.setValue("last_directory", os.path.dirname(file_path))
            self.add_to_recent_files(file_path)

            # Update side panel
            self.update_side_panel_for_current_tab()

            self.statusBar().showMessage(f"Loaded {os.path.basename(file_path)} successfully")

        except Exception as e:
            QtWidgets.QMessageBox.critical(
                self,
                "Error Loading File",
                f"Failed to load file: {file_path}\n\nError: {str(e)}"
            )
            self.statusBar().showMessage("Failed to load file")

    def validate_json_structure(self, data):
        """Validate the JSON data structure"""
        try:
            # Check if data has required structure
            if not isinstance(data, dict):
                return False

            if "data" not in data:
                return False

            data_points = data["data"]
            if not isinstance(data_points, list) or len(data_points) == 0:
                return False

            # Check first data point structure
            sample = data_points[0]
            if not isinstance(sample, dict):
                return False

            if "time" not in sample:
                return False

            # Check for required device fields
            expected_fields = []
            for device in self.devices:
                device_key = device
                for data_type in self.types:
                    expected_fields.append(f"{device_key}_{data_type}")

            # Check if at least some expected fields exist
            found_fields = [field for field in expected_fields if field in sample]
            if len(found_fields) == 0:
                return False

            return True

        except Exception:
            return False

    def process_data(self):
        """Process the loaded JSON data"""
        self.data_points = self.data_json["data"]

        # Auto-detect all keys except "time"
        sample = self.data_points[0]
        self.all_fields = [k for k in sample.keys() if k != "time"]

        # Prepare data arrays and convert current from mA to A
        self.times = [dp["time"] / 1000.0 for dp in self.data_points]  # Convert to seconds
        self.channels = {}

        for k in self.all_fields:
            data_array = [dp[k] for dp in self.data_points]
            # Convert current data from mA to A
            if k.endswith('_curr'):
                data_array = [val / 1000.0 for val in data_array]
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
<b>Data Points:</b> {len(self.data_points):,}<br>
<b>Duration:</b> {self.times[-1] - self.times[0]:.1f}s"""

            self.file_info_label.setText(info_text)
        else:
            self.file_info_label.setText("<b>No file loaded</b>")

    def get_selected_types(self):
        """Get currently selected data types"""
        return [typ for typ, cb in self.field_checkboxes.items() if cb.isChecked()]

    def update_plots(self):
        """Update all plot displays"""
        if not self.data_points:
            return

        selected_types = self.get_selected_types()
        if not selected_types:
            return

        current_tab = self.plotTabWidget.currentIndex()

        if current_tab == 0:  # All tab
            self.update_all_plots(selected_types)
        else:  # Individual device tab
            device = self.devices[current_tab - 1]
            self.update_device_plots(device, selected_types)

    def update_all_plots(self, selected_types):
        """Update the 'All' tab plots with combined device data"""
        self.all_plot_widget.clear()
        self.plots.clear()
        self.curves.clear()

        color_pools = {
            'volt': [(31, 119, 180), (255, 127, 14), (44, 160, 44), (214, 39, 40), (148, 103, 189), (140, 86, 75)],
            'curr': [(255, 127, 14), (44, 160, 44), (214, 39, 40), (148, 103, 189), (140, 86, 75), (31, 119, 180)],
            'pow': [(44, 160, 44), (214, 39, 40), (148, 103, 189), (140, 86, 75), (31, 119, 180), (255, 127, 14)],
            'stat': [(214, 39, 40), (148, 103, 189), (140, 86, 75), (31, 119, 180), (255, 127, 14), (44, 160, 44)]
        }

        for i, data_type in enumerate(selected_types):
            p = self.all_plot_widget.addPlot(row=i, col=0)
            p.showGrid(y=True, x=True, alpha=0.3)
            p.setLabel('left', self.format_type_name(data_type))
            if i == len(selected_types) - 1:
                p.setLabel('bottom', 'Time (s)')

            # Plot each device's data for this type
            colors = color_pools.get(data_type, [(31, 119, 180)] * 6)
            for j, device in enumerate(self.devices):
                device_key = device
                field_key = f"{device_key}_{data_type}"

                if field_key in self.channels:
                    color = colors[j % len(colors)]
                    curve = p.plot(
                        self.times,
                        self.channels[field_key],
                        pen=pg.mkPen(color=color, width=2),
                        name=device
                    )
                    self.curves[f"{device}_{data_type}"] = curve

            # Link x-axes
            if i > 0:
                first_plot_key = f"all_{selected_types[0]}"
                if first_plot_key in self.plots:
                    p.setXLink(self.plots[first_plot_key])

            self.plots[f"all_{data_type}"] = p

        # Add crosshair to first plot
        if selected_types:
            self.add_crosshair_to_all_plot(selected_types, color_pools)

    def update_device_plots(self, device, selected_types):
        """Update individual device tab plots"""
        plot_widget = self.device_plot_widgets[device]
        plot_widget.clear()
        self.plots.clear()
        self.curves.clear()

        device_key = device
        color_pool = [(31, 119, 180), (255, 127, 14), (44, 160, 44), (214, 39, 40)]

        for i, data_type in enumerate(selected_types):
            field_key = f"{device_key}_{data_type}"

            if field_key in self.channels:
                p = plot_widget.addPlot(row=i, col=0)
                p.showGrid(y=True, x=True, alpha=0.3)
                color = color_pool[i % len(color_pool)]

                curve = p.plot(
                    self.times,
                    self.channels[field_key],
                    pen=pg.mkPen(color=color, width=2),
                    name=field_key
                )

                p.setLabel('left', self.format_axis_label(device, data_type))
                if i == len(selected_types) - 1:
                    p.setLabel('bottom', 'Time (s)')

                # Link x-axes
                if i > 0:
                    first_plot_key = f"{device}_{selected_types[0]}"
                    if first_plot_key in self.plots:
                        p.setXLink(self.plots[first_plot_key])

                self.plots[f"{device}_{data_type}"] = p
                self.curves[field_key] = curve

        # Add crosshair to first plot
        if selected_types:
            self.add_crosshair_to_device_plot(device, selected_types, color_pool)

    def add_crosshair_to_all_plot(self, selected_types, color_pools):
        """Add crosshair and floating label to the All tab first plot"""
        first_type = selected_types[0]
        p0 = self.plots[f"all_{first_type}"]

        vLine = pg.InfiniteLine(angle=90, movable=False, pen=pg.mkPen('k', width=1, style=dash_style))
        p0.addItem(vLine, ignoreBounds=True)
        label = pg.TextItem("", anchor=(0, 1), border='w', fill=(0, 0, 0, 150))
        p0.addItem(label)

        times_np = np.array(self.times)

        def mouseMoved(evt):
            pos = evt[0]
            if p0.sceneBoundingRect().contains(pos):
                mousePoint = p0.vb.mapSceneToView(pos)
                x = mousePoint.x()
                vLine.setPos(x)
                idx = np.searchsorted(times_np, x)
                if idx >= len(times_np):
                    idx = len(times_np) - 1
                if idx < 0:
                    idx = 0

                time_val_sec = times_np[idx]
                text = f"<span style='font-size: 12pt'>Time: {time_val_sec:.3f} s</span><br>"

                for data_type in selected_types:
                    text += f"<br><b>{self.format_type_name(data_type)}:</b><br>"
                    colors = color_pools.get(data_type, [(31, 119, 180)] * 6)

                    for j, device in enumerate(self.devices):
                        device_key = device
                        field_key = f"{device_key}_{data_type}"

                        if field_key in self.channels and idx < len(self.channels[field_key]):
                            yval = self.channels[field_key][idx]
                            color = colors[j % len(colors)]
                            color_hex = '#%02x%02x%02x' % color
                            text += f"<span style='color: {color_hex}'>{device}: {yval:.3f}</span><br>"

                label.setHtml(text)
                label.setPos(x + (times_np[-1] - times_np[0]) * 0.02, mousePoint.y())
            else:
                label.setHtml("")

        proxy = pg.SignalProxy(p0.scene().sigMouseMoved, rateLimit=60, slot=mouseMoved)

    def add_crosshair_to_device_plot(self, device, selected_types, color_pool):
        """Add crosshair and floating label to device plot"""
        first_type = selected_types[0]
        p0 = self.plots[f"{device}_{first_type}"]

        vLine = pg.InfiniteLine(angle=90, movable=False, pen=pg.mkPen('k', width=1, style=dash_style))
        p0.addItem(vLine, ignoreBounds=True)
        label = pg.TextItem("", anchor=(0, 1), border='w', fill=(0, 0, 0, 150))
        p0.addItem(label)

        times_np = np.array(self.times)
        device_key = device

        def mouseMoved(evt):
            pos = evt[0]
            if p0.sceneBoundingRect().contains(pos):
                mousePoint = p0.vb.mapSceneToView(pos)
                x = mousePoint.x()
                vLine.setPos(x)
                idx = np.searchsorted(times_np, x)
                if idx >= len(times_np):
                    idx = len(times_np) - 1
                if idx < 0:
                    idx = 0

                time_val_sec = times_np[idx]
                text = f"<span style='font-size: 12pt'>Time: {time_val_sec:.3f} s</span><br>"

                for i, data_type in enumerate(selected_types):
                    field_key = f"{device_key}_{data_type}"
                    if field_key in self.channels and idx < len(self.channels[field_key]):
                        yval = self.channels[field_key][idx]
                        color = color_pool[i % len(color_pool)]
                        color_hex = '#%02x%02x%02x' % color
                        text += f"<span style='color: {color_hex}'>{self.format_axis_label(device, data_type)}: {yval:.3f}</span><br>"

                label.setHtml(text)
                label.setPos(x + (times_np[-1] - times_np[0]) * 0.02, mousePoint.y())
            else:
                label.setHtml("")

        proxy = pg.SignalProxy(p0.scene().sigMouseMoved, rateLimit=60, slot=mouseMoved)

    def toggle_side_panel(self):
        """Toggle the visibility of the side panel"""
        if self.sidePanel.isVisible():
            self.sidePanel.hide()
        else:
            self.sidePanel.show()

    def update_side_panel_for_current_tab(self):
        """Update side panel content based on current tab"""
        self.clear_side_panel_content()

        if not self.data_points:
            return

        current_tab = self.plotTabWidget.currentIndex()

        if current_tab == 0:  # All tab
            self.show_summary_in_side_panel()
        else:  # Individual device tab
            device = self.devices[current_tab - 1]
            self.show_device_analysis_in_side_panel(device)

    def show_summary_in_side_panel(self):
        """Show summary analysis in side panel"""
        analysis_data = self.get_full_device_analysis()
        if not analysis_data or "Summary" not in analysis_data:
            return

        # Create summary table
        summary_data = []
        for category, category_data in analysis_data["Summary"].items():
            summary_data.append([f"=== {category} ===", ""])
            if isinstance(category_data, dict):
                for key, value in category_data.items():
                    summary_data.append([f"  {key}", str(value)])
            else:
                summary_data.append([category, str(category_data)])
            summary_data.append(["", ""])  # Empty row for spacing

        headers = ["Parameter", "Value"]
        self.add_table_to_side_panel(summary_data, headers)

    def show_device_analysis_in_side_panel(self, device):
        """Show individual device analysis in side panel"""
        analysis_data = self.get_full_device_analysis()
        if not analysis_data or device not in analysis_data:
            return

        device_key = device
        device_data = analysis_data[device_key]

        # Convert device data to table format
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
                table.setItem(i, j, QtWidgets.QTableWidgetItem(str(cell)))

        table.resizeColumnsToContents()
        self.sidePanelLayout.addWidget(table)

    def clear_side_panel_content(self):
        """Clear all content from the side panel (except the title)"""
        # Remove all widgets except the first one (title label)
        while self.sidePanelLayout.count() > 1:
            child = self.sidePanelLayout.takeAt(1)
            if child.widget():
                child.widget().deleteLater()

    # Keep your existing analysis and export functions...
    def get_full_device_analysis(self):
        """Generate a comprehensive summary of the data analysis"""
        if not self.times or not self.devices:
            return {}

        data = {}
        time_duration_seconds = self.times[-1] - self.times[0]
        times_array = np.array(self.times)

        # Analyze each device individually
        for device in self.devices:
            device_key = device
            volt_key = f"{device_key}_volt"
            curr_key = f"{device_key}_curr"
            pow_key = f"{device_key}_pow"
            stat_key = f"{device_key}_stat"

            # Skip devices without voltage and current data
            if volt_key not in self.channels or curr_key not in self.channels:
                continue

            # Get data arrays for this specific device
            voltages = np.array(self.channels[volt_key])
            currents = np.array(self.channels[curr_key])  # Already converted to A

            # Validate data lengths match
            if len(voltages) != len(currents) or len(voltages) != len(self.times):
                continue

            # Calculate instantaneous power array (P = V * I)
            power_watts = voltages * currents

            # Calculate amp-hours using trapezoidal integration
            time_hours = times_array / 3600.0  # Convert seconds to hours
            amp_hours = np.trapz(currents, time_hours) if len(currents) > 1 else 0.0

            # Calculate energy (Watt-hours)
            watt_hours = np.trapz(power_watts, time_hours) if len(power_watts) > 1 else 0.0

            # Store device analysis
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
                "Total Data Points": len(self.times),
                "Avg Polling Rate (Hz)": round(len(self.times) / time_duration_seconds,
                                               2) if time_duration_seconds > 0 else 0,
                "Data Quality": "Good" if len(voltages) == len(self.times) else "Issues Detected"
            }

        # Generate summary statistics if we have device data
        if data:
            device_keys = [key for key in data.keys() if key != "Summary"]

            if device_keys:
                # Collect all device metrics for summary calculations
                all_max_voltages = [data[dev]["Max Voltage (V)"] for dev in device_keys]
                all_avg_voltages = [data[dev]["Average Voltage (V)"] for dev in device_keys]
                all_max_currents = [data[dev]["Max Current (A)"] for dev in device_keys]
                all_avg_currents = [data[dev]["Average Current (A)"] for dev in device_keys]
                all_max_powers = [data[dev]["Max Power (W)"] for dev in device_keys]
                all_avg_powers = [data[dev]["Average Power (W)"] for dev in device_keys]
                all_amp_hours = [data[dev]["Calculated Amp Hours (Ah)"] for dev in device_keys]
                all_energy = [data[dev]["Energy Consumed (Wh)"] for dev in device_keys]

                # Find devices with extreme values
                max_current_device = max(device_keys, key=lambda d: data[d]["Max Current (A)"])
                max_power_device = max(device_keys, key=lambda d: data[d]["Max Power (W)"])
                max_energy_device = max(device_keys, key=lambda d: data[d]["Energy Consumed (Wh)"])

                data["Summary"] = {
                    "Analysis Info": {
                        "Total Devices Analyzed": len(device_keys),
                        "Total Channels": len(self.all_fields) + 1,  # +1 for time
                        "Total Data Points": len(self.times) * len(device_keys),
                        "Analysis Duration (s)": round(time_duration_seconds, 2),
                        "Analysis Duration (min)": round(time_duration_seconds / 60.0, 2),
                        "Average Polling Rate (Hz)": round(len(self.times) / time_duration_seconds,
                                                           2) if time_duration_seconds > 0 else 0
                    },
                    "System Voltage": {
                        "Maximum (V)": round(np.max(all_max_voltages), 3),
                        "Average Maximum (V)": round(np.mean(all_max_voltages), 3),
                        "Overall Average (V)": round(np.mean(all_avg_voltages), 3)
                    },
                    "System Current": {
                        "Maximum (A)": round(np.max(all_max_currents), 3),
                        "Average Maximum (A)": round(np.mean(all_max_currents), 3),
                        "Total Average (A)": round(np.sum(all_avg_currents), 3),
                        "Device with Max Current": data[max_current_device]["Device"]
                    },
                    "System Power": {
                        "Maximum (W)": round(np.max(all_max_powers), 3),
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

    # Keep your existing export functions (add_to_recent_files, export_analysis, etc.)
    def add_to_recent_files(self, file_path):
        """Add a file to the recent files list"""
        recent_files = self.settings.value("recent_files", [])
        if not isinstance(recent_files, list):
            recent_files = []

        # Remove if already in list
        if file_path in recent_files:
            recent_files.remove(file_path)

        # Add to beginning
        recent_files.insert(0, file_path)

        # Limit to max recent files
        recent_files = recent_files[:self.max_recent_files]

        # Save to settings
        self.settings.setValue("recent_files", recent_files)
        self.update_recent_files_menu()

    def update_recent_files_menu(self):
        """Update the recent files menu"""
        self.recent_menu.clear()

        recent_files = self.settings.value("recent_files", [])
        if not isinstance(recent_files, list):
            recent_files = []

        # Filter out non-existent files
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

    def export_analysis(self, format_type):
        """Export analysis data to various formats"""
        if not self.data_points:
            QtWidgets.QMessageBox.warning(
                self,
                "No Data",
                "No data loaded. Please open a file first."
            )
            return

        # Get analysis data
        analysis_data = self.get_full_device_analysis()
        if not analysis_data:
            QtWidgets.QMessageBox.warning(
                self,
                "No Analysis Data",
                "No analysis data available. Please ensure data is properly loaded."
            )
            return

        # Get save location
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
                    self,
                    "Missing Dependencies",
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
                f.write("\n")

                # Device data
                for device_key, data in analysis_data.items():
                    if device_key == "Summary":
                        continue
                    f.write(f"Device: {data.get('Device', device_key)}\n")
                    f.write("-" * 30 + "\n")
                    for key, value in data.items():
                        if key != "Device":
                            f.write(f"{key}: {value}\n")
                    f.write("\n")

                # Summary data
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

                # Header
                writer.writerow(["Device", "Parameter", "Value"])

                # Device data
                for device_key, data in analysis_data.items():
                    if device_key == "Summary":
                        continue
                    device_name = data.get('Device', device_key)
                    for key, value in data.items():
                        if key != "Device":
                            writer.writerow([device_name, key, value])

                # Summary data
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
                # Device sheets
                for device_key, data in analysis_data.items():
                    if device_key == "Summary":
                        continue

                    df_data = []
                    device_name = data.get('Device', device_key)
                    for key, value in data.items():
                        if key != "Device":
                            df_data.append({"Parameter": key, "Value": value})

                    df = pd.DataFrame(df_data)
                    sheet_name = device_name[:31]  # Excel sheet name limit
                    df.to_excel(writer, sheet_name=sheet_name, index=False)

                # Summary sheet
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

        # Load splitter sizes
        splitter_sizes = self.settings.value("splitter_sizes", [200, 600, 300])
        if isinstance(splitter_sizes, list) and len(splitter_sizes) == 3:
            self.mainSplitter.setSizes([int(x) for x in splitter_sizes])

    def save_settings(self):
        """Save application settings"""
        self.settings.setValue("window_size", self.size())
        self.settings.setValue("window_position", self.pos())
        self.settings.setValue("splitter_sizes", self.mainSplitter.sizes())

    def closeEvent(self, event):
        """Handle application close event"""
        self.save_settings()
        event.accept()


def main():
    """Main application entry point"""
    app = QtWidgets.QApplication(sys.argv)
    app.setApplicationName("Power Data Analyzer")
    app.setApplicationVersion("2.0")
    app.setOrganizationName("PowerDataAnalyzer")

    # Set application icon if available
    icon_path = "resources/icons/app_icon.ico"
    if os.path.exists(icon_path):
        app.setWindowIcon(QtGui.QIcon(icon_path))

    window = PowerDataAnalyzer()
    window.show()

    # Try to load a default file if specified
    if len(sys.argv) > 1:
        default_file = sys.argv[1]
        if os.path.exists(default_file):
            window.load_file(default_file)

    sys.exit(app.exec())


if __name__ == "__main__":
    main()
