
import argparse
import serial
import struct
import time
import sys
import random

class NanoVNAEmulator:
    def __init__(self, port, baud=115200, delay_per_point=0.008):
        self.port = port
        self.baud = baud
        self.delay_per_point = delay_per_point
        self.ser = None
        self.malform = False
        
    def open(self):
        """Open the serial port"""
        try:
            self.ser = serial.Serial(self.port, self.baud, timeout=0.1)
            print(f"Emulator opened {self.port}", file=sys.stderr)
            return True
        except Exception as e:
            print(f"Failed to open {self.port}: {e}", file=sys.stderr)
            return False
    
    def close(self):
        """Close the serial port"""
        if self.ser and self.ser.is_open:
            self.ser.close()
    
    def generate_datapoint(self, freq):
        """Generate random test S-parameter data"""
        s11_re = random.uniform(-1.0, 1.0)
        s11_im = random.uniform(-1.0, 1.0)
        s21_re = random.uniform(-0.5, 0.5)
        s21_im = random.uniform(-0.5, 0.5)
        return freq, s11_re, s11_im, s21_re, s21_im
    
    def handle_scan_command(self, start, stop, points, mask):
        """
        Handle 'scan' command from client
        Sends binary header followed by datapoints
        """
        if self.malform == True:
            print(f"Bad Scan: {start}-{stop} Hz, {points} points, mask={mask}", file=sys.stderr)
            self.malform = False
            # Generate and send bad datapoints
            for i in range(int(points*random.uniform(0,2))):
                freq = random.randint(1,100000000)
                freq, s11_re, s11_im, s21_re, s21_im = self.generate_datapoint(freq)
            
                # Binary conversion
                pkt = struct.pack('<Iffff', freq, s11_re, s11_im, s21_re, s21_im)
                self.ser.write(pkt)
                self.ser.flush()
                
                # Add delay for accurate speed
                time.sleep(self.delay_per_point)
        else:        
            print(f"Scan: {start}-{stop} Hz, {points} points, mask={mask}", file=sys.stderr)
            
            # Send binary header (mask:uint16, points:uint16)
            header = struct.pack('<HH', mask & 0xffff, points & 0xffff)
            self.ser.write(header)
            self.ser.flush()
            
            # Generate and send datapoints
            for i in range(points):
                # Calculate frequency for this point
                freq = int(start + (stop - start) * i / max(1, points - 1))
                
                freq, s11_re, s11_im, s21_re, s21_im = self.generate_datapoint(freq)
            
                # Binary conversion
                pkt = struct.pack('<Iffff', freq, s11_re, s11_im, s21_re, s21_im)
                self.ser.write(pkt)
                self.ser.flush()
                
                # Add delay for accurate speed
                time.sleep(self.delay_per_point)
    
    def handle_info_command(self):
        """Handle 'info' command - returns device info"""
        info = (
            "2-4GHz 4inch\r\n"
            "Board: NANOVNA_STM32_F303\r\n"
            "2019-2020 Copyright @edy555\r\n"
            "Licensed under GPL. See: https://github.com/ttrftech/NanoVNA\r\n"
            "ch> "
        )
        self.ser.write(info.encode('ascii'))
        self.ser.flush()
    
    def handle_version_command(self):
        """Handle 'version' command"""
        version = "NanoVNA-H v1.0-TEST-EMULATOR\r\nch> "
        self.ser.write(version.encode('ascii'))
        self.ser.flush()

    def handle_malform_command(self):
        # this command causes the next scan to print bogus data
        self.malform = True
    
    def run(self):
        """Main emulator loop"""
        if not self.open():
            return
        
        buffer = b''
        print("Emulator running, waiting for commands...", file=sys.stderr)
        
        try:
            while True:
                # Read available data
                if self.ser.in_waiting > 0:
                    chunk = self.ser.read(self.ser.in_waiting)
                    buffer += chunk
                
                # Process complete commands
                while b'\r' in buffer:
                    line, buffer = buffer.split(b'\r', 1)
                    line = line.strip()
                    
                    if not line:
                        continue
                    
                    try:
                        cmd_str = line.decode('ascii', errors='ignore')
                        parts = cmd_str.split()
                        
                        if not parts:
                            continue
                        
                        command = parts[0].lower()
                        
                        if command == 'scan' and len(parts) >= 5:
                            start = int(parts[1])
                            stop = int(parts[2])
                            points = int(parts[3])
                            mask = int(parts[4])
                            self.handle_scan_command(start, stop, points, mask)
                            
                        elif command == 'info':
                            self.handle_info_command()
                            
                        elif command == 'version':
                            self.handle_version_command()
                        
                        elif command == 'malform':
                            self.handle_malform_command()
                            
                        else:
                            # Unknown command
                            self.ser.write(b"ch> ")
                            self.ser.flush()
                    
                    except (ValueError, IndexError) as e:
                        print(f"Error parsing command: {e}", file=sys.stderr)
                        self.ser.write(b"ch> ")
                        self.ser.flush()
                
                time.sleep(0.001)  # Small sleep to prevent CPU overload
                
        except KeyboardInterrupt:
            print("\nEmulator stopped by user", file=sys.stderr)
        except Exception as e:
            print(f"Emulator error: {e}", file=sys.stderr)
        finally:
            self.close()

def main():
    parser = argparse.ArgumentParser(description='NanoVNA Device Emulator')
    parser.add_argument('port', help='Serial device path (e.g. /dev/pts/5)')
    parser.add_argument('--baud', type=int, default=115200, help='Baud rate (default: 115200)')
    parser.add_argument('--delay', type=float, default=0.001, 
                       help='Delay per datapoint in seconds (default: 0.001)')
    
    args = parser.parse_args()
    
    emulator = NanoVNAEmulator(args.port, args.baud, args.delay)
    emulator.run()

if __name__ == '__main__':
    main()
