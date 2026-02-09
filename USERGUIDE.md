# NanoVNA High-Speed Scanner

User Guide for NanoVNA Scanner. See the [readme](README.md) for further information.

## Installation

### 1.1 Clone Repository

The easiest way to access our app is to clone this repository:

```bash
git clone https://github.com/Jstephenson808/dist_nano_vna_project.git
cd dist_nano_vna_project
```

### 1.2 Install Release

Alternatively, you can install the latest tagged release from [the releases page.](https://github.com/Jstephenson808/dist_nano_vna_project/releases)

Unpack the archive, and open the directory, then follow the following steps.

### 2. Build C Scanner

```bash
cd src/CliApp
make clean
make
```

This will also create and run unit tests, telling you if they pass or fail.

## Usage

### CLI Command Parser

To run the CLI app, ensure your VNA(s) are plugged in then run:
```bash
$ cd src/VnaScanC
$ ./VnaCommandParser
```
This will provide a command prompt. Type 'help' to see a list of all available commands:
```bash
>>> help
```
Output:
```
    exit: safely exits the program
    help: prints a list of all available commands,
          or user guide for specified command
    list: lists the values of the current scan parameters
    scan <command>: scan commands (see 'help scan' for details)
    sweep <command>: sweep commands (see 'help sweep' for details)
    set: sets a parameter to a new value
    vna: executes specified vna command (see 'help vna' for details)
```

You can also append to the help command, as shown below, to get more details on a particular command:
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

## User Guide

Using the CLI app is very simple. Once you have launched the application and plugged in your VNAs, just run
```bash
>>> vna add
```
This will autodetect and connect all VNAs currectly plugged in to your computer. If this command fails to connect your connected VNAs, you may have to find the port at which they are connected and add them manually, e.g.
```bash
vna add /dev/ttyACM0
```

One your VNAs are connected, use the `list` command to see your current settings. Configure them to the scan you wish to use, e.g.
```bash
set start 50000000
set stop 55000000
set res 101
```
This sets the frequency band to 50000000-55000000 Hz, and the resolution to 101 points.

Starting a sweep is then as easy as:
```bash
sweep start 0 1
```
this starts a sweep using VNA 0 and VNA 1 (to find the IDs assigned to your VNAs use the `vna list` command).

Other useful commands include:
```bash
sweep list - lists all sweeps and their current statuses
sweep stop - stops specified sweep (or all sweeps if none specified)
exit - safely stops the program
```

The app can handle up to five sweeps simultaneously, with up to ten VNAs connected.
Your output files (in touchstone format) will be stored in the CliApp directory, as .s2p files.

### Scanner Only

If you do not wish to use the command parser, you can instead use just the scanner (`VnaScanMultithreaded.c`) compiled with a simple main function, `VnaScanMultithreadedMain.c`:

```bash
cd src/CliApp
./VnaScanMultithreaded <start_freq> <stop_freq> <nbr_scans> <sweep_mode> <sweeps> <pps> <nbr_nanoVNAs> [ports]
```

Sweep mode options:
- **-s**: Will run the specified sweep <sweeps> times then stop.
- **-o**: Will run the specified sweep repeatedly for <sweeps> seconds then stop. 

**Examples:**

Single VNA, single 101 point sweep:
```bash
./VnaScanMultithreaded 50000000 900000000 1 -s 1 101 1 /dev/ttyACM0
```

Single VNA, 2020 point sweeps over the range 50000000-900000000 Hz for 60 seconds:
```bash
./VnaScanMultithreaded 50000000 900000000 20 -t 60 101 1 dev/ttyACM0
```

Multiple VNAs (parallel scanning), five 2020 point sweeps each:
```bash
./VnaScanMultithreaded 50000000 900000000 20 -s 5 101 2 dev/ttyACM0 dev/ttyACM1
```

Multiple VNAs (parallel scanning), five 200 point sweeps each, reading 10 points at a time:
```bash
./VnaScanMultithreaded 50000000 900000000 20 -s 5 10 2 dev/ttyACM0 dev/ttyACM1
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

GNU GPL v3.0

## Acknowledgments

- Original [NanoVNA project](https://github.com/ttrftech/NanoVNA) by ttrftech
- [NanoVNA-H firmware](https://github.com/hugen79/NanoVNA-H9) modifications and improvements
- [Unity](https://github.com/ThrowTheSwitch/Unity) by ThrowTheSwitch
- University of Glasgow School of Computing Science

## Project Status

**Active Development** - Academic project for 2025/2026 session
