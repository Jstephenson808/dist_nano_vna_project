import ctypes
import os

# Load the shared library
lib_path = os.path.abspath(os.path.join(os.path.dirname(__file__), "../VnaScanC/libVnaScanMultithreaded.so"))
lib = ctypes.CDLL(lib_path)

# Define the DataPoint struct in Python
class DataPoint(ctypes.Structure):
    _fields_ = [
        ("vna_id", ctypes.c_int),
        ("frequency", ctypes.c_uint32),
        ("s11_re", ctypes.c_float),
        ("s11_im", ctypes.c_float),
        ("s21_re", ctypes.c_float),
        ("s21_im", ctypes.c_float),
        ("sweep_number", ctypes.c_int),
        ("send_time", ctypes.c_double),
        ("receive_time", ctypes.c_double),
    ]

# Set the argument and return types for the function
lib.fill_test_datapoint.argtypes = [ctypes.POINTER(DataPoint)]
lib.fill_test_datapoint.restype = ctypes.c_int

# Create a DataPoint instance and call the function
dp = DataPoint()
result = lib.fill_test_datapoint(ctypes.byref(dp))

print("Result:", result)
print("DataPoint:", dp.vna_id, dp.frequency, dp.s11_re, dp.s11_im, dp.s21_re, dp.s21_im, dp.sweep_number, dp.send_time, dp.receive_time)