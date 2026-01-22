#include "VnaCommandParser.h"

// Validation function to check if token is a valid integer
int isValidInt(const char* tok) {
    if (tok == NULL || tok[0] == '\0') {
        return 0;
    }
    
    int i = 0;
    // Handle optional negative sign
    if (tok[0] == '-' || tok[0] == '+') {
        i = 1;
    }
    
    // Check if at least one digit exists after sign
    if (tok[i] == '\0') {
        return 0;
    }
    
    // Check that all remaining characters are digits
    while (tok[i] != '\0') {
        if (!isdigit(tok[i])) {
            return 0;
        }
        i++;
    }
    
    return 1;
}

// Validation function to check if token is a valid long
int isValidLong(const char* tok) {
    if (tok == NULL || tok[0] == '\0') {
        return 0;
    }
    
    int i = 0;
    // Handle optional negative sign
    if (tok[0] == '-' || tok[0] == '+') {
        i = 1;
    }
    
    // Check if at least one digit exists after sign
    if (tok[i] == '\0') {
        return 0;
    }
    
    // Check that all remaining characters are digits
    while (tok[i] != '\0') {
        if (!isdigit(tok[i])) {
            return 0;
        }
        i++;
    }
    
    return 1;
}

// settings
long start;
long stop;
int nbr_scans;
int sweeps;
SweepMode sweep_mode;
int pps;
extern int num_vnas;
extern const char **ports;

void help() {
    char* tok = strtok(NULL, " \n");
    if (tok == NULL) {
        printf("\
    scan: starts a scan with current settings\n\
    exit: safely exits the program\n\
    help: prints a list of all available commands\n\
    set: sets a parameter to a new value\n\
    list: lists the values of the current settings (can be changed using the set setting)\n"
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
    For example: set start 100000000\n");
    }
    else if (strcmp(tok,"list") == 0) {
        printf("Lists the current settings used for the scan.\n");
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
        if (tok == NULL) {
            printf("ERROR: No value provided for start frequency.\n");
            return;
        }
        
        if (!isValidLong(tok)) {
            printf("ERROR: Start frequency must be a number.\n");
            return;
        }
        
        long val = atol(tok);
        
        if (val <= 0) {
            printf("ERROR: Start frequency must be a positive number.\n");
            return;
        }
        if (val < 10000 || val > 1500000000) {
            printf("ERROR: Start frequency must be between 10kHz and 1.5GHz.\n");
            return;
        }
        if (val >= stop) {
            printf("ERROR: Start frequency must be less than stop frequency (%ld Hz).\n", stop);
            return;
        }

        start = val;
        printf("Start frequency set to %ld Hz\n", start);
    }
    else if (strcmp(tok,"stop") == 0) {
        tok = strtok(NULL, " \n");
        if (tok == NULL) {
            printf("ERROR: No value provided for stop frequency.\n");
            return;
        }
        
        if (!isValidLong(tok)) {
            printf("ERROR: Stop frequency must be a number.\n");
            return;
        }

        long val = atol(tok);

        if (val <= 0) {
            printf("ERROR: Stop frequency must be a positive number.\n");
            return;
        }
        if (val < 10000 || val > 1500000000) {
            printf("ERROR: Stop frequency must be between 10kHz and 1.5GHz.\n");
            return;
        }
        if (val <= start) {
            printf("ERROR: Stop frequency must be greater than start frequency (%ld Hz).\n", start);
            return;
        }

        stop = val;
        printf("Stop frequency set to %ld Hz\n", stop);
    }
    else if (strcmp(tok, "scans") == 0) {
        tok = strtok(NULL, " \n");
        if (tok == NULL) {
            printf("ERROR: No value provided for number of scans.\n");
            return;
        }
        
        if (!isValidInt(tok)) {
            printf("ERROR: Number of scans must be a valid integer.\n");
            return;
        }
        
        int val = atoi(tok);

        if (val <= 0) {
            printf("ERROR: Number of scans must be a positive integer.\n");
            return;
        }

        nbr_scans = val;
        printf("Number of scans set to %d\n", nbr_scans);
    }
    else if (strcmp(tok, "sweeps") == 0) {
        tok = strtok(NULL, " \n");
        if (tok == NULL) {
            printf("ERROR: No value provided for number of sweeps.\n");
            return;
        }
        
        if (!isValidInt(tok)) {
            printf("ERROR: Number of sweeps must be a valid integer.\n");
            return;
        }
        
        int val = atoi(tok);

        if (val <= 0) {
            printf("ERROR: Number of sweeps must be a positive integer.\n");
            return;
        }

        sweeps = val;
        printf("Number of sweeps set to %d\n", sweeps);
    }
    else if (strcmp(tok, "points") == 0) {
        tok = strtok(NULL, " \n");
        if (tok == NULL) {
            printf("ERROR: No value provided for points per scan.\n");
            return;
        }
        
        if (!isValidInt(tok)) {
            printf("ERROR: Points per scan must be a valid integer.\n");
            return;
        }

        int val = atoi(tok);

        if (val < 1 || val > 101) {
            printf("ERROR: Points per scan must be between 1 and 101.\n");
            return;
        }

        pps = val;
        printf("Points per scan set to %d\n", pps);

    }
    else {
        printf("Parameter not recognised. Available parameters: start, stop\n");
    }
}


void list() {
   printf("\
    Current settings:\n\
        Start frequency: %ld Hz\n\
        Stop frequency: %ld Hz\n\
        Number of scans: %d\n\
        Number of sweeps: %d\n\
        Points per scan: %d\n\
        Number of VNAs: %d\n", start, stop, nbr_scans, sweeps, pps, num_vnas);
}


void set() {
    char* tok = strtok(NULL, " \n");
    if (tok == NULL) {
        printf("Usage: set [parameter] [value]\n");
        return;
    }
    if (strcmp(tok,"start") == 0) {
        tok = strtok(NULL, " \n");
        if (tok == NULL) {
            printf("ERROR: No value provided for start frequency.\n");
            return;
        }
        
        if (!isValidLong(tok)) {
            printf("ERROR: Start frequency must be a number.\n");
            return;
        }
        
        long val = atol(tok);
        
        if (val <= 0) {
            printf("ERROR: Start frequency must be a positive number.\n");
            return;
        }
        if (val < 10000 || val > 1500000000) {
            printf("ERROR: Start frequency must be between 10kHz and 1.5GHz.\n");
            return;
        }
        if (val >= stop) {
            printf("ERROR: Start frequency must be less than stop frequency (%ld Hz).\n", stop);
            return;
        }

        start = val;
        printf("Start frequency set to %ld Hz\n", start);
    }
    else if (strcmp(tok,"stop") == 0) {
        tok = strtok(NULL, " \n");
        if (tok == NULL) {
            printf("ERROR: No value provided for stop frequency.\n");
            return;
        }
        
        if (!isValidLong(tok)) {
            printf("ERROR: Stop frequency must be a number.\n");
            return;
        }

        long val = atol(tok);

        if (val <= 0) {
            printf("ERROR: Stop frequency must be a positive number.\n");
            return;
        }
        if (val < 10000 || val > 1500000000) {
            printf("ERROR: Stop frequency must be between 10kHz and 1.5GHz.\n");
            return;
        }
        if (val <= start) {
            printf("ERROR: Stop frequency must be greater than start frequency (%ld Hz).\n", start);
            return;
        }

        stop = val;
        printf("Stop frequency set to %ld Hz\n", stop);
    }
    else if (strcmp(tok, "scans") == 0) {
        tok = strtok(NULL, " \n");
        if (tok == NULL) {
            printf("ERROR: No value provided for number of scans.\n");
            return;
        }
        
        if (!isValidInt(tok)) {
            printf("ERROR: Number of scans must be a valid integer.\n");
            return;
        }
        
        int val = atoi(tok);

        if (val <= 0) {
            printf("ERROR: Number of scans must be a positive integer.\n");
            return;
        }

        nbr_scans = val;
        printf("Number of scans set to %d\n", nbr_scans);
    }
    else if (strcmp(tok, "sweeps") == 0) {
        tok = strtok(NULL, " \n");
        if (tok == NULL) {
            printf("ERROR: No value provided for number of sweeps.\n");
            return;
        }
        
        if (!isValidInt(tok)) {
            printf("ERROR: Number of sweeps must be a valid integer.\n");
            return;
        }
        
        int val = atoi(tok);

        if (val <= 0) {
            printf("ERROR: Number of sweeps must be a positive integer.\n");
            return;
        }

        sweeps = val;
        printf("Number of sweeps set to %d\n", sweeps);
    }
    else if (strcmp(tok, "points") == 0) {
        tok = strtok(NULL, " \n");
        if (tok == NULL) {
            printf("ERROR: No value provided for points per scan.\n");
            return;
        }
        
        if (!isValidInt(tok)) {
            printf("ERROR: Points per scan must be a valid integer.\n");
            return;
        }

        int val = atoi(tok);

        if (val < 1 || val > 101) {
            printf("ERROR: Points per scan must be between 1 and 101.\n");
            return;
        }

        pps = val;
        printf("Points per scan set to %d\n", pps);

    }
    else {
        printf("Parameter not recognised. Available parameters: start, stop\n");
    }
}


void list() {
   printf("\
    Current settings:\n\
        Start frequency: %ld Hz\n\
        Stop frequency: %ld Hz\n\
        Number of scans: %d\n\
        Number of sweeps: %d\n\
        Points per scan: %d\n\
        Number of VNAs: %d\n", start, stop, nbr_scans, sweeps, pps, num_vnas);
}


void list_vnas() {
    for (int i = 0; i < num_vnas; i++) {
        printf("    %d. %s\n", i+1, ports[i]);
    }
    char** paths;
    int new = find_vnas(paths);
    if (new > 0) {
        printf("Other serial devices detected:\n");
        for (int i = 0; i < new; i++) {
            printf("    %s\n", paths[i]);
        }
    }
    else {
        printf("No other serial devices detected\n");
    }
}

void vna_commands() {
    char* tok = strtok(NULL, " \n");
    if (strcmp(tok,"add") == 0) {
        add_vna(strtok(NULL, " \n"));
    }
    else if (strcmp(tok,"list") == 0) {
        list_vnas();
    }
    else {
        printf("Usage: vna <add/list> [name]\nSee 'help scan' for more info.\n");
    }
}

int read_command() {
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
    else if (strcmp(tok, "list") == 0) {
        list();
    }
    else if (strcmp(tok,"vna") == 0) {
        vna_commands();
    }
    else {
        printf("Command not recognised. Type 'help' for list of available commands.\n");
    }
    return 0;
}

void initialise_settings() {
    // set to defaults
    start = 50000000;
    stop = 900000000;
    nbr_scans = 5;
    sweeps = 1;
    sweep_mode = NUM_SWEEPS;
    pps = 101;

    const char* default_port = "/dev/ttyACM0";
    initialise_port_array(default_port);
}

#ifndef TESTSUITE
int main() {
    int fin = 0;
    while (fin != 1) {
        printf(">>> ");
        fin = read_command();
    }
    return 0;
}
#endif