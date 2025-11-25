import serial
import struct

ser = serial.Serial('/dev/ttyACM0', 115200, timeout=5)

ser.write(b'info\r')
details = ""
while "ch>" not in details:
    details += ser.read().decode('utf-8')
print(details)


# Send scan command
ser.write(b'scan 50000000 900000000 101 135\r')

# Read binary data
header = ser.read(4)
mask, points = struct.unpack('<HH', header)
print(points)

for i in range(points):
    data = ser.read(20)  # freq + 2x complex float
    freq, s11_re, s11_im, s21_re, s21_im = struct.unpack('<Iffff', data)
    print(f"{freq} Hz: S11={s11_re}+{s11_im}j, S21={s21_re}+{s21_im}j")

ser.close()