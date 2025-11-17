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
├── Basic-Scanner/                    # Main project directory
│   ├── vnaScanMultithreaded.c        # Main multithreaded scanner implementation
│   ├── vnaScanMultithreaded.h        # Data structures and function declarations
│   ├── Makefile                      # Build configuration
│   ├── vnaScan.c                     # Prototype: single-threaded C scanner
│   └── vnaScan.py                    # Prototype: initial Python implementation
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
cd Basic-Scanner
make
```

## Usage

### Main Scanner - Multithreaded Implementation

The primary scanner is `vnaScanMultithreaded`, which supports single or multiple VNA devices:

```bash
cd Basic-Scanner
./vnaScanMultithreaded <start_freq> <stop_freq> <points> <num_vnas>
```

**Examples:**

Single VNA, full frequency sweep:
```bash
./vnaScanMultithreaded 50000000 900000000 101 1
```

Multiple VNAs (parallel scanning):
```bash
./vnaScanMultithreaded 50000000 900000000 101 2
```

Short range test:
```bash
./vnaScanMultithreaded 50000000 100000000 51 1
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
cd Basic-Scanner
make clean
make
```

### Code Structure

**Main Implementation:**
- `vnaScanMultithreaded.c` - Producer-consumer multithreading for parallel VNA operation
- `vnaScanMultithreaded.h` - Data structures, thread coordination, and function declarations

**Prototypes (Development History):**
- `vnaScan.c` - Initial single-threaded C implementation
- `vnaScan.py` - Initial Python prototype

**Architecture:**
- Producer threads: One per VNA device, handles scanning and data acquisition
- Consumer thread: Single thread for processing and 
- Shared buffer: Thread-safe coordination using mutexes and condition variables

## Team
Team JH05 - University of Glasgow  
Level 3 Team Project H (2024/2025)

## License

GNU GPL v3.0

## Acknowledgments

- Original [NanoVNA project](https://github.com/ttrftech/NanoVNA) by ttrftech
- [NanoVNA-H firmware](https://github.com/hugen79/NanoVNA-H9) modifications and improvements
- University of Glasgow School of Computing Science

## Project Status

**Active Development** - Academic project for 2024/2025 session
