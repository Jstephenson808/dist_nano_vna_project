#!/usr/bin/env python3
import customtkinter as ctk
import subprocess
import threading

class VNAScannerGUI:
    def __init__(self):
        # Main window
        self.root = ctk.CTk()
        self.root.title("VNA Scanner - Multi-threaded Control")
        self.root.geometry("1400x1400")


    def run(self):
        """Start the GUI main loop"""
        self.root.mainloop()

if __name__ == "__main__":
    app = VNAScannerGUI()
    app.run()
