"""
VNA Scanning API - Python wrapper for C library async scanning functions

This module provides a Python interface to the C VNA scanning library using ctypes.
It handles all the complexity of calling C functions with callbacks and converting data.
"""

import ctypes
from ctypes import Structure, POINTER, c_int, c_uint32, c_float, c_double, c_char_p
from enum import IntEnum
import os
from typing import List, Callable, Optional
import threading


class SweepMode(IntEnum):
    """Sweep mode enumeration matching C library values"""
    NUM_SWEEPS = 0
    TIME = 1


class DataPoint(Structure):
    """Python representation of DataPoint struct from C library
    
    Fields:
        vna_id: Which VNA produced this data (0-based)
        frequency: Frequency in Hz
        s11_re: S11 real component
        s11_im: S11 imaginary component
        s21_re: S21 real component
        s21_im: S21 imaginary component
        sweep_number: Which sweep this point belongs to
        send_time: Time command was sent to VNA (seconds since epoch)
        receive_time: Time data was received (seconds since epoch)
    """
    _fields_ = [
        ("vna_id", c_int),
        ("frequency", c_uint32),
        ("s11_re", c_float),
        ("s11_im", c_float),
        ("s21_re", c_float),
        ("s21_im", c_float),
        ("sweep_number", c_int),
        ("send_time", c_double),
        ("receive_time", c_double),
    ]

    def __repr__(self):
        return (f"DataPoint(vna_id={self.vna_id}, freq={self.frequency}Hz, "
                f"S11={self.s11_re:.3f}+{self.s11_im:.3f}j, "
                f"S21={self.s21_re:.3f}+{self.s21_im:.3f}j, "
                f"sweep={self.sweep_number})")


class ScanParameters:
    """Container for VNA scan parameters"""
    def __init__(self, start_freq: int, stop_freq: int, num_points: int,
                 sweep_mode: SweepMode, sweeps_or_time: int, ports: List[str]):
        """
        Initialize scan parameters
        
        Args:
            start_freq: Starting frequency in Hz
            stop_freq: Stopping frequency in Hz
            num_points: Number of data points per sweep
            sweep_mode: SweepMode.NUM_SWEEPS or SweepMode.TIME
            sweeps_or_time: Number of sweeps (if NUM_SWEEPS mode) or time limit in seconds (if TIME mode)
            ports: List of serial port paths, e.g. ["/dev/ttyACM0", "/dev/ttyACM1"]
        """
        self.start_freq = start_freq
        self.stop_freq = stop_freq
        self.num_points = num_points
        self.sweep_mode = sweep_mode
        self.sweeps_or_time = sweeps_or_time
        self.ports = ports

    def __repr__(self):
        mode_str = "NUM_SWEEPS" if self.sweep_mode == SweepMode.NUM_SWEEPS else "TIME"
        return (f"ScanParameters({self.start_freq}Hz-{self.stop_freq}Hz, "
                f"{self.num_points} points, {mode_str} mode, "
                f"value={self.sweeps_or_time}, ports={self.ports})")


class VNAScanAPI:
    """
    Python wrapper for C library VNA scanning functions
    
    Usage:
        api = VNAScanAPI()
        params = ScanParameters(1e6, 3e6, 101, SweepMode.NUM_SWEEPS, 5, ["/dev/ttyACM0"])
        
        def on_data(datapoint):
            print(f"Received: {datapoint}")
        
        def on_status(msg):
            print(f"Status: {msg}")
        
        def on_error(err):
            print(f"Error: {err}")
        
        success = api.start_scan(params, on_data, on_status, on_error)
        if success:
            # Scan is running in background, callbacks will be invoked
            while api.is_scanning():
                time.sleep(0.1)
    """

    def __init__(self, lib_path: Optional[str] = None):
        """
        Initialize the API and load the C shared library
        
        Args:
            lib_path: Path to libVnaScanMultithreaded.so. If None, auto-detects.
        
        Raises:
            OSError: If shared library cannot be found or loaded
        """
        if lib_path is None:
            # Auto-detect library path relative to this file
            lib_path = os.path.join(os.path.dirname(__file__), 
                                   "../VnaScanC/libVnaScanMultithreaded.so")
        
        self.lib_path = os.path.abspath(lib_path)
        self.lib = ctypes.CDLL(self.lib_path)
        
        self._setup_function_signatures()
        self._scan_active = False
        
        # Store callbacks to keep them alive
        self._data_callback_func = None
        self._status_callback_func = None
        self._error_callback_func = None
        
        # Keep ports array alive during scan to prevent garbage collection
        self._ports_array = None

    def _setup_function_signatures(self):
        """Configure ctypes function signatures for C library functions"""
        
        # Define callback function types
        self.DataCallbackType = ctypes.CFUNCTYPE(None, POINTER(DataPoint))
        self.StatusCallbackType = ctypes.CFUNCTYPE(None, c_char_p)
        self.ErrorCallbackType = ctypes.CFUNCTYPE(None, c_char_p)
        
        # start_async_scan signature
        self.lib.start_async_scan.argtypes = [
            c_int,                          # num_vnas
            c_int,                          # nbr_scans
            c_int,                          # start frequency
            c_int,                          # stop frequency
            c_int,                          # sweep_mode
            c_int,                          # sweeps_or_time
            POINTER(c_char_p),              # ports array
            self.DataCallbackType,          # data callback
            self.StatusCallbackType,        # status callback
            self.ErrorCallbackType          # error callback
        ]
        self.lib.start_async_scan.restype = c_int
        
        # stop_async_scan signature
        self.lib.stop_async_scan.argtypes = []
        self.lib.stop_async_scan.restype = c_int
        
        # is_async_scan_active signature
        self.lib.is_async_scan_active.argtypes = []
        self.lib.is_async_scan_active.restype = c_int

    def start_scan(self, params: ScanParameters,
                   data_callback: Callable[[DataPoint], None],
                   status_callback: Optional[Callable[[str], None]] = None,
                   error_callback: Optional[Callable[[str], None]] = None) -> bool:
        """
        Start an asynchronous VNA scan
        
        The scan runs in a background thread. Callbacks are invoked as data arrives.
        Only one scan can run at a time.
        
        Args:
            params: ScanParameters object with scan configuration
            data_callback: Called for each data point received (required)
            status_callback: Called with status messages (optional)
            error_callback: Called if errors occur (optional)
        
        Returns:
            True if scan started successfully, False otherwise
        """
        # Check actual C library state, not just Python flag
        if self.is_scanning():
            if error_callback:
                error_callback("Scan already running")
            return False
        if self._scan_active:
            if error_callback:
                error_callback("Scan already running")
            return False
        
        # Create ctypes callback wrappers that Python keeps alive
        self._data_callback_func = self.DataCallbackType(
            lambda dp_ptr: self._data_callback_wrapper(dp_ptr, data_callback)
        )
        
        self._status_callback_func = None
        if status_callback:
            self._status_callback_func = self.StatusCallbackType(
                lambda msg_ptr: self._status_callback_wrapper(msg_ptr, status_callback)
            )
        
        self._error_callback_func = None
        if error_callback:
            self._error_callback_func = self.ErrorCallbackType(
                lambda err_ptr: self._error_callback_wrapper(err_ptr, error_callback)
            )
        
        try:
            # Convert ports list to ctypes array of strings
            # CRITICAL: Store in instance variable to prevent garbage collection!
            self._ports_array = (c_char_p * len(params.ports))()
            for i, port in enumerate(params.ports):
                if isinstance(port, str):
                    self._ports_array[i] = port.encode('utf-8')
                else:
                    self._ports_array[i] = port
            
            # Call C library function
            result = self.lib.start_async_scan(
                len(params.ports),              # num_vnas
                params.num_points,              # nbr_scans (number of points per sweep)
                params.start_freq,              # start frequency
                params.stop_freq,               # stop frequency
                params.sweep_mode.value,        # sweep mode
                params.sweeps_or_time,          # sweeps or time
                self._ports_array,              # ports array - kept alive by instance variable
                self._data_callback_func,       # data callback
                self._status_callback_func,     # status callback (can be NULL)
                self._error_callback_func       # error callback (can be NULL)
            )
            
            if result == 0:
                self._scan_active = True
                return True
            else:
                if error_callback:
                    error_callback("Failed to start scan (C library returned error)")
                return False
                
        except Exception as e:
            if error_callback:
                error_callback(f"Exception starting scan: {str(e)}")
            return False

    def stop_scan(self) -> bool:
        """
        Stop the currently running async scan
        
        Returns:
            True if successful, False otherwise
        """
        if not self._scan_active:
            return True  # Not running, nothing to stop
        
        try:
            result = self.lib.stop_async_scan()
            self._scan_active = False
            
            # Clear references to allow garbage collection after scan stops
            self._ports_array = None
            
            return result == 0
        except Exception:
            self._scan_active = False
            # Clear references even on exception
            self._ports_array = None
            return False

    def is_scanning(self) -> bool:
        """
        Check if a scan is currently running
        
        Returns:
            True if scan is active, False otherwise
        """
        try:
            # Always check actual C library state
            c_is_scanning = bool(self.lib.is_async_scan_active())
            
            # Sync Python flag with C state
            self._scan_active = c_is_scanning
            
            return c_is_scanning
        except Exception:
            return False

    @staticmethod
    def _data_callback_wrapper(datapoint_ptr, python_callback):
        """Internal wrapper for data callback
        
        Converts C DataPoint struct to Python and calls user's callback.
        """
        if datapoint_ptr:
            try:
                datapoint = datapoint_ptr.contents
                python_callback(datapoint)
            except Exception as e:
                print(f"Error in data callback: {e}")

    @staticmethod
    def _status_callback_wrapper(message_ptr, python_callback):
        """Internal wrapper for status callback
        
        Decodes C string and calls user's callback.
        """
        if message_ptr:
            try:
                message = message_ptr.decode('utf-8')
                python_callback(message)
            except Exception as e:
                print(f"Error in status callback: {e}")

    @staticmethod
    def _error_callback_wrapper(error_ptr, python_callback):
        """Internal wrapper for error callback
        
        Decodes C string and calls user's callback.
        """
        if error_ptr:
            try:
                error = error_ptr.decode('utf-8')
                python_callback(error)
            except Exception as e:
                print(f"Error in error callback: {e}")