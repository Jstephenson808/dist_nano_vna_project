#include "VnaScanMultithreaded.h"

void help() {
    printf("        scan: starts a scan with current settings\n\
        exit: safely exits the program\n\
        help: prints a list of all available commands\n");
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
        scanf("%s", buff);
        if (strcmp(buff,"scan") == 0) {
            run_multithreaded_scan(nbr_nanoVNAs, nbr_scans, start_freq, stop_freq, sweep_mode, sweeps, ports);
        }
        else if (strcmp(buff,"exit") == 0) {
            stop = 1;
        }
        else if (strcmp(buff,"help") == 0) {
            help();
        }
    }
    return 0;
}