#!/bin/bash

echo "____ Starting Virtual Serial Ports ____"
socat -d -d pty,raw,echo=0,link=/tmp/vna0_master pty,raw,echo=0,link=/tmp/vna0_slave 2>&1 &
socat -d -d pty,raw,echo=0,link=/tmp/vna1_master pty,raw,echo=0,link=/tmp/vna1_slave 2>&1 &
sleep 1
    
echo "____ Starting Emulators ____"
python3 nanovna_emulator.py /tmp/vna0_master --delay 0.001 &
python3 nanovna_emulator.py /tmp/vna1_master --delay 0.001 &
sleep 1 

echo "____ Running Command Parser ____"
chmod +x VnaCommandParser
./../src/VnaScanC/VnaCommandParser
#gdb ./../src/VnaScanC/VnaCommandParser
