#!/bin/bash

echo "____ Starting Virtual Serial Ports ____"
socat -d -d pty,raw,echo=0,link=/tmp/vna0_source pty,raw,echo=0,link=/tmp/sim_ttyACM0 2>&1 &
socat -d -d pty,raw,echo=0,link=/tmp/vna1_source pty,raw,echo=0,link=/tmp/sim_ttyACM1 2>&1 &
sleep 1
    
echo "____ Starting Emulators ____"
python3 nanovna_emulator.py /tmp/vna0_source --delay 0.001 &
python3 nanovna_emulator.py /tmp/vna1_source --delay 0.001 &
sleep 1 

echo "____ Running Unity Tests ____"
cd TestCliApp
chmod +x TestVnaScanMultithreaded
timeout 120s  ./TestVnaScanMultithreaded /tmp/sim_ttyACM0 /tmp/sim_ttyACM1
# this line can be used if you need to run with a debugger
#gdb -ex 'run /tmp/sim_ttyACM0 /tmp/sim_ttyACM1' ./TestVnaScanMultithreaded

chmod +x TestVnaCommandParser
timeout 120s ./TestVnaCommandParser /tmp/sim_ttyACM0 /tmp/sim_ttyACM1 < testin.txt
#gdb -ex 'run /tmp/sim_ttyACM0 /tmp/sim_ttyACM1' ./TestVnaCommandParser

chmod +x TestVnaCommunication
timeout 120s ./TestVnaCommunication /tmp/sim_ttyACM0 /tmp/sim_ttyACM1 
#gdb -ex 'run /tmp/sim_ttyACM0 /tmp/sim_ttyACM1' ./TestVnaCommunication