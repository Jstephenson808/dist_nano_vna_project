#include "VnaScanMultithreaded.h"
#include <string.h>

// defaults
long start = 50000000;
long stop = 900000000;
int nbr_scans = 5;
int sweeps = 1;
SweepMode sweep_mode = NUM_SWEEPS;
int pps = 101;
int num_vnas = 1;
const char* default_port = "/dev/ttyACM0";
const char **ports = (const char **)&default_port;

void help() {
    char* tok = strtok(NULL, " \n");
    if (tok == NULL) {
        printf("\
    scan: starts a scan with current settings\n\
    exit: safely exits the program\n\
    help: prints a list of all available commands\n\
    set: sets a parameter to a new value\n"
        );
    }
    else if (strcmp(tok,"scan") == 0) {
        printf("\
    Starts a scan with current settings. Options:\n\
    scan sweeps - runs a certain number of sweeps (default)  \n\
    scan time - runs sweeps continuosly until specified time elapsed\n"
        );
    }
    else if (strcmp(tok,"set") == 0) {
        printf("\
    Sets a parameter to a new value.\n\
    In the terminal, enter: set [parameter] [value]\n\
    Paramters you can set:\n\
        start - starting frequency\n\
        stop - stopping frequency\n\
        scans - number of scans to compute\n\
        sweeps - number of sweeps to perform\n\
        points - number of points per scan\n\
        vnas - number of VNAs to use\n\
    For example: set start 100000000\n");
    }
    else {
        printf("Usage: help [command]\nFor list of possible commands type 'help'.\n");
    }
}

void scan() {
    char* tok = strtok(NULL, " \n");
    if (tok == NULL || (strcmp(tok,"sweeps") == 0)) {
        sweep_mode = NUM_SWEEPS;
        run_multithreaded_scan(num_vnas, nbr_scans, start, stop, sweep_mode, sweeps, pps, ports);
    }
    else if (strcmp(tok,"time") == 0) {
        sweep_mode = TIME;
        run_multithreaded_scan(num_vnas, nbr_scans, start, stop, sweep_mode, sweeps, pps, ports);
    }
    else {
        printf("Usage: scan [sweep_mode]\nSee 'help scan' for more info.\n");
    }
}

void set() {
    char* tok = strtok(NULL, " \n");
    if (tok == NULL) {
        printf("Usage: set [parameter] [value]\n");
        return;
    }
    if (strcmp(tok,"start") == 0) {
        tok = strtok(NULL, " \n");
        if (tok != NULL) {
            start = atol(tok);
            printf("Start frequency set to %ld Hz\n", start);
        }
    }
    else if (strcmp(tok,"stop") == 0) {
        tok = strtok(NULL, " \n");
        if (tok != NULL) {
            stop = atol(tok);
            printf("Stop frequency set to %ld Hz\n", stop);
        }
    }
    else if (strcmp(tok, "scans") == 0) {
        tok = strtok(NULL, " \n");
        if (tok != NULL) {
            nbr_scans = atoi(tok);
            printf("Number of scans set to %d\n", nbr_scans);
        }
    }
    else if (strcmp(tok, "sweeps") == 0) {
        tok = strtok(NULL, " \n");
        if (tok != NULL) {
            sweeps = atoi(tok);
            printf("Number of sweeps set to %d\n", sweeps);
        }
    }
    else if (strcmp(tok, "points") == 0) {
        tok = strtok(NULL, " \n");
        if (tok != NULL) {
            pps = atoi(tok);
            printf("Points per scan set to %d\n", pps);
        }
    }
    else if (strcmp(tok, "vnas") == 0) {
        tok = strtok(NULL, " \n");
        if (tok != NULL) {
            num_vnas = atoi(tok);
            printf("Number of VNAs set to %d\n", num_vnas);
        }
    }
    else {
        printf("Parameter not recognised. Available parameters: start, stop\n");
    }
}


int readCommand() {
    char buff[50];
    fgets(buff, sizeof(buff), stdin);

    char* tok = strtok(buff, " \n");

    if (tok == NULL) {
        return 0;
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
    else if (strcmp(tok,"set") == 0) {
        set();
    }
    else {
        printf("Command not recognised. Type 'help' for list of available commands.\n");
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