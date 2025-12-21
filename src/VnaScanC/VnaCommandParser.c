#include "VnaScanMultithreaded.h"
#include <string.h>

void help() {
    char* tok = strtok(NULL, " \n");
    if (tok == NULL) {
        printf(
"    scan: starts a scan with current settings\n\
    exit: safely exits the program\n\
    help: prints a list of all available commands\n"
        );
    }
    else if (strcmp(tok,"scan") == 0) {
        printf(
"    Starts a scan with current settings. Options:\n\
    scan sweeps - runs a certain number of sweeps (default)  \n\
    scan time - runs sweeps continuosly until specified time elapsed\n"
        );
    }
}

int main() {
    // set defaults
    long start_freq = 50000000;
    long stop_freq = 900000000;
    int nbr_scans = 20;
    int sweeps = 5;
    SweepMode sweep_mode = NUM_SWEEPS;
    int nbr_nanoVNAs = 1;
    const char* default_port = "/dev/ttyACM0";
    const char **ports = (const char **)&default_port;


    int stop = 0;
    while (stop != 1) {
        printf(">>> ");
        char buff[50];
        fgets(buff, sizeof(buff), stdin);

        char* tok = strtok(buff, " \n");

        if (tok == NULL) {
            printf("bad read");
        }
        else if (strcmp(tok,"scan") == 0) {
            tok = strtok(NULL, " \n");
            if (tok == NULL || (strcmp(tok,"sweeps") == 0)) {
                run_multithreaded_scan(nbr_nanoVNAs, nbr_scans, start_freq, stop_freq, sweep_mode, sweeps, ports);
            }
            else if (strcmp(tok,"time") == 0) {
                sweep_mode = TIME;
                run_multithreaded_scan(nbr_nanoVNAs, nbr_scans, start_freq, stop_freq, sweep_mode, sweeps, ports);
                sweep_mode = NUM_SWEEPS;
            }
        }
        else if (strcmp(tok,"exit") == 0) {
            stop = 1;
        }
        else if (strcmp(tok,"help") == 0) {
            help();
        }
    }
    return 0;
}