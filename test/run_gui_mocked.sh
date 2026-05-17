#!/bin/bash

echo "____ Starting Virtual Serial Ports ____"
socat -d -d pty,raw,echo=0,link=/tmp/vna0_source pty,raw,echo=0,link=/tmp/sim_ttyACM0 2>&1 &
socat -d -d pty,raw,echo=0,link=/tmp/vna1_source pty,raw,echo=0,link=/tmp/sim_ttyACM1 2>&1 &
sleep 1
    
echo "____ Starting Emulators ____"
python3 nanovna_emulator.py /tmp/vna0_source --delay 0.001 &
python3 nanovna_emulator.py /tmp/vna1_source --delay 0.001 &
sleep 1 

echo "____ Running GUI ____"
python3 ../src/VnaScanGUI/vna_scan_gui.py
