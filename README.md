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
│   │   └── VnaScanMultithreaded.h          # Data structures and function declarations
│   └── VnaScanPython/
│       └── VnaScan.py                      # Prototype: initial Python implementation
└── test/
    ├── nanovna_emulator.py                 # Python emulator for CI/CD testing
    └── TestVnaScanC/
        └── TestVnaScanMultithreaded.c      # Unity tests for multithreaded scanner
```

**Note:** The prototypes (`vnaScan.c`, `vnaScan.py`) were initial explorations. The actual production implementation is **`vnaScanMultithreaded.c`** and **`vnaScanMultithreaded.h`**.

## Requirements

### Hardware
- NanoVNA-H device (or compatible VNA)
- USB cable for serial connection

### Software
- **Linux** (tested), macOS, or Windows
- **C Compiler** (Clang)

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
./VnaScanMultithreaded <start_freq> <stop_freq> <scans> <sweeps> <num_vnas> [port 1] [port 2] ...
```

**Examples:**

Single VNA, single 101 point sweep:
```bash
./VnaScanMultithreaded 50000000 900000000 1 1 1 dev/ttyACM0
```

Single VNA, five 2020 point sweeps:
```bash
./VnaScanMultithreaded 50000000 900000000 20 5 1 dev/ttyACM0
```

Multiple VNAs (parallel scanning), five 2020 point sweeps each:
```bash
./VnaScanMultithreaded 50000000 900000000 20 5 2 dev/ttyACM0 dev/ttyACM1
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
- University of Glasgow School of Computing Science

## Project Status

**Active Development** - Academic project for 2025/2026 session
