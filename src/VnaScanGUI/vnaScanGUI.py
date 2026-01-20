import customtkinter as ctk
import subprocess
import threading
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from matplotlib.figure import Figure
import numpy as np
import glob

class VNAScannerGUI:
    def __init__(self):
        # Main window
        self.root = ctk.CTk()
        self.root.title("VNA Scanner - Multi-threaded Control")
        self.root.geometry("1400x1400")
        
        # State
        self.tooltip_window = None  # For help tooltips

        ctk.set_appearance_mode("dark")
        ctk.set_default_color_theme("blue")

        self.setup_ui_structure()

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
        
        ctk.CTkLabel(freq_frame, text="Stop (Hz):").pack(anchor="w", padx=10)
        self.stop_freq = ctk.CTkEntry(freq_frame, placeholder_text="900000000")
        self.stop_freq.insert(0, "900000000")
        self.stop_freq.pack(padx=10, pady=(0, 10), fill="x")
        
        # Scan parameters
        scan_frame = ctk.CTkFrame(self.left_panel)
        scan_frame.pack(pady=10, padx=20, fill="x")

        
        ctk.CTkLabel(scan_frame, text="Scan Parameters", font=("Roboto", 14, "bold")).pack(pady=(10, 5))
        
        # Time limit (0 = continuous)
        self.time_limit_label = ctk.CTkLabel(scan_frame, text="Time Limit (seconds, 0=continuous):")
        self.time_limit_label.pack(anchor="w", padx=10)
        self.time_limit = ctk.CTkEntry(scan_frame, placeholder_text="0")
        self.time_limit.insert(0, "0")
        self.time_limit.pack(padx=10, pady=(0, 10), fill="x")

        ctk.CTkLabel(scan_frame, text="Number of Scans:").pack(anchor="w", padx=10)
        self.num_scans = ctk.CTkEntry(scan_frame, placeholder_text="10")
        self.num_scans.insert(0, "10")
        self.num_scans.pack(padx=10, pady=(0, 10), fill="x")
        
        # Dynamic sweep configuration
        self.sweep_label = ctk.CTkLabel(scan_frame, text="Number of Sweeps:")
        self.sweep_label.pack(anchor="w", padx=10)
        
        self.num_sweeps = ctk.CTkEntry(scan_frame, placeholder_text="1")
        self.num_sweeps.insert(0, "1")
        self.num_sweeps.pack(padx=10, pady=(0, 10), fill="x")

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
        
        # Control buttons
        self.start_button = ctk.CTkButton(
            self.left_panel, 
            text="Start Scan", 
            fg_color=("#16A500", "#14A007"),
            hover_color="#043100",
            font=("Roboto", 14, "bold")
        )
        self.start_button.pack(pady=10, padx=20, fill="x")
        
        self.stop_button = ctk.CTkButton(
            self.left_panel,
            text="Stop Scan",
            fg_color=("#AE0006", "#B80006"),
            hover_color="#4E0003",
            state="disabled",
            font=("Roboto", 14)
        )
        self.stop_button.pack(pady=(0, 10), padx=20, fill="x")
        
        clear_btn = ctk.CTkButton(self.left_panel, text="Clear Data")
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
        
    def change_theme(self, theme):
        """Change appearance theme"""
        ctk.set_appearance_mode(theme.lower())

    def run(self):
        """Start the GUI main loop"""
        self.root.mainloop()

if __name__ == "__main__":
    app = VNAScannerGUI()
    app.run()
