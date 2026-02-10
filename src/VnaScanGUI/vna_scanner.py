"""
VNA Scanner Python Wrapper

Uses subprocess to communicate with VnaCommandParser for scanning operations.
Parses real-time output to provide data for GUI plotting.
"""

import subprocess
import threading
import queue
import os
import glob
import re
from dataclasses import dataclass
from typing import Callable, Optional, List
import math


@dataclass
class VNADataPoint:
    """Single VNA measurement point"""
    frequency: int      # Hz
    s11_re: float
    s11_im: float
    s21_re: float
    s21_im: float
    vna_id: int
    time_sent: float
    time_recv: float
    
    @property
    def s11_mag_db(self) -> float:
        """S11 magnitude in dB"""
        mag = math.sqrt(self.s11_re**2 + self.s11_im**2)
        return 20 * math.log10(mag) if mag > 0 else -100
    
    @property
    def s21_mag_db(self) -> float:
        """S21 magnitude in dB"""
        mag = math.sqrt(self.s21_re**2 + self.s21_im**2)
        return 20 * math.log10(mag) if mag > 0 else -100
    
    @property
    def s11_phase_deg(self) -> float:
        """S11 phase in degrees"""
        return math.degrees(math.atan2(self.s11_im, self.s11_re))
    
    @property
    def s21_phase_deg(self) -> float:
        """S21 phase in degrees"""
        return math.degrees(math.atan2(self.s21_im, self.s21_re))


class VNAScanner:
    """
    Wrapper for VnaCommandParser C program.
    Manages scanning operations and provides real-time data callbacks.
    """
    
    def __init__(self, parser_path: Optional[str] = None):
        """
        Initialize the VNA Scanner.
        
        Args:
            parser_path: Path to VnaCommandParser executable. 
                        If None, searches in expected locations.
        """
        self.parser_path = parser_path or self._find_parser()
        self.process: Optional[subprocess.Popen] = None
        self.scan_thread: Optional[threading.Thread] = None
        self.data_queue: queue.Queue = queue.Queue()
        self._stop_flag = threading.Event()
        self._is_scanning = False
        self._data_callback: Optional[Callable] = None
        self._status_callback: Optional[Callable] = None
        self._current_touchstone_file: Optional[str] = None
        
    def _find_parser(self) -> str:
        """Find the VnaCommandParser executable"""
        # Check relative paths from GUI directory
        base_dir = os.path.dirname(os.path.abspath(__file__))
        possible_paths = [
            os.path.join(base_dir, "..", "CliApp", "VnaCommandParser"),
            os.path.join(base_dir, "..", "..", "src", "CliApp", "VnaCommandParser"),
            "/home/jonasl/Desktop/temp/jh05-main/src/CliApp/VnaCommandParser",
        ]
        
        for path in possible_paths:
            if os.path.isfile(path) and os.access(path, os.X_OK):
                return os.path.abspath(path)
        
        raise FileNotFoundError(
            "VnaCommandParser not found. Please build it with 'make VnaCommandParser' "
            "in the CliApp directory."
        )
    
    @staticmethod
    def detect_vnas() -> List[str]:
        """Detect available VNA devices (ttyACM ports)"""
        return sorted(glob.glob("/dev/ttyACM*"))
    
    @property
    def is_scanning(self) -> bool:
        """Check if a scan is in progress"""
        return self._is_scanning
    
    def start_scan(self, 
                   start_freq: int,
                   stop_freq: int,
                   num_scans: int = 1,
                   num_sweeps: int = 1,
                   points_per_scan: int = 101,
                   time_mode: bool = False,
                   time_limit: int = 0,
                   ports: List[str] = None,
                   data_callback: Optional[Callable[[VNADataPoint], None]] = None,
                   status_callback: Optional[Callable[[str], None]] = None) -> bool:
        """
        Start a VNA scan.
        
        Args:
            start_freq: Start frequency in Hz
            stop_freq: Stop frequency in Hz
            num_scans: Number of scans (resolution multiplier)
            num_sweeps: Number of sweeps (or time in seconds if time_mode)
            points_per_scan: Points per individual scan (max 101)
            time_mode: If True, use time-based scanning
            time_limit: Time limit in seconds (0 = continuous)
            ports: List of VNA port paths
            data_callback: Called for each data point received
            status_callback: Called for status updates
            
        Returns:
            True if scan started successfully
        """
        if self._is_scanning:
            if status_callback:
                status_callback("Scan already in progress")
            return False
        
        if not ports:
            ports = self.detect_vnas()
            if not ports:
                if status_callback:
                    status_callback("No VNA devices found")
                return False
        
        self._data_callback = data_callback
        self._status_callback = status_callback
        self._stop_flag.clear()
        
        # Start scan in background thread
        self.scan_thread = threading.Thread(
            target=self._run_scan,
            args=(start_freq, stop_freq, num_scans, num_sweeps, 
                  points_per_scan, time_mode, time_limit, ports),
            daemon=True
        )
        self.scan_thread.start()
        return True
    
    def _run_scan(self, start_freq, stop_freq, num_scans, num_sweeps,
                  points_per_scan, time_mode, time_limit, ports):
        """Run the scan in a background thread"""
        self._is_scanning = True
        
        try:
            # Run normal scan (fixed number of sweeps or time limit)
            self._run_normal_scan(start_freq, stop_freq, num_scans, num_sweeps,
                                points_per_scan, time_mode, time_limit, ports)
                
        except Exception as e:
            if self._status_callback:
                self._status_callback(f"Scan error: {e}")
        finally:
            self._is_scanning = False
            if self._status_callback:
                self._status_callback("Scan complete")

    def _run_normal_scan(self, start_freq, stop_freq, num_scans, num_sweeps,
                        points_per_scan, time_mode, time_limit, ports):
        """Run normal scan (fixed sweeps or time limit)"""
        # Build commands to send to the parser
        commands = []
        
        # Set parameters
        commands.append(f"set start {start_freq}")
        commands.append(f"set stop {stop_freq}")
        commands.append(f"set scans {num_scans}")
        commands.append(f"set points {points_per_scan}")
        commands.append(f"set verbose true")
        
        if time_mode and time_limit > 0:
            commands.append(f"set sweeps {time_limit}")
        else:
            commands.append(f"set sweeps {num_sweeps}")
        
        # Add VNAs
        for port in ports:
            commands.append(f"vna add {port}")
        
        # Start scan
        if time_mode and time_limit > 0:
            commands.append("scan time")
        else:
            commands.append("scan sweeps")
        
        # Exit after scan
        commands.append("exit")
        
        # Join commands
        command_input = "\n".join(commands) + "\n"
        
        if self._status_callback:
            self._status_callback(f"Starting scan: {start_freq/1e6:.1f} - {stop_freq/1e6:.1f} MHz")
        
        # Execute the scan
        self._execute_scan_commands(command_input)

    def _execute_scan_commands(self, command_input):
        """Execute scan commands with the VnaCommandParser"""
        try:
            # Start the process
            self.process = subprocess.Popen(
                [self.parser_path],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                bufsize=1  # Line buffered
            )
            
            # Send all commands
            self.process.stdin.write(command_input)
            self.process.stdin.flush()
            
            # Parse output in real-time
            self._parse_output()
            
            # Wait for process to complete and get return code
            return_code = self.process.wait()
            
            # Read any remaining stderr
            stderr_output = self.process.stderr.read()
            if stderr_output and self._status_callback:
                self._status_callback(f"Process warnings: {stderr_output.strip()}")
                
        except Exception as e:
            if self._status_callback:
                self._status_callback(f"Scan execution error: {str(e)}")
    
    def _parse_output(self):
        """Parse the VnaCommandParser output in real-time"""
        # Output format from scan_consumer:
        # ID Label VNA TimeSent TimeRecv Freq SParam Format Value
        # Example: 20260127_143052 InteractiveMode 0 0.001234 0.052345 50000000 S11 REAL 0.123456
        
        current_point = {}
        header_seen = False
        data_points_received = 0
        
        while self.process and self.process.poll() is None:
            if self._stop_flag.is_set():
                break
                
            line = self.process.stdout.readline()
            if not line:
                break
            
            line = line.strip()
            
            # Skip prompts and empty lines
            if line.startswith(">>>") or not line:
                continue
            
            # Skip the header line
            if line.startswith("ID Label VNA"):
                header_seen = True
                if self._status_callback:
                    self._status_callback("Data header received, starting data collection...")
                continue
            
            # Skip info messages
            if line.startswith("Saving data to:") or line.startswith("---"):
                if self._status_callback and "Saving" in line:
                    # Extract touchstone filename
                    match = re.search(r'Saving data to: (.+)', line)
                    if match:
                        self._current_touchstone_file = match.group(1)
                        self._status_callback(f"Saving to: {self._current_touchstone_file}")
                continue
            
            # Check for error messages
            if "ERROR" in line.upper() or "Error" in line:
                if self._status_callback:
                    self._status_callback(f"Scanner error: {line}")
                continue
            
            # Parse data lines
            if header_seen:
                parts = line.split()
                if len(parts) >= 8:
                    try:
                        # Parse: ID Label VNA TimeSent TimeRecv Freq SParam Format Value
                        vna_id = int(parts[2])
                        time_sent = float(parts[3])
                        time_recv = float(parts[4])
                        freq = int(parts[5])
                        sparam = parts[6]  # S11 or S21
                        fmt = parts[7]     # REAL or IMG
                        value = float(parts[8])
                        
                        # Build composite key for this frequency point
                        key = (freq, vna_id, time_sent)
                        
                        if key not in current_point:
                            current_point[key] = {
                                'frequency': freq,
                                'vna_id': vna_id,
                                'time_sent': time_sent,
                                'time_recv': time_recv,
                                's11_re': 0, 's11_im': 0,
                                's21_re': 0, 's21_im': 0
                            }
                        
                        # Store the value
                        if sparam == "S11" and fmt == "REAL":
                            current_point[key]['s11_re'] = value
                        elif sparam == "S11" and fmt == "IMG":
                            current_point[key]['s11_im'] = value
                        elif sparam == "S21" and fmt == "REAL":
                            current_point[key]['s21_re'] = value
                        elif sparam == "S21" and fmt == "IMG":
                            current_point[key]['s21_im'] = value
                            # S21 IMG is the last value for a point, emit it
                            p = current_point[key]
                            data_point = VNADataPoint(
                                frequency=p['frequency'],
                                s11_re=p['s11_re'],
                                s11_im=p['s11_im'],
                                s21_re=p['s21_re'],
                                s21_im=p['s21_im'],
                                vna_id=p['vna_id'],
                                time_sent=p['time_sent'],
                                time_recv=p['time_recv']
                            )
                            
                            data_points_received += 1
                            if data_points_received % 10 == 0 and self._status_callback:
                                self._status_callback(f"Received {data_points_received} data points...")
                            
                            if self._data_callback:
                                self._data_callback(data_point)
                            
                            # Clean up
                            del current_point[key]
                            
                    except (ValueError, IndexError) as e:
                        # Skip malformed lines
                        continue
        
        # Read any remaining output
        if self.process:
            remaining = self.process.stdout.read()
            stderr = self.process.stderr.read()
            if stderr and self._status_callback:
                self._status_callback(f"Process warnings: {stderr.strip()}")
        
        if self._status_callback:
            self._status_callback(f"Data collection complete. Total points: {data_points_received}")
    
    def stop_scan(self):
        """Stop the current scan"""
        self._stop_flag.set()
        
        if self.process:
            try:
                self.process.terminate()
                self.process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.process.kill()
            self.process = None
        
        self._is_scanning = False
        
        if self._status_callback:
            self._status_callback("Scan stopped by user")
    
    def get_touchstone_file(self) -> Optional[str]:
        """Get the path to the last generated touchstone file"""
        return self._current_touchstone_file
    
    @staticmethod
    def read_touchstone(filepath: str) -> List[VNADataPoint]:
        """
        Read a touchstone (.s2p) file and return data points.
        
        Args:
            filepath: Path to the .s2p file
            
        Returns:
            List of VNADataPoint objects
        """
        points = []
        
        with open(filepath, 'r') as f:
            for line in f:
                line = line.strip()
                
                # Skip comments and option line
                if line.startswith('!') or line.startswith('#') or not line:
                    continue
                
                parts = line.split()
                if len(parts) >= 5:
                    try:
                        freq = int(float(parts[0]))
                        s11_re = float(parts[1])
                        s11_im = float(parts[2])
                        s21_re = float(parts[3])
                        s21_im = float(parts[4])
                        
                        points.append(VNADataPoint(
                            frequency=freq,
                            s11_re=s11_re,
                            s11_im=s11_im,
                            s21_re=s21_re,
                            s21_im=s21_im,
                            vna_id=0,
                            time_sent=0,
                            time_recv=0
                        ))
                    except ValueError:
                        continue
        
        return points


# Simple test
if __name__ == "__main__":
    scanner = VNAScanner()
    print(f"Parser found at: {scanner.parser_path}")
    print(f"Detected VNAs: {scanner.detect_vnas()}")
