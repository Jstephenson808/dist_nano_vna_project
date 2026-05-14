Here is a clean **step by step README.md** you can use for your setup.

***

# 🧩 Using COM / Serial Devices in WSL2 (usbipd)

This guide explains how to attach and use single or multiple USB serial devices (COM ports) inside WSL2 using `usbipd`.

***

## ✅ Prerequisites

*   Windows 10/11 with WSL2 installed
*   Ubuntu (or another distro) running in WSL2
*   `usbipd` installed on Windows

Install `usbipd` if needed:

```powershell
winget install usbipd
```

***

## 🔌 Step 1: List available USB devices (Windows)

Open PowerShell:

```powershell
usbipd list
```

Example output:

    BUSID  VID:PID    DEVICE
    2-1    2341:0043  Arduino Uno
    2-2    10C4:EA60  CP210x UART

***

## 🔗 Step 2: Bind and attach devices to WSL

### Bind each device (once)

```powershell
usbipd bind --busid 2-1
usbipd bind --busid 2-2
```

### Attach to WSL

```powershell
usbipd attach --wsl --busid 2-1
usbipd attach --wsl --busid 2-2
```

***

## 🔍 Step 3: Verify inside WSL

Open Ubuntu:

```bash
dmesg | tail
```

You should see something like:

    cdc_acm 1-1:1.0: ttyACM0: USB ACM device

***

## 🔎 Step 4: Identify device paths

### IMPORTANT

Do **not assume** devices will appear as `/dev/ttyUSB*`.

There are two main types:

| Device Type                            | Appears As     |
| -------------------------------------- | -------------- |
| CDC ACM (Arduino, dev boards)          | `/dev/ttyACM0` |
| USB serial chips (FTDI, CH340, CP210x) | `/dev/ttyUSB0` |

***

### Check both types:

```bash
ls /dev/ttyACM*
ls /dev/ttyUSB*
```

***

## 🧠 Step 5: Use stable device names (recommended)

Instead of relying on:

    /dev/ttyACM0
    /dev/ttyUSB0

Use persistent paths:

```bash
ls /dev/serial/by-id/
```

Example:

    usb-Arduino_12345-if00 -> ../../ttyACM0
    usb-CP2102_ABC-if00-port0 -> ../../ttyUSB0

✅ These names are stable across reconnects

***

## ⚠️ If `/dev/serial/by-id` does not exist

This directory may be missing in WSL.

### Fallback options:

#### Option 1: Use `dmesg`

```bash
dmesg | grep tty
```

#### Option 2: Use `lsusb`

```bash
lsusb
```

Match device order manually with `/dev/ttyACM*` or `/dev/ttyUSB*`.

***

## 🔧 Step 6: Fix permissions

If access is denied:

```bash
sudo chmod 666 /dev/ttyACM0
```

Better long term fix:

```bash
sudo usermod -aG dialout $USER
```

Then restart WSL.

***

## 🔁 Step 7: Reattach after reboot

WSL does not persist USB connections.

After reboot:

```powershell
usbipd attach --wsl --busid 2-1
usbipd attach --wsl --busid 2-2
```

***

## 🧪 Step 8: Test device

Example using `screen`:

```bash
screen /dev/ttyACM0 115200
```

Or:

```bash
minicom -D /dev/ttyACM0
```

***

## 🧩 Optional: Create friendly aliases

```bash
sudo ln -s /dev/ttyACM0 /dev/mydevice1
```

***

## 🚀 Summary

*   Use `usbipd` to attach devices from Windows
*   Devices may appear as `ttyACM` or `ttyUSB`
*   Always verify with `dmesg`
*   Prefer `/dev/serial/by-id` when available
*   Reattach devices after reboot

***

## ✅ Example workflow

### Windows

```powershell
usbipd list
usbipd bind --busid 2-1
usbipd attach --wsl --busid 2-1
```

### WSL

```bash
dmesg | tail
ls /dev/ttyACM*
```


