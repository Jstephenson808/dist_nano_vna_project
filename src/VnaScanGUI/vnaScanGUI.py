import customtkinter as ctk
import subprocess
import threading

class VNAScannerGUI:
    def __init__(self):
        # Main window
        self.root = ctk.CTk()
        self.root.title("VNA Scanner - Multi-threaded Control")
        self.root.geometry("1400x1400")

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

    def run(self):
        """Start the GUI main loop"""
        self.root.mainloop()

if __name__ == "__main__":
    app = VNAScannerGUI()
    app.run()
