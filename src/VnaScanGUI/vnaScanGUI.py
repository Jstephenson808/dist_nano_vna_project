import customtkinter as ctk
import subprocess
import threading
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from matplotlib.figure import Figure
import numpy as np
import glob
import math
import os
import sys
from tkinter import filedialog

# Add parent directory to path for imports when running directly
if __name__ == "__main__":
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# Import the VNA scanner wrapper
from vna_scanner import VNAScanner, VNADataPoint


import colorsys

class VNAScannerGUI:
    def __init__(self):
        # Main window
        self.root = ctk.CTk()
        self.root.title("VNA Scanner - Multi-threaded Control")
        self.root.geometry("1600x1000")
        self.root.minsize(1200, 700)  # Set minimum window size
        
        # State
        self.tooltip_window = None  # For help tooltips
        
        # Scan data storage - organized by sweep and VNA
        self.scan_data = []  # List of VNADataPoint
        self.sweep_history = []  # List of sweeps, each sweep is dict: {vna_id: {freq: (s11, s21)}}
        self.current_sweep = {}  # Current sweep being collected: {vna_id: {freq: (s11, s21)}}
        self.sweep_count = 0
        self.max_sweep_history = 5  # Keep last N sweeps for opacity effect
        
        # VNA color cache (generated dynamically)
        self._vna_color_cache = {}
        
        # Y-axis scale management
        self.y_min = -40  # Default Y-axis minimum
        self.y_max = 0    # Default Y-axis maximum
        self.auto_scaled = False  # Track if auto-scale has been applied
        
        # Scanner instance
        try:
            self.scanner = VNAScanner()
            self.scanner_available = True
        except FileNotFoundError as e:
            self.scanner = None
            self.scanner_available = False
            print(f"Warning: {e}")
        
        # Plot update scheduling
        self._plot_update_pending = False
        self._update_interval = 100  # ms

        ctk.set_appearance_mode("dark")
        ctk.set_default_color_theme("blue")

        self.setup_ui_structure()

    def setup_ui_structure(self):
        """Setup the user interface"""
        # Main container with two columns
        self.root.grid_columnconfigure(0, weight=0, minsize=350)
        self.root.grid_columnconfigure(1, weight=1)
        self.root.grid_rowconfigure(0, weight=1)
        
        # Left panel - Controls
        self.left_panel = ctk.CTkFrame(self.root, width=350)
        self.left_panel.grid(row=0, column=0, padx=10, pady=10, sticky="nsew")
        self.left_panel.grid_propagate(False)
        
        # Right panel - Plots
        self.right_panel = ctk.CTkFrame(self.root)
        self.right_panel.grid(row=0, column=1, padx=(0, 10), pady=10, sticky="nsew")
        self.right_panel.grid_rowconfigure(2, weight=1, minsize=400)  # Ensure plot has minimum height

        self.create_plot_panel()
        self.create_control_panel()
    

    def create_control_panel(self):
        """Create control panel with scan parameters"""
        # Title
        title = ctk.CTkLabel(self.left_panel, text="VNA Scanner", font=("Roboto", 24, "bold"))
        title.pack(pady=(20, 10))
        
        # Frequency parameters
        freq_frame = ctk.CTkFrame(self.left_panel)
        freq_frame.pack(pady=10, padx=20, fill="x")
        
        ctk.CTkLabel(freq_frame, text="Frequency Range", font=("Roboto", 14, "bold")).pack(pady=(10, 5))
        
        ctk.CTkLabel(freq_frame, text="Start (Hz):").pack(anchor="w", padx=10)
        self.start_freq = ctk.CTkEntry(freq_frame, placeholder_text="50000000")
        self.start_freq.insert(0, "50000000")
        self.start_freq.pack(padx=10, pady=(0, 10), fill="x")
        self.start_freq.bind("<KeyRelease>", lambda e: self.update_points_display(None))
        
        ctk.CTkLabel(freq_frame, text="Stop (Hz):").pack(anchor="w", padx=10)
        self.stop_freq = ctk.CTkEntry(freq_frame, placeholder_text="900000000")
        self.stop_freq.insert(0, "900000000")
        self.stop_freq.pack(padx=10, pady=(0, 10), fill="x")
        self.stop_freq.bind("<KeyRelease>", lambda e: self.update_points_display(None))
        
        # Scan Control
        scan_frame = ctk.CTkFrame(self.left_panel)
        scan_frame.pack(pady=10, padx=20, fill="x")

        
        ctk.CTkLabel(scan_frame, text="Scan Control", font=("Roboto", 14, "bold")).pack(pady=(10, 5))
        
        # Time limit
        self.time_limit_label = ctk.CTkLabel(scan_frame, text="Scan Duration (seconds):")
        self.time_limit_label.pack(anchor="w", padx=10)
        self.time_limit = ctk.CTkEntry(scan_frame, placeholder_text="300")
        self.time_limit.insert(0, "300")
        self.time_limit.pack(padx=10, pady=(0, 10), fill="x")
        self.time_limit.bind("<KeyRelease>", lambda e: self.update_scan_button_text())

        # Points control with tooltip
        points_label_frame = ctk.CTkFrame(scan_frame, fg_color="transparent")
        points_label_frame.pack(anchor="w", padx=10, fill="x")
        
        self.points_label = ctk.CTkLabel(points_label_frame, text="Total Points:", text_color=("gray10", "gray90"))
        self.points_label.pack(side="left")
        
        self.points_help_label = ctk.CTkLabel(points_label_frame, text="?", text_color=("#4a9eff", "#1f6aa5"),
                                              font=("Roboto", 12, "bold"), cursor="hand2")
        self.points_help_label.pack(side="left", padx=(5, 0))
        self.points_help_label.bind("<Enter>", lambda e: self.show_tooltip(e, "Total measurement points.\n• Decrease: fine steps of 1 (101→100→99...)\n• Increase: multiples of 101 (101→202→303...)\n• Default 101 = fastest single sweep"))
        self.points_help_label.bind("<Leave>", lambda e: self.hide_tooltip())
        self.points_help_label.bind("<Button-1>", lambda e: self.show_tooltip(e, "Total measurement points.\n• Decrease: fine steps of 1 (101→100→99...)\n• Increase: multiples of 101 (101→202→303...)\n• Default 101 = fastest single sweep"))
        
        # Slider for points: 0-200 range maps to special scale
        # 0-100: fine control (1-101 points)
        # 101-200: coarse control (multiples of 101: 202, 303, ... up to ~10x101)
        self.points_slider = ctk.CTkSlider(scan_frame, from_=0, to=200, number_of_steps=200,
                                           command=self.update_points_display)
        self.points_slider.set(100)  # Default to 101 points (fastest)
        self.points_slider.pack(padx=10, pady=(0, 5), fill="x")
        
        # Display current points and scan parameters
        self.points_display = ctk.CTkLabel(scan_frame, text="101 points (1 scan × 101 pps) - Fastest",
                                           font=("Roboto", 10), text_color="gray60")
        self.points_display.pack(anchor="w", padx=10, pady=(0, 10))
        
        # Sweep mode checkbox
        self.sweep_mode = ctk.CTkCheckBox(scan_frame, text="Sweep mode (fixed number of sweeps)", command=self.toggle_sweep_mode)
        self.sweep_mode.pack(anchor="w", padx=10, pady=(5, 10))
        
        # Number of sweeps
        self.num_sweeps_label = ctk.CTkLabel(scan_frame, text="Number of Sweeps:", text_color="gray50")
        self.num_sweeps_label.pack(anchor="w", padx=10)
        
        self.num_sweeps = ctk.CTkEntry(scan_frame, placeholder_text="1", state="disabled")
        self.num_sweeps.insert(0, "1")
        self.num_sweeps.pack(padx=10, pady=(0, 10), fill="x")
        
        # Start/Stop buttons in scan control panel
        self.start_button = ctk.CTkButton(
            scan_frame, 
            text="Scan for 300 seconds", 
            fg_color=("#16A500", "#14A007"),
            hover_color="#043100",
            font=("Roboto", 14, "bold"),
            command=self.start_scan
        )
        self.start_button.pack(pady=10, padx=10, fill="x")
        
        self.stop_button = ctk.CTkButton(
            scan_frame,
            text="Stop Scan",
            fg_color=("#AE0006", "#B80006"),
            hover_color="#4E0003",
            state="disabled",
            font=("Roboto", 14),
            command=self.stop_scan
        )
        self.stop_button.pack(pady=(0, 10), padx=10, fill="x")

        # VNA configuration
        vna_frame = ctk.CTkFrame(self.left_panel)
        vna_frame.pack(pady=10, padx=20, fill="x")
        
        ctk.CTkLabel(vna_frame, text="VNA Configuration", font=("Roboto", 14, "bold")).pack(pady=(10, 5))
        
        ctk.CTkLabel(vna_frame, text="Number of VNAs:").pack(anchor="w", padx=10)
        self.num_vnas = ctk.CTkEntry(vna_frame, placeholder_text="2")
        self.num_vnas.insert(0, "2")
        self.num_vnas.pack(padx=10, pady=(0, 5), fill="x")
        
        detect_btn = ctk.CTkButton(vna_frame, text="Auto-detect VNAs", command=self.detect_vnas)
        detect_btn.pack(padx=10, pady=(0, 10), fill="x")
        
        ctk.CTkLabel(vna_frame, text="Serial Ports (one per line):").pack(anchor="w", padx=10)
        self.ports_text = ctk.CTkTextbox(vna_frame, height=80)
        self.ports_text.insert("1.0", "/dev/ttyACM0\n/dev/ttyACM1")
        self.ports_text.pack(padx=10, pady=(0, 10), fill="x")
        
        # Utility buttons
        clear_btn = ctk.CTkButton(self.left_panel, text="Clear Data", command=self.clear_data)
        clear_btn.pack(pady=(0, 10), padx=20, fill="x")
        
        self.touchstone_btn = ctk.CTkButton(
            self.left_panel, 
            text="Read Touchstone", 
            command=self.read_touchstone_file
        )
        self.touchstone_btn.pack(pady=(0, 20), padx=20, fill="x")
        
        # Status log
        ctk.CTkLabel(self.left_panel, text="Status Log", font=("Roboto", 12, "bold")).pack(anchor="w", padx=20)
        self.log_text = ctk.CTkTextbox(self.left_panel, height=80)
        self.log_text.pack(pady=(5, 10), padx=20, fill="both", expand=True)
        
        # Theme selector
        ctk.CTkLabel(self.left_panel, text="Appearance", font=("Roboto", 11)).pack(pady=(0, 5))
        self.theme_menu = ctk.CTkOptionMenu(
            self.left_panel,
            values=["System", "Dark", "Light"],
            command=self.change_theme
        )
        self.theme_menu.set("Dark")
        self.theme_menu.pack(pady=(0, 20), padx=20)
        
    def create_plot_panel(self):
        """Create plotting panel with matplotlib"""
        # Plot type selector
        plot_control = ctk.CTkFrame(self.right_panel)
        plot_control.pack(pady=10, padx=10, fill="x")
        
        ctk.CTkLabel(plot_control, text="Plot Type:", font=("Roboto", 12)).pack(side="left", padx=10)
        self.plot_type = ctk.CTkOptionMenu(
            plot_control,
            values=[
                "LogMag (S11)",
                "LogMag (S21)",
                "Combined (S11 & S21)",
                "SWR (S11)",
                "Linear (S11)",
                "Linear (S21)"
            ],
            command=lambda x: self.on_plot_type_changed()
        )
        self.plot_type.set("LogMag (S11)")
        self.plot_type.pack(side="left", padx=10)

        # Statistics
        self.stats_label = ctk.CTkLabel(plot_control, text="Points: 0", font=("Roboto", 11))
        self.stats_label.pack(side="right", padx=20)
        
        # Y-axis scale control
        scale_control = ctk.CTkFrame(self.right_panel)
        scale_control.pack(pady=(0, 5), padx=10, fill="x")
        
        ctk.CTkLabel(scale_control, text="Y-Axis Range:", font=("Roboto", 11)).pack(side="left", padx=10)
        
        self.scale_slider = ctk.CTkSlider(
            scale_control, 
            from_=0, 
            to=1, 
            number_of_steps=1000,
            button_color="#1f6aa5",
            button_hover_color="#144870",
            command=self.update_scale
        )
        # Default to ~40 dB (logarithmic scale: 0.1 to 500 dB)
        self.scale_slider.set(0.4)  
        self.scale_slider.pack(side="left", padx=10, fill="x", expand=True)
        
        self.scale_label = ctk.CTkLabel(scale_control, text="40 dB", font=("Roboto", 10), width=60)
        self.scale_label.pack(side="left", padx=5)
        
        self.auto_scale_btn = ctk.CTkButton(
            scale_control,
            text="Auto Scale",
            width=90,
            command=self.auto_scale_axis
        )
        self.auto_scale_btn.pack(side="left", padx=5)
        
        # Matplotlib figure
        self.figure = Figure(figsize=(8, 6), dpi=100)
        self.ax = self.figure.add_subplot(111)
        self.ax.set_title("S-Parameter Data")
        self.ax.set_xlabel("Frequency (MHz)")
        self.ax.set_ylabel("Magnitude (dB)")
        self.ax.grid(True, alpha=0.3)
        
        # Container for plot and scrollbar
        plot_container = ctk.CTkFrame(self.right_panel)
        plot_container.pack(pady=10, padx=10, fill="both", expand=True)
        plot_container.grid_rowconfigure(0, weight=1)
        plot_container.grid_columnconfigure(0, weight=1)
        
        # Canvas wrapper
        canvas_frame = ctk.CTkFrame(plot_container)
        canvas_frame.grid(row=0, column=0, sticky="nsew")
        
        self.canvas = FigureCanvasTkAgg(self.figure, canvas_frame)
        self.canvas.get_tk_widget().pack(fill="both", expand=True)
        
        # Vertical slider for Y-axis panning - always visible
        slider_frame = ctk.CTkFrame(plot_container, width=30)
        slider_frame.grid(row=0, column=1, sticky="ns", padx=(5, 0))
        slider_frame.grid_propagate(False)
        
        self.y_pan_slider = ctk.CTkSlider(
            slider_frame,
            orientation="vertical",
            width=25,
            from_=-1,
            to=1,
            button_color="#1f6aa5",
            button_hover_color="#144870",
            progress_color="transparent",
            command=self.on_y_pan_slider_move
        )
        self.y_pan_slider.set(0)  # Initialize at center
        self.y_pan_slider.pack(fill="y", expand=True, padx=2, pady=10)
        
        # Add mouse wheel scrolling for Y-axis panning
        self.canvas.mpl_connect('scroll_event', self.on_scroll)

    def on_scroll(self, event):
        """Handle mouse wheel scroll to pan Y-axis"""
        if event.inaxes != self.ax:
            return
        
        # Calculate scroll amount based on current range
        y_range = self.y_max - self.y_min
        scroll_amount = y_range * 0.3  # Scroll by 30% of visible range
        
        if event.button == 'up':
            # Scroll up - shift view downward (reversed)
            self.y_min -= scroll_amount
            self.y_max -= scroll_amount
        elif event.button == 'down':
            # Scroll down - shift view upward (reversed)
            self.y_min += scroll_amount
            self.y_max += scroll_amount
        
        self.update_plot()
        self.update_y_pan_slider()
    
    def on_y_pan_slider_move(self, value):
        """Handle Y-axis pan slider movement with logarithmic scaling"""
        import math
        # Slider range: -1 (bottom) to +1 (top)
        # Use logarithmic mapping for finer control near center
        slider_pos = float(value)
        
        # Logarithmic mapping: center has finer control, extremes coarser
        # Map [-1, 1] to [-500, 500] dB logarithmically
        if slider_pos == 0:
            target_center = 0
        else:
            # Exponential scaling: small values near center, large at extremes
            sign = 1 if slider_pos > 0 else -1
            abs_pos = abs(slider_pos)
            # Map [0, 1] to [0, 500] logarithmically
            max_range = 500
            target_center = sign * (math.exp(abs_pos * math.log(max_range + 1)) - 1)
        
        y_range = self.y_max - self.y_min
        self.y_min = target_center - y_range / 2
        self.y_max = target_center + y_range / 2
        
        self.update_plot()
    
    def update_y_pan_slider(self):
        """Update pan slider position based on current Y-axis center"""
        import math
        # Calculate current center position
        current_center = (self.y_max + self.y_min) / 2
        
        # Inverse logarithmic mapping
        max_range = 500
        if current_center == 0:
            position = 0
        else:
            sign = 1 if current_center > 0 else -1
            abs_center = abs(current_center)
            # Inverse of exp(x * log(max_range + 1)) - 1
            abs_center = max(0, min(max_range, abs_center))  # Clamp
            if abs_center == 0:
                position = 0
            else:
                position = sign * (math.log(abs_center + 1) / math.log(max_range + 1))
        
        position = max(-1.0, min(1.0, position))  # Clamp to -1 to 1
        self.y_pan_slider.set(position)
    
    def sync_sliders_to_state(self):
        """Synchronize both sliders to match current Y-axis state"""
        # Update scale slider
        import math
        min_db, max_db = 0.1, 1000
        current_range = self.y_max - self.y_min
        current_range = max(min_db, min(max_db, current_range))
        slider_pos = math.log(current_range / min_db) / math.log(max_db / min_db)
        
        # Temporarily disable callback to avoid recursion
        old_command = self.scale_slider.cget("command")
        self.scale_slider.configure(command=lambda x: None)
        self.scale_slider.set(slider_pos)
        self.scale_slider.configure(command=old_command)
        
        # Update label
        units = self._current_y_units()
        if current_range >= 1:
            self.scale_label.configure(text=f"{int(current_range)} {units}".strip())
        else:
            self.scale_label.configure(text=f"{current_range:.2f} {units}".strip())
        
        # Update Y-pan slider with logarithmic mapping
        import math
        current_center = (self.y_max + self.y_min) / 2
        max_range = 500
        
        if current_center == 0:
            position = 0
        else:
            sign = 1 if current_center > 0 else -1
            abs_center = abs(current_center)
            abs_center = max(0, min(max_range, abs_center))
            if abs_center == 0:
                position = 0
            else:
                position = sign * (math.log(abs_center + 1) / math.log(max_range + 1))
        
        position = max(-1.0, min(1.0, position))
        
        # Temporarily disable callback
        old_command2 = self.y_pan_slider.cget("command")
        self.y_pan_slider.configure(command=lambda x: None)
        self.y_pan_slider.set(position)
        self.y_pan_slider.configure(command=old_command2)

    def log(self, message):
        """Add message to log"""
        self.log_text.insert("end", f"{message}\n")
        self.log_text.see("end")
    
    def on_plot_type_changed(self):
        """Handle plot type change and trigger auto-scale"""
        self.auto_scale_axis()

    def _current_plot_mode(self):
        """Return tuple (mode, channel) where mode in {'logmag','linear','swr','combined'} and channel in {'S11','S21','BOTH'}"""
        pt = self.plot_type.get()
        if pt.startswith("Combined"):
            return ("combined", "BOTH")
        if pt.startswith("LogMag"):
            chan = "S11" if "S11" in pt else "S21"
            return ("logmag", chan)
        if pt.startswith("Linear"):
            chan = "S11" if "S11" in pt else "S21"
            return ("linear", chan)
        if pt.startswith("SWR"):
            return ("swr", "S11")
        # Legacy fallback
        if "S11" in pt:
            return ("logmag", "S11")
        return ("logmag", "S21")

    def _compute_values_for_plot(self, freq_data, mode, channel):
        """Given freq_data {freq: (s11_db, s21_db)}, return (sorted_freqs, values) for selected plot"""
        sorted_freqs = sorted(freq_data.keys())
        if mode == "logmag":
            if channel == "S11":
                values = [freq_data[f][0] for f in sorted_freqs]
            else:
                values = [freq_data[f][1] for f in sorted_freqs]
            return sorted_freqs, values
        elif mode == "linear":
            # Convert dB magnitude to linear magnitude |S|
            if channel == "S11":
                db_vals = [freq_data[f][0] for f in sorted_freqs]
            else:
                db_vals = [freq_data[f][1] for f in sorted_freqs]
            values = [10 ** (v / 20.0) for v in db_vals]
            return sorted_freqs, values
        elif mode == "swr":
            # SWR from |Γ| where |Γ| = 10^(S11_dB/20)
            db_vals = [freq_data[f][0] for f in sorted_freqs]
            rho_vals = [max(0.0, min(0.9999, 10 ** (v / 20.0))) for v in db_vals]
            values = [(1 + r) / (1 - r) if r < 1.0 else 999.0 for r in rho_vals]
            return sorted_freqs, values
        else:
            values = [freq_data[f][0] for f in sorted_freqs]
            return sorted_freqs, values

    def _current_y_units(self):
        mode, _ = self._current_plot_mode()
        if mode == "logmag" or mode == "combined":
            return "dB"
        if mode == "swr":
            return "SWR"
        return ""
    
    def update_scale(self, value):
        """Update Y-axis range based on slider (logarithmic scale)"""
        # Logarithmic mapping: 0->0.1 dB, 1->1000 dB
        import math
        min_db, max_db = 0.1, 1000
        slider_pos = float(value)
        range_db = min_db * math.exp(slider_pos * math.log(max_db / min_db))
        
        units = self._current_y_units()
        if range_db >= 1:
            self.scale_label.configure(text=f"{int(range_db)} {units}".strip())
        else:
            self.scale_label.configure(text=f"{range_db:.2f} {units}".strip())
        
        # Adjust y_max to maintain the range centered on current data
        y_center = (self.y_max + self.y_min) / 2
        self.y_min = y_center - range_db / 2
        self.y_max = y_center + range_db / 2
        
        self.update_plot()
    
    def auto_scale_axis(self):
        """Automatically adjust Y-axis to fit current data with small buffer"""
        # Collect all current values according to selected mode
        all_values = []
        mode, channel = self._current_plot_mode()
        # From history
        for sweep_data in self.sweep_history:
            for vna_id, freq_data in sweep_data.items():
                if mode == "combined":
                    # Include both S11 and S21 values
                    sorted_freqs = sorted(freq_data.keys())
                    all_values.extend([freq_data[f][0] for f in sorted_freqs])  # S11
                    all_values.extend([freq_data[f][1] for f in sorted_freqs])  # S21
                else:
                    _, vals = self._compute_values_for_plot(freq_data, mode, channel)
                    all_values.extend(vals)
        # From current sweep
        for vna_id, freq_data in self.current_sweep.items():
            if mode == "combined":
                sorted_freqs = sorted(freq_data.keys())
                all_values.extend([freq_data[f][0] for f in sorted_freqs])  # S11
                all_values.extend([freq_data[f][1] for f in sorted_freqs])  # S21
            else:
                _, vals = self._compute_values_for_plot(freq_data, mode, channel)
                all_values.extend(vals)
        
        if all_values:
            y_min, y_max = min(all_values), max(all_values)
            y_range = y_max - y_min
            
            # Add 1% buffer (minimum 0.1 dB)
            buffer = max(0.1, y_range * 0.01)
            
            self.y_min = y_min - buffer
            self.y_max = y_max + buffer
            
            # Update slider to match the new range (inverse logarithmic mapping)
            import math
            min_db, max_db = 0.1, 1000
            new_range = self.y_max - self.y_min
            new_range = max(min_db, min(max_db, new_range))  # Clamp to valid range
            slider_pos = math.log(new_range / min_db) / math.log(max_db / min_db)
            self.scale_slider.set(slider_pos)
            
            units = self._current_y_units()
            if new_range >= 1:
                self.scale_label.configure(text=f"{int(new_range)} {units}".strip())
            else:
                self.scale_label.configure(text=f"{new_range:.2f} {units}".strip())
            
            self.auto_scaled = True
            self.update_plot()
            self.update_y_pan_slider()
            units = self._current_y_units()
            self.log(f"Auto-scaled Y-axis: {self.y_min:.2f} to {self.y_max:.2f} {units}".strip())
    
    def toggle_sweep_mode(self):
        """Toggle between continuous scan and fixed sweep count mode"""
        if self.sweep_mode.get() == 1:
            # Sweep mode enabled - enable sweeps field, disable time limit
            self.num_sweeps.configure(state="normal", text_color=("gray10", "gray90"))
            self.num_sweeps_label.configure(text_color=("gray10", "gray90"))
            
            self.time_limit.configure(state="disabled", text_color="gray50")
            self.time_limit_label.configure(text_color="gray50")
        else:
            # Continuous scan mode - disable sweeps field, enable time limit
            self.num_sweeps.configure(state="disabled", text_color="gray50")
            self.num_sweeps_label.configure(text_color="gray50")
            
            self.time_limit.configure(state="normal", text_color=("gray10", "gray90"))
            self.time_limit_label.configure(text_color=("gray10", "gray90"))
        self.update_scan_button_text()
    
    def slider_to_multiplier(self, slider_value):
        """Convert slider position (0-100) to multiplier using logarithmic scale"""
        try:
            start = int(self.start_freq.get())
            stop = int(self.stop_freq.get())
            bandwidth = stop - start
            if bandwidth <= 0:
                return 1
            
            # Calculate theoretical max (1 Hz spacing)
            max_useful = max(1, bandwidth // 101)
            
            # Limit to practical range (avoid extreme values)
            max_value = min(max_useful, 100000)
            
            # Logarithmic mapping: 0 → 1, 100 → max_value
            if max_value <= 1:
                return 1
            
            import math
            normalized = slider_value / 100.0  # 0.0 to 1.0
            multiplier = 10 ** (normalized * math.log10(max_value))
            return max(1, int(round(multiplier)))
        except (ValueError, ZeroDivisionError):
            return 1
    


    def slider_to_points(self, slider_value):
        """Convert slider value (0-200) to total points with special scaling.
        0-100: fine control from 1 to 101 points (increments of 1)
        101-200: coarse control from 202 to 1111 points (multiples of 101)
        """
        slider_value = int(slider_value)
        if slider_value <= 100:
            # Fine control: 1 to 101 points
            return max(1, slider_value + 1)
        else:
            # Coarse control: multiples of 101 (202, 303, 404, ...)
            multiplier = slider_value - 100 + 1  # 2, 3, 4, ...
            return multiplier * 101
    
    def calculate_scan_params_from_points(self, total_points):
        """Calculate optimal scans and pps for given total points, prioritizing speed."""
        max_pps = 101  # Hardware limit
        
        if total_points <= max_pps:
            # Single scan with reduced pps
            return 1, total_points, total_points
        else:
            # Multiple scans at max pps
            scans = math.ceil(total_points / max_pps)
            actual_points = scans * max_pps
            return scans, max_pps, actual_points
    
    def update_points_display(self, slider_value):
        """Update the points display with current parameters"""
        try:
            total_points = self.slider_to_points(self.points_slider.get())
            scans, pps, actual_points = self.calculate_scan_params_from_points(total_points)
            
            start = int(self.start_freq.get())
            stop = int(self.stop_freq.get())
            bandwidth = stop - start
            
            if bandwidth > 0:
                spacing = bandwidth / actual_points
                
                # Format spacing nicely
                if spacing >= 1e6:
                    spacing_str = f"{spacing/1e6:.2f} MHz"
                elif spacing >= 1e3:
                    spacing_str = f"{spacing/1e3:.1f} kHz"
                else:
                    spacing_str = f"{spacing:.0f} Hz"
                
                # Speed indication
                if scans == 1:
                    speed = "Fastest" if pps == 101 else "Fast"
                else:
                    speed = "Medium" if scans <= 5 else "Slow"
                
                self.points_display.configure(
                    text=f"{actual_points} pts ({scans} scan × {pps} pps) - {speed} - {spacing_str} spacing"
                )
            else:
                self.points_display.configure(text=f"{actual_points} points - Invalid frequency range")
        except ValueError:
            self.points_display.configure(text="101 points - Enter valid frequencies")
    
    def update_scan_button_text(self):
        """Update the start button text based on time limit and sweep mode"""
        try:
            time_limit = int(self.time_limit.get() or "300")
            if self.sweep_mode.get() == 1:
                self.start_button.configure(text="Start Sweep Scan")
            else:
                self.start_button.configure(text=f"Scan for {time_limit} seconds")
        except ValueError:
            self.start_button.configure(text="Scan for 300 seconds")
    
    def show_tooltip(self, event, text):
        """Show tooltip near the cursor"""
        if self.tooltip_window:
            self.tooltip_window.destroy()
        
        x = event.widget.winfo_rootx() + 20
        y = event.widget.winfo_rooty() + 20
        
        self.tooltip_window = ctk.CTkToplevel(self.root)
        self.tooltip_window.wm_overrideredirect(True)
        self.tooltip_window.wm_geometry(f"+{x}+{y}")
        
        label = ctk.CTkLabel(self.tooltip_window, text=text, justify="left",
                            fg_color=("#ffffe0", "#3a3a3a"), corner_radius=6,
                            text_color=("black", "white"), padx=10, pady=5)
        label.pack()
    
    def hide_tooltip(self):
        """Hide tooltip"""
        if self.tooltip_window:
            self.tooltip_window.destroy()
            self.tooltip_window = None

    def clear_data(self):
        """Clear all scan data"""
        self.scan_data = []
        self.sweep_history = []
        self.current_sweep = {}
        self.sweep_count = 0
        self.auto_scaled = False  # Reset auto-scale flag
        
        self.ax.clear()
        self.ax.set_xlabel("Frequency (MHz)")
        self.ax.set_ylabel("Magnitude (dB)")
        self.ax.set_title("S-Parameter Data")
        self.ax.grid(True, alpha=0.3)
        
        # Set fixed X-axis based on frequency settings
        try:
            start = int(self.start_freq.get()) / 1e6
            stop = int(self.stop_freq.get()) / 1e6
            self.ax.set_xlim(start, stop)
        except ValueError:
            pass
        
        self.canvas.draw()
        self.stats_label.configure(text="Points: 0 | Sweeps: 0")
        self.log("Data cleared")
    
    def generate_distinct_colors(self, n):
        """Generate n maximally distinct colors using golden ratio in HSV space.
        Returns list of hex color strings."""
        if n <= 0:
            return []
        
        colors = []
        # Golden ratio conjugate for even distribution
        golden_ratio = 0.618033988749895
        
        # Start with a random-ish hue, then use golden ratio to spread
        hue = 0.0
        for i in range(n):
            # High saturation and value for vivid, visible colors
            saturation = 0.85 + 0.15 * (i % 2)  # Alternate between 0.85 and 1.0
            value = 0.95 - 0.1 * ((i // 2) % 2)  # Alternate between 0.85 and 0.95
            
            r, g, b = colorsys.hsv_to_rgb(hue, saturation, value)
            hex_color = f'#{int(r*255):02x}{int(g*255):02x}{int(b*255):02x}'
            colors.append(hex_color)
            
            hue = (hue + golden_ratio) % 1.0
        
        return colors
    
    def desaturate_color(self, hex_color, saturation_factor):
        """Desaturate a hex color by the given factor (0=grayscale, 1=original)."""
        # Parse hex color
        r = int(hex_color[1:3], 16) / 255.0
        g = int(hex_color[3:5], 16) / 255.0
        b = int(hex_color[5:7], 16) / 255.0
        
        # Convert to HSV
        h, s, v = colorsys.rgb_to_hsv(r, g, b)
        
        # Reduce saturation
        s = s * saturation_factor
        
        # Convert back to RGB
        r, g, b = colorsys.hsv_to_rgb(h, s, v)
        
        return f'#{int(r*255):02x}{int(g*255):02x}{int(b*255):02x}'
    
    def get_vna_color(self, vna_id, total_vnas):
        """Get color for a specific VNA, generating colors dynamically if needed."""
        # Regenerate cache if we have more VNAs than cached colors
        if total_vnas > len(self._vna_color_cache):
            colors = self.generate_distinct_colors(max(total_vnas, 8))
            self._vna_color_cache = {i: colors[i] for i in range(len(colors))}
        
        return self._vna_color_cache.get(vna_id, '#FFFFFF')
    
    def update_plot(self):
        """Update plot with sweep history - older data more transparent, different VNA colors"""
        self.ax.clear()
        
        # Fixed X-axis based on frequency settings
        try:
            start_mhz = int(self.start_freq.get()) / 1e6
            stop_mhz = int(self.stop_freq.get()) / 1e6
            self.ax.set_xlim(start_mhz, stop_mhz)
        except ValueError:
            start_mhz, stop_mhz = 50, 900  # Default
            self.ax.set_xlim(start_mhz, stop_mhz)
        
        mode, channel = self._current_plot_mode()
        
        all_values = []  # Collect all values for Y-axis scaling
        
        # Collect all VNA IDs to determine total count for color generation
        all_vna_ids = set()
        for sweep in self.sweep_history:
            all_vna_ids.update(sweep.keys())
        all_vna_ids.update(self.current_sweep.keys())
        total_vnas = max(len(all_vna_ids), 1)
        
        # Plot sweep history (older sweeps first, more transparent)
        total_sweeps = len(self.sweep_history)
        
        for sweep_idx, sweep_data in enumerate(self.sweep_history):
            # Calculate opacity: oldest = very faint, newest history = semi-visible
            if total_sweeps > 1:
                age_ratio = sweep_idx / (total_sweeps - 1)  # 0 = oldest, 1 = newest in history
            else:
                age_ratio = 0.5
            alpha = 0.15 + 0.35 * age_ratio  # Range: 0.15 to 0.5 for history
            
            # Desaturate older sweeps (oldest = grayscale, newest history = 50% saturation)
            saturation_factor = 0.1 + 0.4 * age_ratio  # Range: 0.1 to 0.5
            
            # Plot each VNA's data from this sweep
            for vna_id, freq_data in sweep_data.items():
                base_color = self.get_vna_color(vna_id, total_vnas)
                color = self.desaturate_color(base_color, saturation_factor)
                
                # Thin lines for history
                linewidth = 0.8 + 0.7 * age_ratio
                
                if mode == "combined":
                    # Plot both S11 and S21 with different line styles
                    sorted_freqs = sorted(freq_data.keys())
                    freqs_mhz = [f / 1e6 for f in sorted_freqs]
                    s11_values = [freq_data[f][0] for f in sorted_freqs]
                    s21_values = [freq_data[f][1] for f in sorted_freqs]
                    all_values.extend(s11_values)
                    all_values.extend(s21_values)
                    # S11: solid line
                    self.ax.plot(freqs_mhz, s11_values, color=color, linewidth=linewidth, 
                               alpha=alpha, zorder=sweep_idx, linestyle='-')
                    # S21: dashed line
                    self.ax.plot(freqs_mhz, s21_values, color=color, linewidth=linewidth, 
                               alpha=alpha, zorder=sweep_idx, linestyle='--')
                else:
                    # Prepare values for selected plot mode
                    sorted_freqs, values = self._compute_values_for_plot(freq_data, mode, channel)
                    freqs_mhz = [f / 1e6 for f in sorted_freqs]
                    all_values.extend(values)
                    self.ax.plot(freqs_mhz, values, color=color, linewidth=linewidth, 
                               alpha=alpha, zorder=sweep_idx)
        
        # Plot current sweep (being collected) - PROMINENTLY VISIBLE
        if self.current_sweep:
            for vna_id, freq_data in self.current_sweep.items():
                if freq_data:
                    color = self.get_vna_color(vna_id, total_vnas)
                    
                    if mode == "combined":
                        # Plot both S11 and S21 with different line styles
                        sorted_freqs = sorted(freq_data.keys())
                        freqs_mhz = [f / 1e6 for f in sorted_freqs]
                        s11_values = [freq_data[f][0] for f in sorted_freqs]
                        s21_values = [freq_data[f][1] for f in sorted_freqs]
                        all_values.extend(s11_values)
                        all_values.extend(s21_values)
                        
                        # S11: solid line with white glow
                        self.ax.plot(freqs_mhz, s11_values, color='white', linewidth=5, 
                                   alpha=0.8, zorder=total_sweeps + 1, linestyle='-')
                        self.ax.plot(freqs_mhz, s11_values, color=color, linewidth=3, 
                                   alpha=1.0, zorder=total_sweeps + 2, linestyle='-')
                        
                        # S21: dashed line with white glow
                        self.ax.plot(freqs_mhz, s21_values, color='white', linewidth=5, 
                                   alpha=0.8, zorder=total_sweeps + 1, linestyle='--')
                        self.ax.plot(freqs_mhz, s21_values, color=color, linewidth=3, 
                                   alpha=1.0, zorder=total_sweeps + 2, linestyle='--')
                    else:
                        sorted_freqs, values = self._compute_values_for_plot(freq_data, mode, channel)
                        freqs_mhz = [f / 1e6 for f in sorted_freqs]
                        all_values.extend(values)
                        
                        # Draw white outline/glow behind the current line for visibility
                        self.ax.plot(freqs_mhz, values, color='white', linewidth=5, 
                                   alpha=0.8, zorder=total_sweeps + 1)
                        
                        # Draw the actual current sweep line - thick and bold
                        self.ax.plot(freqs_mhz, values, color=color, linewidth=3, 
                                   alpha=1.0, zorder=total_sweeps + 2)
        
        # Apply fixed Y-axis limits from user control
        self.ax.set_ylim(self.y_min, self.y_max)
        
        # Update sliders to reflect current state
        self.sync_sliders_to_state()
        
        # Labels and styling
        self.ax.set_xlabel("Frequency (MHz)", fontsize=11, fontweight='bold')
        y_units = self._current_y_units()
        if mode == "logmag":
            self.ax.set_ylabel(f"{channel} LogMag ({y_units})".strip(), fontsize=11, fontweight='bold')
            self.ax.set_title(f"{channel} LogMag vs Frequency", fontsize=13, fontweight='bold')
        elif mode == "combined":
            self.ax.set_ylabel(f"Magnitude ({y_units})".strip(), fontsize=11, fontweight='bold')
            self.ax.set_title("S11 & S21 Combined vs Frequency", fontsize=13, fontweight='bold')
        elif mode == "linear":
            self.ax.set_ylabel(f"{channel} Linear Magnitude", fontsize=11, fontweight='bold')
            self.ax.set_title(f"{channel} Linear Magnitude vs Frequency", fontsize=13, fontweight='bold')
        elif mode == "swr":
            self.ax.set_ylabel("SWR", fontsize=11, fontweight='bold')
            self.ax.set_title("SWR (from S11) vs Frequency", fontsize=13, fontweight='bold')
        self.ax.grid(True, alpha=0.3)
        
        # Add comprehensive legend
        from matplotlib.lines import Line2D
        from matplotlib.patches import Patch
        legend_handles = []
        
        # Add VNA colors to legend
        for vna_id in sorted(all_vna_ids):
            color = self.get_vna_color(vna_id, total_vnas)
            legend_handles.append(Line2D([0], [0], color=color, linewidth=2, 
                                        label=f'VNA {vna_id}'))
        
        # Add line style legend for combined mode
        if mode == "combined":
            legend_handles.append(Line2D([0], [0], color='gray', linewidth=2, 
                                        linestyle='-', label='S11 (solid)'))
            legend_handles.append(Line2D([0], [0], color='gray', linewidth=2, 
                                        linestyle='--', label='S21 (dashed)'))
        
        # Add sweep status indicators (omit live view legend)
        if self.sweep_history:
            legend_handles.append(Line2D([0], [0], color='gray', linewidth=1, 
                                        alpha=0.4, linestyle='-', label=f'History ({len(self.sweep_history)} sweeps)'))
        
        if legend_handles:
            self.ax.legend(handles=legend_handles, loc='upper right', fontsize=8, 
                          framealpha=0.9, edgecolor='gray')
        
        self.canvas.draw()
        
        # Update stats
        total_points = sum(len(fd) for sweep in self.sweep_history for fd in sweep.values())
        total_points += sum(len(fd) for fd in self.current_sweep.values())
        live_str = " [LIVE]" if self.current_sweep else ""
        self.stats_label.configure(text=f"Points: {total_points} | Sweeps: {len(self.sweep_history)}{live_str}")
    
    def _schedule_plot_update(self):
        """Schedule a plot update (debounced)"""
        if not self._plot_update_pending:
            self._plot_update_pending = True
            self.root.after(self._update_interval, self._do_plot_update)
    
    def _do_plot_update(self):
        """Actually perform the plot update"""
        self._plot_update_pending = False
        self.update_plot()
    
    def on_data_point(self, point: VNADataPoint):
        """Callback when a data point is received from the scanner"""
        self.scan_data.append(point)
        
        vna_id = point.vna_id
        freq = point.frequency
        
        # Initialize VNA entry in current sweep if needed
        if vna_id not in self.current_sweep:
            self.current_sweep[vna_id] = {}
            self.root.after(0, lambda vid=vna_id: self.log(f"VNA{vid} connected - receiving data"))
        
        # Check if this frequency was already seen for this VNA in current sweep
        # If so, this VNA is starting a new sweep
        if freq in self.current_sweep[vna_id]:
            # This VNA has wrapped around - save current sweep and start new one
            # Archive current sweep if it has data
            if self.current_sweep and any(len(data) > 0 for data in self.current_sweep.values()):
                # Deep copy each VNA's data
                sweep_copy = {vid: dict(fdata) for vid, fdata in self.current_sweep.items()}
                self.sweep_history.append(sweep_copy)
                self.sweep_count += 1
                
                # Log VNA data counts for first few sweeps
                if self.sweep_count <= 3:
                    vna_counts = {vid: len(fdata) for vid, fdata in sweep_copy.items()}
                    self.root.after(0, lambda c=vna_counts, n=self.sweep_count: 
                                  self.log(f"Sweep {n}: {' | '.join(f'VNA{k}: {v}pts' for k,v in sorted(c.items()))}"))
                
                # Auto-scale after first sweep
                if self.sweep_count == 1 and not self.auto_scaled:
                    self.root.after(0, self.auto_scale_axis)
                
                # Trim old sweeps to keep memory bounded
                if len(self.sweep_history) > self.max_sweep_history:
                    self.sweep_history.pop(0)
            
            # Start fresh sweep - clear all VNAs
            self.current_sweep = {}
        
        # Ensure vna_id entry exists (may have been cleared above)
        if vna_id not in self.current_sweep:
            self.current_sweep[vna_id] = {}
        
        # Store data point: (s11_mag_db, s21_mag_db)
        self.current_sweep[vna_id][freq] = (point.s11_mag_db, point.s21_mag_db)
        
        # Schedule plot update (runs on main thread)
        self.root.after(0, self._schedule_plot_update)
    
    def on_status_update(self, message: str):
        """Callback for scanner status updates"""
        self.root.after(0, lambda: self.log(message))
        
        # Check if scan complete
        if "complete" in message.lower() or "stopped" in message.lower():
            self.root.after(0, self._scan_finished)
    
    def _scan_finished(self):
        """Called when scan finishes"""
        self.start_button.configure(state="normal")
        self.stop_button.configure(state="disabled")
        self.update_plot()  # Final update
    
    def start_scan(self):
        """Start a VNA scan"""
        if not self.scanner_available:
            self.log("ERROR: VnaCommandParser not found!")
            self.log("Please run 'make VnaCommandParser' in src/VnaScanC/")
            return
        
        if self.scanner.is_scanning:
            self.log("Scan already in progress")
            return
        
        try:
            # Get parameters
            start_freq = int(self.start_freq.get())
            stop_freq = int(self.stop_freq.get())
            
            # Validate frequencies
            if start_freq >= stop_freq:
                self.log("ERROR: Start frequency must be less than stop frequency")
                return
            if start_freq < 10000 or stop_freq > 1500000000:
                self.log("ERROR: Frequencies must be between 10kHz and 1.5GHz")
                return
            
            # Calculate optimal scan parameters from points slider
            total_points = self.slider_to_points(self.points_slider.get())
            scans, pps, actual_points = self.calculate_scan_params_from_points(total_points)
            
            # Get sweep settings
            sweep_mode = self.sweep_mode.get() == 1
            time_limit = int(self.time_limit.get() or "0")
            num_sweeps = int(self.num_sweeps.get() or "1")
            
            # Get ports
            ports_text = self.ports_text.get("1.0", "end").strip()
            ports = [p.strip() for p in ports_text.split("\n") if p.strip()]
            
            if not ports:
                self.log("ERROR: No VNA ports specified")
                return
            
            # Clear previous data for fresh scan
            self.clear_data()
            
            # Reset auto-scale flag for new scan
            self.auto_scaled = False
            
            # Update UI
            self.start_button.configure(state="disabled")
            self.stop_button.configure(state="normal")
            
            self.log(f"Starting scan: {start_freq/1e6:.1f} - {stop_freq/1e6:.1f} MHz")
            self.log(f"Resolution: {scans} scans × {pps} pps ({actual_points} points)")
            self.log(f"Ports: {', '.join(ports)}")
            
            # Determine scan mode
            if sweep_mode:
                self.log(f"Mode: FIXED SWEEPS ({num_sweeps} sweeps)")
            else:
                # Ensure time_limit is at least 1 second
                if time_limit < 1:
                    time_limit = 300
                    self.log(f"Mode: TIMED ({time_limit} seconds) - using default")
                else:
                    self.log(f"Mode: TIMED ({time_limit} seconds)")
            
            # Start the scan
            success = self.scanner.start_scan(
                start_freq=start_freq,
                stop_freq=stop_freq,
                num_scans=scans,
                num_sweeps=num_sweeps if sweep_mode else 1,
                points_per_scan=pps,
                time_mode=not sweep_mode,
                time_limit=time_limit,
                ports=ports,
                data_callback=self.on_data_point,
                status_callback=self.on_status_update
            )
            
            if not success:
                self.start_button.configure(state="normal")
                self.stop_button.configure(state="disabled")
                
        except ValueError as e:
            self.log(f"ERROR: Invalid parameter - {e}")
            self.start_button.configure(state="normal")
            self.stop_button.configure(state="disabled")
    
    def stop_scan(self):
        """Stop the current scan"""
        if self.scanner and self.scanner.is_scanning:
            self.scanner.stop_scan()
            self.log("Stopping scan...")
    
    def read_touchstone_file(self):
        """Open and read a touchstone file"""
        filepath = filedialog.askopenfilename(
            title="Select Touchstone File",
            filetypes=[("Touchstone files", "*.s2p"), ("All files", "*.*")],
            initialdir=os.getcwd()
        )
        
        if not filepath:
            return
        
        try:
            points = VNAScanner.read_touchstone(filepath)
            
            if not points:
                self.log(f"No data found in {filepath}")
                return
            
            # Clear and load data
            self.clear_data()
            
            for point in points:
                self.scan_data.append(point)
                freq = point.frequency
                if freq not in self.frequencies:
                    self.frequencies.append(freq)
                self.s11_data[freq] = point.s11_mag_db
                self.s21_data[freq] = point.s21_mag_db
            
            self.update_plot()
            self.log(f"Loaded {len(points)} points from {os.path.basename(filepath)}")
            
        except Exception as e:
            self.log(f"ERROR reading file: {e}")
    
    def detect_vnas(self):
        """Auto-detect and validate connected VNA devices"""
        if self.scanner_available:
            ports = self.scanner.detect_vnas()
        else:
            ports = sorted(glob.glob("/dev/ttyACM*"))
        
        if not ports:
            self.log("No VNA devices found on /dev/ttyACM*")
            return
        
        self.log(f"Found {len(ports)} serial device(s), validating...")
        
        # Validate each port by trying to add it via VnaCommandParser
        # Run validation twice - some VNAs need to wake up on first ping
        valid_ports = []
        for port in ports:
            # Test if this is actually a NanoVNA-H (run twice)
            is_valid = False
            for attempt in range(2):
                try:
                    result = subprocess.run(
                        [self.scanner.parser_path],
                        input=f"vna add {port}\nexit\n",
                        capture_output=True,
                        text=True,
                        timeout=2
                    )
                    
                    # Check if it was successfully added (no error message)
                    if "Serial device is not a NanoVNA-H" not in result.stderr:
                        is_valid = True
                        break  # Success, no need for second attempt
                except Exception as e:
                    if attempt == 1:  # Only log error on final attempt
                        self.log(f"✗ {port} - Validation failed: {e}")
            
            if is_valid:
                valid_ports.append(port)
                self.log(f"✓ {port} - Valid NanoVNA-H")
            else:
                self.log(f"✗ {port} - Not a NanoVNA-H or not responding")
        
        if not valid_ports:
            self.log("ERROR: No valid NanoVNA-H devices found!")
            return
        
        self.num_vnas.delete(0, "end")
        self.num_vnas.insert(0, str(len(valid_ports)))
        
        self.ports_text.delete("1.0", "end")
        self.ports_text.insert("1.0", "\n".join(valid_ports))
        
        self.log(f"✓ Ready: {len(valid_ports)} valid NanoVNA-H device(s)")
        
    def change_theme(self, theme):
        """Change appearance theme"""
        ctk.set_appearance_mode(theme.lower())

    def run(self):
        """Start the GUI main loop"""
        self.update_points_display(None)
        self.root.mainloop()

if __name__ == "__main__":
    app = VNAScannerGUI()
    app.run()
    
