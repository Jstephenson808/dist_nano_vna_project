# NanoVNA High-Speed Scanner

**Team JH05 - University of Glasgow**

A data acquisition system for NanoVNA (Vector Network Analyzer) devices, enabling fast automated scanning and measurement capture via USB serial interface.

## Overview

This project provides a multithreaded C scanner for interfacing with NanoVNA-H devices to perform rapid RF measurements, using binary protocol communication and a producer-consumer multithreading architecture.

### Key Features

- **High-Speed Binary Scanning** - Direct binary protocol communication for maximum throughput
- **Multithreaded Architecture** - Producer-consumer pattern supporting parallel scanning from multiple VNA devices
- **Calibration Support** - On-device calibration application 
- **S-Parameter Capture** - Full S11 and S21 complex data acquisition (frequency, real, imaginary components)

## Project Structure

```
├── README.md
├── src/                                    # Main project directory
│   ├── VnaScanC/                           
│   │   ├── Makefile                        # Build configuration
│   │   ├── VnaScan.c                       # Prototype: single-threaded C scanner
│   │   ├── VnaScanMultithreaded.c          # Main multithreaded scanner implementation
│   │   ├── VnaScanMultithreadedMain.c      # Driver file for above
│   │   ├── VnaCommandParser.c              # Alternative driver file with CLI command parser
│   │   └── VnaScanMultithreaded.h          # Data structures and function declarations
│   └── VnaScanPython/
│       └── VnaScan.py                      # Prototype: initial Python implementation
└── test/
    ├── nanovna_emulator.py                 # Python emulator for CI/CD testing
    ├── simulatedTests.sh                   # Bash file for runnings tests with emulator automatically
    ├── testin.txt                          # Plaintext input for TestVnaCommandParser (to be piped in via standard in)
    └── TestVnaScanC/
        └── TestVnaScanMultithreaded.c      # Unity tests for multithreaded scanner
        └── TestVnaCommandParser.c          # Unity tests for CLI command parser
```

**Note:** The prototypes (`vnaScan.c`, `vnaScan.py`) were initial explorations. The actual production implementation is contained within **`VnaScanMultithreaded.c`**, **`VnaScanMultithreadedMain.c`**, and **`VnaCommandParser.c`**.

## Requirements

### Hardware
- NanoVNA-H device (or compatible VNA)

### Software
- **Linux** (tested), macOS, or Windows
- **C Compiler** (Clang or GCC)
- **Python3**
- **socat** and **pyserial** for Python (for testing, not required for general use) 

## Installation

### 1. Clone Repository

```bash
git clone https://stgit.dcs.gla.ac.uk/team-project-h/2025/jh05/jh05-main.git
cd jh05-main
```

### 2. Build C Scanner

```bash
cd src/VnaScanC
make
```

This will also create and run unit tests for VnaScanMultithreaded, telling you if they pass or fail.

## Usage

### Main Scanner

The primary scanner is `VnaScanMultithreaded`, which supports single or multiple VNA devices:

```bash
cd src/VnaScanC
./VnaScanMultithreaded <start_freq> <stop_freq> <nbr_scans> <sweep_mode> <sweeps> <nbr_nanoVNAs> [port1] [port2] ...
```

**Examples:**

Single VNA, single 101 point sweep:
```bash
./VnaScanMultithreaded 50000000 900000000 1 -s 1 1 dev/ttyACM0
```

Single VNA, five 2020 point sweeps:
```bash
./VnaScanMultithreaded 50000000 900000000 20 -s 5 1 dev/ttyACM0
```

Single VNA, 2020 point sweeps over the range 50000000-900000000 Hz for 60 seconds:
```bash
./VnaScanMultithreaded 50000000 900000000 20 -t 60 1 dev/ttyACM0
```

Multiple VNAs (parallel scanning), five 2020 point sweeps each:
```bash
./VnaScanMultithreaded 50000000 900000000 20 -s 5 2 dev/ttyACM0 dev/ttyACM1
```

### CLI Command Parser

There is an option to use a CLI app to run scans as follows:
```bash
cd src/VnaScanC
./VnaCommandParser
```

You can find a list of available commands for this app with the following command:
```bash
./VnaCommandParser
>>> help
```

Or find details about a given command like so:
```bash
./VnaCommandParser
>>> help <command>
```
e.g.
```bash
./VnaCommandParser
>>> help <scan>
```

## Testing

We have a unit testing suite, powered by [ThrowTheSwitch's Unity testing framework](https://github.com/ThrowTheSwitch/Unity).

Our unit tests are contained within the test directory. They can be run individually as so (after running the Makefile):
```bash
cd test/TestVnaScanC
./TestVnaScanMultithreaded
./TestVnaCommandParser
```
This will ignore some tests as there is no VNA connected. They can also be run with a VNA plugged in as so:
```bash
./TestVnaScanMultithreaded dev/ttyACM0
```
This will run all tests, although there are a couple that will only work properly with the simulated VNA.

We also have a small bash file that can simulate having a VNA connected for the purposes of testing. 
To run this file you need to ensure that you have the Python modules socat and pyserial, and have run the Makefile.
The file is then run like so:
```bash
bash simulatedTests.sh
```

## Scan Modes

The scanner supports different mask values for output control:

- **Mask 135** (Recommended): Binary format, S11+S21, with calibration
- **Mask 143**: Binary format, S11+S21, without calibration
- **Mask 7**: Text format, S11+S21, with calibration (human-readable)

## Documentation

- **[Connection Guide](Basic-Scanner/NanoVNA-H/CONNECTION_GUIDE.md)** - Device setup and troubleshooting
- **[Benchmark Guide](Basic-Scanner/NanoVNA-H/BENCHMARK_README.md)** - Performance testing
- **[Quick Test Guide](Basic-Scanner/NanoVNA-H/QUICK_TEST.md)** - Command reference

## Development

### Building from Source

```bash
cd src/VnaScanC
make clean
make
```

### Code Structure

**Main Implementation:**
- `VnaScanMultithreaded.c` - Functions to allow for multithreaded, multi-VNA scans.
- `VnaScanMultithreadedMain.c` - Driver file, takes in user input and calls relevant functions.
- `VnaScanMultithreaded.h` - Header file, declares data structures and function prototypes.
- `VnaCommandParser.c` - Driver file, repeatedly takes in user input and executes commands.
- `VnaCommandParser.h` - Header file for above

**Prototypes (Development History):**
- `VnaScan.c` - Initial single-threaded C implementation
- `VnaScan.py` - Initial Python prototype

## Team
Team JH05 - University of Glasgow  
Level 3 Team Project H (2025/2026)

## License

GNU GPL v3.0

## Acknowledgments

- Original [NanoVNA project](https://github.com/ttrftech/NanoVNA) by ttrftech
- [NanoVNA-H firmware](https://github.com/hugen79/NanoVNA-H9) modifications and improvements
- [Unity](https://github.com/ThrowTheSwitch/Unity) by ThrowTheSwitch
- University of Glasgow School of Computing Science

## Project Status

**Active Development** - Academic project for 2025/2026 session
