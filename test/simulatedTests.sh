#!/bin/bash

echo "____ Starting Virtual Serial Ports ____"
socat -d -d pty,raw,echo=0,link=/tmp/vna0_master pty,raw,echo=0,link=/tmp/vna0_slave 2>&1 &
socat -d -d pty,raw,echo=0,link=/tmp/vna1_master pty,raw,echo=0,link=/tmp/vna1_slave 2>&1 &
sleep 1
    
echo "____ Starting Emulators ____"
python3 nanovna_emulator.py /tmp/vna0_master --delay 0.001 &
python3 nanovna_emulator.py /tmp/vna1_master --delay 0.001 &
sleep 1 

echo "____ Running Unity Tests ____"
cd TestCliApp
chmod +x TestVnaScanMultithreaded
timeout 120s  ./TestVnaScanMultithreaded /tmp/vna0_slave /tmp/vna1_slave
# this line can be used if you need to run with a debugger
#gdb -ex 'run /tmp/vna0_slave /tmp/vna1_slave' ./TestVnaScanMultithreaded

chmod +x TestVnaCommandParser
timeout 120s ./TestVnaCommandParser /tmp/vna0_slave /tmp/vna1_slave < testin.txt
#gdb -ex 'run /tmp/vna0_slave /tmp/vna1_slave' ./TestVnaCommandParser

chmod +x TestVnaCommunication
timeout 120s ./TestVnaCommunication /tmp/vna0_slave /tmp/vna1_slave 
#gdb -ex 'run /tmp/vna0_slave /tmp/vna1_slave' ./TestVnaCommunication