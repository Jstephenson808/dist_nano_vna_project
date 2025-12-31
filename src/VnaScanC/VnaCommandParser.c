#include "VnaScanMultithreaded.h"
#include <string.h>

// defaults
long start = 50000000;
long stop = 900000000;
int nbr_scans = 5;
int sweeps = 1;
SweepMode sweep_mode = NUM_SWEEPS;
int num_vnas = 1;
const char* default_port = "/dev/ttyACM0";
const char **ports = (const char **)&default_port;

void help() {
    char* tok = strtok(NULL, " \n");
    if (tok == NULL) {
        printf("\
    scan: starts a scan with current settings\n\
    exit: safely exits the program\n\
    help: prints a list of all available commands\n"
        );
    }
    else if (strcmp(tok,"scan") == 0) {
        printf("\
    Starts a scan with current settings. Options:\n\
    scan sweeps - runs a certain number of sweeps (default)  \n\
    scan time - runs sweeps continuosly until specified time elapsed\n"
        );
    }
}

void scan() {
    char* tok = strtok(NULL, " \n");
    if (tok == NULL || (strcmp(tok,"sweeps") == 0)) {
        sweep_mode = NUM_SWEEPS;
        run_multithreaded_scan(num_vnas, nbr_scans, start, stop, sweep_mode, sweeps, ports);
    }
    else if (strcmp(tok,"time") == 0) {
        sweep_mode = TIME;
        run_multithreaded_scan(num_vnas, nbr_scans, start, stop, sweep_mode, sweeps, ports);
    }
}

int readCommand() {
    char buff[50];
    fgets(buff, sizeof(buff), stdin);

    char* tok = strtok(buff, " \n");

    if (tok == NULL) {
        printf("bad read");
    }
    else if (strcmp(tok,"scan") == 0) {
        scan();
    }
    else if (strcmp(tok,"exit") == 0) {
        return 1;
    }
    else if (strcmp(tok,"help") == 0) {
        help();
    }
    return 0;
}

#ifndef TESTSUITE
int main() {
    int fin = 0;
    while (fin != 1) {
        printf(">>> ");
        fin = readCommand();
    }
    return 0;
}
#endif