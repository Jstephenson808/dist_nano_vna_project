import customtkinter as ctk
import subprocess
import threading
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from matplotlib.figure import Figure
import numpy as np
import glob
from vnaScanAPI import VNAScanAPI, ScanParameters, SweepMode

class VNAScannerGUI:
    def __init__(self):
        # Main window
        self.root = ctk.CTk()
        self.root.title("VNA Scanner - Multi-threaded Control")
        self.root.geometry("1400x1400")
        
        # State
        self.tooltip_window = None  # For help tooltips
        
        # Initialize VNA API and data storage
        try:
            self.vna_api = VNAScanAPI()
            print("✓ VNA API initialized successfully")
        except Exception as e:
            self.vna_api = None
            print(f"Warning: Failed to initialize VNA API: {e}")
        
        # Data storage for real-time plotting (thread-safe)
        self.scan_data = []
        self.data_lock = threading.Lock()

        ctk.set_appearance_mode("dark")
        ctk.set_default_color_theme("blue")

        self.setup_ui_structure()
        
        # Start periodic state synchronization
        self.root.after(2000, self.sync_scan_state)

    def setup_ui_structure(self):
        """Setup the user interface"""
        # Main container with two columns
        self.root.grid_columnconfigure(1, weight=1)
        self.root.grid_rowconfigure(0, weight=1)
        
        # Left panel - Controls
        self.left_panel = ctk.CTkFrame(self.root, width=300)
        self.left_panel.grid(row=0, column=0, padx=10, pady=10, sticky="nsew")
        self.left_panel.grid_propagate(False)
        
        # Right panel - Plots
        self.right_panel = ctk.CTkFrame(self.root)
        self.right_panel.grid(row=0, column=1, padx=(0, 10), pady=10, sticky="nsew")

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
        self.start_freq.bind("<KeyRelease>", lambda e: self.update_resolution_display(None))
        
        ctk.CTkLabel(freq_frame, text="Stop (Hz):").pack(anchor="w", padx=10)
        self.stop_freq = ctk.CTkEntry(freq_frame, placeholder_text="900000000")
        self.stop_freq.insert(0, "900000000")
        self.stop_freq.pack(padx=10, pady=(0, 10), fill="x")
        self.stop_freq.bind("<KeyRelease>", lambda e: self.update_resolution_display(None))
        
        # Scan Control
        scan_frame = ctk.CTkFrame(self.left_panel)
        scan_frame.pack(pady=10, padx=20, fill="x")

        
        ctk.CTkLabel(scan_frame, text="Scan Control", font=("Roboto", 14, "bold")).pack(pady=(10, 5))
        
        # Time limit (0 = continuous)
        self.time_limit_label = ctk.CTkLabel(scan_frame, text="Time Limit (seconds, 0=continuous):")
        self.time_limit_label.pack(anchor="w", padx=10)
        self.time_limit = ctk.CTkEntry(scan_frame, placeholder_text="0")
        self.time_limit.insert(0, "0")
        self.time_limit.pack(padx=10, pady=(0, 10), fill="x")
        self.time_limit.bind("<KeyRelease>", lambda e: self.update_scan_button_text())

        # Resolution Multiplier with tooltip
        res_label_frame = ctk.CTkFrame(scan_frame, fg_color="transparent")
        res_label_frame.pack(anchor="w", padx=10, fill="x")
        
        self.num_scans_label = ctk.CTkLabel(res_label_frame, text="Resolution Multiplier:", text_color=("gray10", "gray90"))
        self.num_scans_label.pack(side="left")
        
        self.res_help_label = ctk.CTkLabel(res_label_frame, text="?", text_color=("#4a9eff", "#1f6aa5"),
                                           font=("Roboto", 12, "bold"), cursor="hand2")
        self.res_help_label.pack(side="left", padx=(5, 0))
        self.res_help_label.bind("<Enter>", lambda e: self.show_tooltip(e, "Multiplies base 101 frequency points across defined bandwidth.\nExample: 2 = 202 points, 10 = 1010 points"))
        self.res_help_label.bind("<Leave>", lambda e: self.hide_tooltip())
        self.res_help_label.bind("<Button-1>", lambda e: self.show_tooltip(e, "Multiplies base 101 frequency points across defined bandwidth.\nExample: 2 = 202 points, 10 = 1010 points"))
        
        # Slider for resolution (0-100 scale)
        self.num_scans_slider = ctk.CTkSlider(scan_frame, from_=0, to=100, number_of_steps=100,
                                              command=self.update_resolution_display)
        self.num_scans_slider.set(0)  # Start at multiplier=1
        self.num_scans_slider.pack(padx=10, pady=(0, 5), fill="x")
        
        # Display current value and frequency spacing
        self.resolution_display = ctk.CTkLabel(scan_frame, text="Multiplier: 1 (101 pts)",
                                               font=("Roboto", 10), text_color="gray60")
        self.resolution_display.pack(anchor="w", padx=10, pady=(0, 10))
        
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
            text="Start Continuous Scan", 
            fg_color=("#16A500", "#14A007"),
            hover_color="#043100",
            font=("Roboto", 14, "bold"),
            command=self.start_scan  # LINK: Button → start_scan function
        )
        self.start_button.pack(pady=10, padx=10, fill="x")
        
        self.stop_button = ctk.CTkButton(
            scan_frame,
            text="Stop Scan",
            fg_color=("#AE0006", "#B80006"),
            hover_color="#4E0003",
            state="disabled",
            font=("Roboto", 14),
            command=self.stop_scan  # LINK: Button → stop_scan function
        )
        self.stop_button.pack(pady=(0, 10), padx=10, fill="x")

        # VNA configuration
        vna_frame = ctk.CTkFrame(self.left_panel)
        vna_frame.pack(pady=10, padx=20, fill="x")
        
        ctk.CTkLabel(vna_frame, text="VNA Configuration", font=("Roboto", 14, "bold")).pack(pady=(10, 5))
        
        ctk.CTkLabel(vna_frame, text="Number of VNAs:").pack(anchor="w", padx=10)
        self.num_vnas = ctk.CTkEntry(vna_frame, placeholder_text="1")  # Changed from "2" to "1"
        self.num_vnas.insert(0, "1")  # Only one VNA available
        self.num_vnas.pack(padx=10, pady=(0, 5), fill="x")
        
        detect_btn = ctk.CTkButton(vna_frame, text="Auto-detect VNAs", command=self.detect_vnas)
        detect_btn.pack(padx=10, pady=(0, 10), fill="x")
        
        ctk.CTkLabel(vna_frame, text="Serial Ports (one per line):").pack(anchor="w", padx=10)
        self.ports_text = ctk.CTkTextbox(vna_frame, height=80)
        self.ports_text.insert("1.0", "/dev/ttyACM0")  # Only the available port
        self.ports_text.pack(padx=10, pady=(0, 10), fill="x")
        
        # Utility buttons
        clear_btn = ctk.CTkButton(self.left_panel, text="Clear Data", command=self.clear_data)
        clear_btn.pack(pady=(0, 10), padx=20, fill="x")
        
        touchstone_btn = ctk.CTkButton(self.left_panel, text="Read Touchstone")
        touchstone_btn.pack(pady=(0, 20), padx=20, fill="x")
        touchstone_btn.configure(state="disabled")
        
        # Status log
        ctk.CTkLabel(self.left_panel, text="Status Log", font=("Roboto", 12, "bold")).pack(anchor="w", padx=20)
        self.log_text = ctk.CTkTextbox(self.left_panel, height=100)
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
            values=["S11 Magnitude (dB)", "S21 Magnitude (dB)"],
            command=lambda x: self.update_plot()
        )
        self.plot_type.set("S11 Magnitude (dB)")
        self.plot_type.pack(side="left", padx=10)

        # Statistics
        self.stats_label = ctk.CTkLabel(plot_control, text="Points: 0", font=("Roboto", 11))
        self.stats_label.pack(side="right", padx=20)
        
        # Matplotlib figure
        self.figure = Figure(figsize=(8, 6), dpi=100)
        self.ax = self.figure.add_subplot(111)
        self.ax.set_title("S-Parameter Data")
        self.ax.set_xlabel("Frequency (MHz)")
        self.ax.set_ylabel("Magnitude (dB)")
        self.ax.grid(True, alpha=0.3)
        
        self.canvas = FigureCanvasTkAgg(self.figure, self.right_panel)
        self.canvas.get_tk_widget().pack(pady=10, padx=10, fill="both", expand=True)

    def log(self, message):
        """Add message to log"""
        self.log_text.insert("end", f"{message}\n")
        self.log_text.see("end")
    
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
    
    def update_resolution_display(self, slider_value):
        """Update the resolution display with current multiplier and frequency spacing"""
        try:
            multiplier = self.slider_to_multiplier(self.num_scans_slider.get())
            total_points = multiplier * 101
            
            start = int(self.start_freq.get())
            stop = int(self.stop_freq.get())
            bandwidth = stop - start
            
            if bandwidth > 0 and total_points > 0:
                spacing_hz = bandwidth / total_points
                
                # Format spacing nicely
                if spacing_hz >= 1e6:
                    spacing_str = f"{spacing_hz/1e6:.2f} MHz"
                elif spacing_hz >= 1e3:
                    spacing_str = f"{spacing_hz/1e3:.2f} kHz"
                else:
                    spacing_str = f"{spacing_hz:.1f} Hz"
                
                self.resolution_display.configure(
                    text=f"Multiplier: {multiplier} ({total_points} pts, spacing: {spacing_str})"
                )
            else:
                self.resolution_display.configure(
                    text=f"Multiplier: {multiplier} ({total_points} pts, spacing: invalid range)"
                )
        except ValueError:
            self.resolution_display.configure(text="Multiplier: 1 (101 pts, spacing: enter valid frequencies)")
    
    def update_scan_button_text(self):
        """Update the start button text based on time limit and sweep mode"""
        try:
            time_limit = int(self.time_limit.get() or "0")
            if self.sweep_mode.get() == 1:
                self.start_button.configure(text="Start Sweep Scan")
            elif time_limit > 0:
                self.start_button.configure(text=f"Scan for {time_limit} seconds")
            else:
                self.start_button.configure(text="Start Continuous Scan")
        except ValueError:
            self.start_button.configure(text="Start Continuous Scan")
    
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
        self.ax.clear()
        self.ax.set_xlabel("Frequency (MHz)")
        self.ax.set_ylabel("Magnitude (dB)")
        self.ax.set_title("S-Parameter Data")
        self.ax.grid(True, alpha=0.3)
        self.canvas.draw()
        self.stats_label.configure(text="Points: 0")
        self.log("Data cleared")
    
    def update_plot(self):
        """Update plot with current data (stub for now)"""
        # Will be implemented when we add scan data collection
        pass
    
    def detect_vnas(self):
        """Auto-detect connected VNA devices"""
        ports = sorted(glob.glob("/dev/ttyACM*"))
        
        if not ports:
            self.log("No VNA devices found on /dev/ttyACM*")
            return
        
        self.num_vnas.delete(0, "end")
        self.num_vnas.insert(0, str(len(ports)))
        
        self.ports_text.delete("1.0", "end")
        self.ports_text.insert("1.0", "\n".join(ports))
        
        self.log(f"Detected {len(ports)} VNA device(s): {', '.join(ports)}")
    
    def clear_data(self):
        """Clear all scan data"""
        self.ax.clear()
        self.ax.set_xlabel("Frequency (MHz)")
        self.ax.set_ylabel("Magnitude (dB)")
        self.ax.set_title("S-Parameter Data")
        self.ax.grid(True, alpha=0.3)
        self.canvas.draw()
        self.stats_label.configure(text="Points: 0")
        self.log("Data cleared")
    
    def update_plot(self):
        """Update plot with current data (stub for now)"""
        # Will be implemented when we add scan data collection
        pass
        
    def change_theme(self, theme):
        """Change appearance theme"""
        ctk.set_appearance_mode(theme.lower())

    # ============================================================================  
    # VNA SCAN CONTROL FUNCTIONS - Linking GUI to C Library
    # ============================================================================
    
    def start_scan(self):
        """LINK: Start button → C library async scanning
        
        This function:
        1. Reads parameters from GUI controls
        2. Converts them to C library format 
        3. Calls start_async_scan() with Python callbacks
        4. Updates GUI state (disable start, enable stop)
        """
        if not self.vna_api:
            self.log("ERROR: VNA API not initialized")
            return
        
        try:
            # STEP 1: Read parameters from GUI controls
            start_freq = int(self.start_freq.get())
            stop_freq = int(self.stop_freq.get())
            multiplier = self.slider_to_multiplier(self.num_scans_slider.get())
            num_points = 101 * multiplier  # Convert multiplier to actual points
            
            # STEP 2: Determine sweep mode from GUI
            if self.sweep_mode.get() == 1:  # Checkbox checked = fixed sweeps
                mode = SweepMode.NUM_SWEEPS
                sweeps_time = int(self.num_sweeps.get())
                self.log(f"Starting {sweeps_time} sweep(s) with {num_points} points each")
            else:  # Continuous/time-based mode
                mode = SweepMode.TIME
                time_limit = int(self.time_limit.get() or "0")
                sweeps_time = time_limit if time_limit > 0 else 3600  # Default 1 hour max
                self.log(f"Starting continuous scan for {sweeps_time}s with {num_points} points/sweep")
            
            # STEP 3: Get serial ports from GUI
            ports_text = self.ports_text.get("1.0", "end-1c").strip()
            ports = [p.strip() for p in ports_text.split('\n') if p.strip()]
            
            if not ports:
                self.log("ERROR: No ports specified")
                return
            
            self.log(f"Using ports: {', '.join(ports)}")
            
            # STEP 4: Clear previous data for new scan
            with self.data_lock:
                self.scan_data = []
            
            # STEP 5: Create scan parameters object
            params = ScanParameters(start_freq, stop_freq, num_points, mode, sweeps_time, ports)
            
            # STEP 6: Start async scan with callback links
            success = self.vna_api.start_scan(
                params,
                data_callback=self.on_data_received,    # LINK: C data → Python callback
                status_callback=self.on_status_update,  # LINK: C status → GUI log
                error_callback=self.on_scan_error       # LINK: C errors → GUI error handling
            )
            
            # STEP 7: Update GUI state based on result
            if success:
                self.start_button.configure(state="disabled")  # Disable start during scan
                self.stop_button.configure(state="normal")     # Enable stop button
                # Success message will come from status callback after port opens
            else:
                self.log("✗ Failed to start scan")
                
        except ValueError as e:
            self.log(f"ERROR: Invalid parameter - {e}")
        except Exception as e:
            self.log(f"ERROR: {e}")
    
    def stop_scan(self):
        """LINK: Stop button → C library stop function"""
        if not self.vna_api:
            self.log("ERROR: VNA API not initialized")
            return
        
        # Call C library stop function
        success = self.vna_api.stop_scan()
        
        # Always restore button states 
        self.start_button.configure(state="normal")
        self.stop_button.configure(state="disabled")
        
        if success:
            self.log("✓ Scan stopped")
        else:
            self.log("✗ Error stopping scan")
    
    # ============================================================================
    # CALLBACK FUNCTIONS - C Library → GUI Updates  
    # ============================================================================
    
    def on_data_received(self, datapoint):
        """LINK: C library data callback → GUI plot updates
        
        Called from C library thread for each data point.
        Must be thread-safe!
        """
        # THREAD-SAFE: Store data with lock
        with self.data_lock:
            self.scan_data.append({
                'vna_id': datapoint.vna_id,
                'frequency': datapoint.frequency,
                's11': complex(datapoint.s11_re, datapoint.s11_im),  # Convert to Python complex
                's21': complex(datapoint.s21_re, datapoint.s21_im),
                'sweep_number': datapoint.sweep_number,
                'send_time': datapoint.send_time,
                'receive_time': datapoint.receive_time
            })
        
        # THREAD-SAFE: Schedule GUI update from main thread
        self.root.after_idle(self.update_plot_from_data)
    
    def on_status_update(self, message: str):
        """LINK: C library status → GUI log"""
        self.root.after_idle(lambda: self.log(f"[Status] {message}"))
        
        # Only detect final completion, not intermediate messages
        if "scan completed successfully" in message.lower():
            self.root.after_idle(self.auto_restore_gui_state)
        
        # Detect scan completion and auto-restore GUI state
        if "completed successfully" in message.lower() or "finished" in message.lower():
            self.root.after_idle(self.auto_restore_gui_state)
    
    def on_scan_error(self, error: str):
        """LINK: C library errors → GUI error handling"""
        self.root.after_idle(lambda: self.log(f"[Error] {error}"))
        self.root.after_idle(self.stop_scan)  # Auto-stop on error
    
    def update_plot_from_data(self):
        """LINK: New data → Real-time plot updates
        
        Called from main thread when new data arrives.
        """
        with self.data_lock:
            if not self.scan_data:
                return
            
            # Get recent data (last ~1000 points for performance)
            recent_data = self.scan_data[-1000:] if len(self.scan_data) > 1000 else self.scan_data
        
        # Extract plotting data
        frequencies = [d['frequency'] / 1e6 for d in recent_data]  # Convert Hz → MHz
        
        try:
            # Choose S11 or S21 based on GUI selection
            if self.plot_type.get() == "S11 Magnitude (dB)":
                magnitudes = [20 * np.log10(abs(d['s11'])) if d['s11'] != 0 else -100 
                             for d in recent_data]
            else:  # S21
                magnitudes = [20 * np.log10(abs(d['s21'])) if d['s21'] != 0 else -100 
                             for d in recent_data]
            
            # Update matplotlib plot
            self.ax.clear()
            if frequencies and magnitudes:
                self.ax.plot(frequencies, magnitudes, 'b-', alpha=0.8, linewidth=1)
            
            self.ax.set_xlabel("Frequency (MHz)")
            self.ax.set_ylabel("Magnitude (dB)")
            self.ax.set_title("Real-time S-Parameter Data")
            self.ax.grid(True, alpha=0.3)
            self.canvas.draw()
            
            # Update statistics
            self.stats_label.configure(text=f"Points: {len(self.scan_data)}")
            
        except Exception as e:
            self.log(f"Plot error: {e}")
    
    def auto_restore_gui_state(self):
        """Automatically restore GUI state when scan completes"""
        if self.vna_api and not self.vna_api.is_scanning():
            # Only restore if buttons are actually in scanning state
            if self.start_button.cget("state") == "disabled":
                self.start_button.configure(state="normal")
                self.stop_button.configure(state="disabled")
    
    def sync_scan_state(self):
        """Periodically sync GUI state with C library scanning state"""
        if self.vna_api:
            c_library_scanning = self.vna_api.is_scanning()
            gui_thinks_scanning = (self.start_button.cget("state") == "disabled")
            
            # If states are out of sync, fix them
            if not c_library_scanning and gui_thinks_scanning:
                self.auto_restore_gui_state()
            elif c_library_scanning and not gui_thinks_scanning:
                self.start_button.configure(state="disabled")
                self.stop_button.configure(state="normal")
        
        # Schedule next sync check in 2 seconds
        self.root.after(2000, self.sync_scan_state)

    def run(self):
        """Start the GUI main loop"""
        self.update_resolution_display(1)
        self.root.mainloop()


if __name__ == "__main__":
    app = VNAScannerGUI()
    app.run()
