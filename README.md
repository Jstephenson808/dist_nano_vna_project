# NanoVNA High-Speed Scanner

**Team JH05 - University of Glasgow**

A data acquisition system for NanoVNA (Vector Network Analyzer) devices, enabling fast automated scanning and measurement capture via USB serial interface.

## Overview

This project provides a CLI app for interfacing with multiple NanoVNA-H devices to perform rapid RF measurements, and a GUI to interact with that CLI App graphically and display read-time graphs. 
See [GUI README](src/VnaScanGUI/README.md) for more information on how to install and use the GUI.

### Key Features

- **Multi-VNA Control** - Can orchestrate sweeps from multiple VNAs at once
- **Programmable Interface** - Simple Command Line Interface can be utilised by other applications, so setting up and collecting sweeps can be automated easily
- **Touchstone File Compatibility** - Can output data to stdout, formatted touchstone files, or both

## Project Structure

```
├── README.md
├── src/                                # Source Code Directory
│   ├── CliApp/                             # CLI App
│   │   ├── Makefile                            # Build configuration
│   │   ├── VnaCommandParser.c                  # Primary driver file with CLI command parser
│   │   ├── VnaCommandParser.h
│   │   ├── VnaCommunication.c                  # Helpful methods for interacting with VNAs
│   │   ├── VnaCommunication.h
│   │   ├── VnaScanMultithreaded.c              # Main multithreaded scanner implementation
│   │   ├── VnaScanMultithreaded.h
│   │   └── VnaScanMultithreadedMain.c          # Alternate driver file with no CLI command parser, takes sweep details as Command Line Arguments
│   ├── VnaScanGUI/                         # Python GUI Application
│   │   ├── README.md
│   │   ├── requirements.txt                    # Packages required for application
│   │   ├── vna_scan_gui.py                     # Main GUI Application
│   │   └── vna_scanner.py                      # Scripts for interacting with CLI command parser
│   └── Experiments/                        # Prototypes Directory
│       ├── VnaScan.c                           # Prototype: single-threaded C scanner
│       └── VnaScan.py                          # Prototype: initial Python implementation
└── test/
    ├── nanovna_emulator.py                 # Python emulator for CI/CD testing
    ├── simulatedTests.sh                   # Bash script for running tests with emulator automatically
    ├── runCommandParser.sh                 # Bash script for running command parser with emulated VNAs more easily
    ├── TestCliApp/
    │   ├── TestVnaCommandParser.c              # Unity tests for CLI command parser
    │   ├── testin.txt                          # Plaintext input for TestVnaCommandParser (to be piped in via standard in)
    │   ├── TestVnaCommunication.c              # Unity tests for VNA methods
    │   └── TestVnaScanMultithreaded.c          # Unity tests for multithreaded scanner
    └── TestVnaScanGUI/
        ├── __init__.py                         
        ├── requirements.txt                    
        └── test_gui_basics.py                  # Python tests for vna_scan_gui.py

```

**Note:** Directories in src/ correspond to high-level project components. These modules communicate though standard interfaces, and should remain loosely coupled.

## Requirements

### Hardware

- NanoVNA-H device (or compatible VNA)

### Software
- **Linux** or **macOS**
- **C Compiler** (Clang or GCC)
- **Python3**
- **socat** and **pyserial** for Python (for testing, not required for general use)
- See [GUI README](src/VnaScanGUI/README.md) for GUI Software requirements

## Installation

### 1. Clone Repository

```bash
git clone https://github.com/Jstephenson808/dist_nano_vna_project.git
cd dist_nano_vna_project
```

### 2. Build C Scanner

```bash
cd src/CliApp
make
```

This will also create and run unit tests, telling you if they pass or fail.

## Usage

### CLI Command Parser

The simplest way to run this project is to use the CLI app, `VnaCommandParser`:

```bash
cd src/CliApp
./VnaCommandParser
```

You can find a list of available commands for this app with the following command:

```bash
./VnaCommandParser
>>> help
```

Or find details about a given command:

```bash
./VnaCommandParser
>>> help <command> [subcommand]
```
e.g.
```bash
./VnaCommandParser
>>> help scan
```
```bash
./VnaCommandParser
>>> help vna add
```

See the [user guide](USERGUIDE.md) for more information and examples.

### Scanner Only

If you do not wish to use the command parser, you can instead use just the scanner (`VnaScanMultithreaded.c`) compiled with a simple main function, `VnaScanMultithreadedMain.c`:

```bash
cd src/CliApp
./VnaScanMultithreaded <start_freq> <stop_freq> <nbr_scans> <sweep_mode> <sweeps> <pps> <nbr_nanoVNAs> [ports]
```

See the [user guide](USERGUIDE.md) for more information and examples.

## Testing

We have a unit testing suite, powered by [ThrowTheSwitch's Unity testing framework](https://github.com/ThrowTheSwitch/Unity).

Our unit tests are contained within the test directory. They can be run individually (after running the Makefile):
```bash
cd test/TestCliApp
./TestVnaScanMultithreaded
./TestVnaCommandParser
./TestVnaCommunication
```
This will ignore some tests as there is no VNA connected. They can also be run with a VNA plugged in:
```bash
./TestVnaScanMultithreaded dev/ttyACM0
```
This will run all tests, although there are a couple that will only work properly with the simulated VNA.

We also have a small bash script that can simulate having a VNA connected for the purposes of testing. 
To run this script you need to ensure that you have the Python modules socat and pyserial, and have run the Makefile.
Then just pass it to bash:
```bash
cd test
bash simulatedTests.sh
```

For debugging purposes, it is also possible to compile executables with debugging sybols readable by programs like gdb.
To do this, compile a debug version of the test / program with make, for example:
```bash
cd src/CliApp
make DebugVnaScanMultithreaded
```
Then you can run the file, or even the simulated tests bash script, with a debugger e.g.
```bash
cd src/CliApp
gdb VnaScanMultithreaded
(gdb) run 50000000 900000000 20 -s 5 101 1 /dev/ttyACM0
```

```bash
cd test
gdb bash simulatedTests.sh
```

## Scan Modes

The scanner supports different mask values for output control:

- **Mask 135** (Recommended): Binary format, S11+S21, with calibration
- **Mask 143**: Binary format, S11+S21, without calibration

You can configure the Mask, if you so wish, by changing the value where it says
```c
#define MASK 135
```
in `VnaScanMultithreaded.h`, then recompiling:
```bash
cd src/CliApp
make clean
make
```

## Development

See the [Contributer's Guide](CONTRIBUTING.md) for contribution guidance.

### Building from Source

```bash
cd src/CliApp
make clean
make
```

### Code Structure

**Main Implementation:**
- `VnaScanMultithreaded.c` - Functions to allow for multiple, multithreaded, multi-VNA scans.
- `VnaScanMultithreaded.h` - Header file, declares data structures and function prototypes.
- `VnaScanMultithreadedMain.c` - Driver file, takes in command line arguments and starts a scan.
- `VnaCommandParser.c` - Driver file, repeatedly takes in user input and executes commands.
- `VnaCommandParser.h` - Header file for above
- `VnaCommunication.c` - Contains many useful functions for interacting with VNAs. Imported by all files dealing with VNAs directly.
- `VnaCommunication.h` - Header file for above

**Prototypes (Development History):**

- `VnaScan.c` - Initial single-threaded C implementation
- `VnaScan.py` - Initial Python prototype

## Team

Team JH05 - University of Glasgow
Level 3 Team Project H (2025/2026)

## License

[GNU GPL v3.0](LICENSE)

## Acknowledgments

- Original [NanoVNA project](https://github.com/ttrftech/NanoVNA) by ttrftech
- [NanoVNA-H firmware](https://github.com/hugen79/NanoVNA-H9) modifications and improvements
- [Unity](https://github.com/ThrowTheSwitch/Unity) by ThrowTheSwitch
- University of Glasgow School of Computing Science

## Project Status

**Active Development** - Academic project for 2025/2026 session
