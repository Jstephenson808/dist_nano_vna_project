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
int resolution;
int nbr_scans;
int pps;
int sweeps;
int time_to_sweep;
bool verbose;

void help() {
    char* tok = strtok(NULL, " \n");
    if (tok == NULL) {
        printf("\
    exit: safely exits the program\n\
    help: prints a list of all available commands,\n\
          or user guide for specified command\n\
    list: lists the values of the current scan parameters\n\
    scan <command>: scan commands (see 'help scan' for details)\n\
    sweep <command>: sweep commands (see 'help sweep' for details)\n\
    set: sets a parameter to a new value\n\
    vna: executes specified vna command (see 'help vna' for details)\n"
        );
    } else if (strcmp(tok,"scan") == 0) {
        tok = strtok(NULL, " \n");
        if (tok == NULL) {
            printf("\
    Starts a scan with current settings. Options:\n\
        scan number [vna ids] - runs a certain number of sweeps (default)  \n\
        scan time [vna ids] - runs sweeps continuosly until specified time elapsed\n"
            );
        } else if (strcmp(tok,"num") == 0 || strcmp(tok,"number") == 0) {
            printf("\
    Starts a scan with current settings that runs until the given frequency\n\
    band has been traversed a certain number of times (sweeps).\n\
    Uses the specified vna ids, or all connected vnas if no vna ids given.\n\
    Usage example:\n\
        scan num 2 3 5\n\
    (starts a scan with VNAs 2, 3, and 5)\n"
            );
        } else if (strcmp(tok,"time") == 0) {
            printf("\
    Starts a scan with current settings that runs for the given amount of\n\
    time in seconds (time_to_sweep).\n\
    Uses the specified vna ids, or all connected vnas if no vna ids given.\n\
    Usage example:\n\
        scan time 2 3 5\n\
    (starts a scan with VNAs 2, 3, and 5)\n"
            );
        } else {
            printf("\
    command not recognised. scan subcommands:\n\
        scan number\n\
        scan time\n\
    see 'help sweep' for more.\n"
            );
        }
    } else if (strcmp(tok,"sweep") == 0) {
        tok = strtok(NULL, " \n");
        if (tok == NULL) {
            printf("\
    Options:\n\
        sweep start [vna ids] - starts sweep with current settings.\n\
                                uses specified VNAs, or all connected\n\
                                VNAs if no VNA IDs specific.\n\
        sweep stop [scan id] -  stops specified sweep, or all sweeps if\n\
                                no scan id specified\n\
        sweep list - lists the status of all available scan IDs\n"
            );
        } else if (strcmp(tok,"start") == 0) {
            printf("\
    Starts sweep with current settings.\n\
    Uses specified VNAs, or all connected VNAs if no VNA IDs specific.\n\
    Example:\n\
        sweep start 0 2 5\n\
    (starts a sweep with VNAs 0, 2, and 5)\n"
            );
        } else if (strcmp(tok,"stop") == 0) {
            printf("\
    Stops the specified sweep, waiting until it has fully concluded before\n\
    returning control to the user.\n\
    To discover active sweeps, use 'sweep list'.\n"
            );
        } else if (strcmp(tok,"list") == 0) {
            printf("\
    Lists all scan ids, and their current states. Possible states:\n\
        'vacant' - no scan is currently assigned to this id\n\
        'idle' - a scan is assigned to this id, but is not currently\n\
        active. You can free the id with 'sweep stop'.\n\
        'busy' - an active scan is using this id.\n"
            );
        } else {
            printf("\
    command not recognised. sweep subcommands:\n\
        sweep start\n\
        sweep stop\n\
        sweep list\n\
    see 'help sweep' for more.\n"
            );
        }
    } else if (strcmp(tok,"set") == 0) {
        printf("\
    Sets a parameter to a new value.\n\
    In the terminal, enter: set [parameter] [value]\n\
    Paramters you can set:\n\
        start - starting frequency\n\
        stop - stopping frequency\n\
        scans - number of scans to compute\n\
        sweeps - number of sweeps to perform\n\
        points - number of points per scan\n\
        verbose - if readings should be printed to stdout\n\
    For example: set start 100000000\n");
    } else if (strcmp(tok,"list") == 0) {
        printf("Lists the current settings used for the scan.\n");
    } else if (strcmp(tok,"vna") == 0) {
        tok = strtok(NULL, " \n");
        if (tok == NULL) {
            printf("\
    Family of commands to manage VNA connections.\n\
    Command options:\n\
        vna add <port> - connects to the specified vna.\n\
        vna remove <port> - disconnects the specified vna.\n\
        vna list - lists connected VNAs and searches /dev directory\n\
        for devices of the format ttyACM*\n\
        vna ping - pings all connected VNAs and checks for a response\n\
        vna id - prints board and version of all connected VNAs\n\
        vna reset - restarts all vnas, closing connections\n");
        } else if (strcmp(tok,"add") == 0) {
            printf("\
    Attempts to connect to the specified VNA device, first checking\n\
    that it is reachable and that it represents a NanoVNA-H device.\n\
    If no port name is given, attempts to connect to any USB-serial\n\
    device connected to your device and check if it is a NanoVNA-H\n\
    Usage example:\n\
        vna add /dev/ttyACM0\n");
        } else if (strcmp(tok,"remove") == 0) {
            printf("\
    Attempts to disconnect the specified VNA device, if it can\n\
    be found in the open connections.\n\
    Usage example:\n\
        vna remove /dev/ttyACM0\n");
        } else if (strcmp(tok,"list") == 0) {
            printf("\
    Lists connected VNAs and searches /dev directory for unlisted\n\
    files of the format ttyACM*, which are then listed.\n\
    Usage example:\n\
        vna list\n");
        } else if (strcmp(tok,"ping") == 0) {
            printf("\
    Pings all connected VNAs and prints 'pong' for those who respond\n\
    Specifies those who do not respond.\n");
        } else if (strcmp(tok,"id") == 0) {
            printf("\
    Prints the board and firmware version of every connected VNA\n\
    in the format:\n\
        <num>. <serial_port> <board> version <version>\n");
        } else if (strcmp(tok,"reset") == 0) {
            printf("\
    Sends the rest command to every VNA and closes their connection\n\
    to this program.\n");
        } else {
            printf("\
    command not recognised. vna subcommands:\n\
        vna add\n\
        vna remove\n\
        vna list\n\
        vna ping\n\
        vna id\n\
        vna reset\n\
    see 'help vna' for more.\n");
        }
    } else if (strcmp(tok,"help") == 0) {
        printf("\
    prints a user guide for the specified command,\n\
    or a list of all available commands.\n\
    Usage example:\n\
        help help\n");
    } else {
        printf("Usage: help [command]\nFor list of possible commands type 'help'.\n");
    }
}

int get_vna_list_from_args(char* tok, int* vnas) {
    int count = 0;
    while (tok != NULL && count < MAXIMUM_VNA_PORTS) {
        if (!isValidInt(tok)) {
            printf("ERROR: vna ids must be valid integers.\n");
            return -1;
        }
        
        int vna_id = atoi(tok);
        if (vna_id < 0 || vna_id >= MAXIMUM_VNA_PORTS) {
            printf("ERROR: vna ids must be between 0 and %d.\n", MAXIMUM_VNA_PORTS);
            return -1;
        }

        if(is_connected(vna_id)) {
            vnas[count] = vna_id;
            count++;
        } else {
            printf("vna %d not connected\n",vna_id);
        }
        tok = strtok(NULL, " \n");
    }
    return count;
}

void scan() {
    const char *interactive_label = "InteractiveMode";
    SweepMode sweep_mode;
    int nbr_sweeps;

    char* tok = strtok(NULL, " \n");
    if (tok == NULL) {
        printf("Usage: scan <sweep_mode> [vna_ids]\nSee 'help scan' for more info.\n");
        return;
    } else if (strcmp(tok,"num") == 0 || strcmp(tok,"number") == 0) {
        sweep_mode = NUM_SWEEPS;
        nbr_sweeps = sweeps;
    } else if (strcmp(tok,"time") == 0) {
        sweep_mode = TIME;
        nbr_sweeps = time_to_sweep;
    } else {
        printf("Usage: scan <sweep_mode> [vna_ids]\nSee 'help scan' for more info.\n");
        return;
    }

    int* vna_list = calloc(sizeof(int),MAXIMUM_VNA_PORTS);
    if (!vna_list) {
        fprintf(stderr, "couldn't assign space for vna ids");
        return;
    }

    tok = strtok(NULL, " \n");
    int nbr_vnas = (tok == NULL ? get_connected_vnas(vna_list) : get_vna_list_from_args(tok,vna_list));
    if (nbr_vnas < 1) {
        printf("%d vnas not enough", nbr_vnas);
        return;
    }

    start_sweep(nbr_vnas, vna_list, nbr_scans, start, stop, sweep_mode, nbr_sweeps, pps, interactive_label, verbose);
}

void sweep() {
    char* tok = strtok(NULL, " \n");
    const char *interactive_label = "InteractiveMode";

    if (strcmp(tok, "stop") == 0) {
        tok = strtok(NULL, " \n");
        if (tok == NULL) {
            for (int i = 0; i < MAX_ONGOING_SCANS; i++) {
                if (is_running(i)) {
                    printf("Stopping sweep %d\n", i);
                    stop_sweep(i);
                }
            }
        } else {
            if (!isValidInt(tok)) {
                printf("ERROR: scan id must be a valid integer.\n");
                return;
            }
            int scan_id = atoi(tok);
            if (scan_id < 0 || scan_id > MAX_ONGOING_SCANS) {
                printf("ERROR: scan id must be between 0 and %d.\n", MAX_ONGOING_SCANS);
                return;
            } else if (!is_running(scan_id)) {
                printf("ERROR: scan %d is not currently running.\n", scan_id);
                return;
            }
            int err = stop_sweep(scan_id);
            if (err != EXIT_SUCCESS) {
                fprintf(stderr, "error %d stopping scan %d.\n", err, scan_id);
                return;
            }
        }
    } else if (strcmp(tok, "list") == 0) {
        char* status = calloc(sizeof(char),8);
        if (!status) {
            fprintf(stderr, "failed to allocate memory\n");
            return;
        }
        for (int i = 0; i < MAX_ONGOING_SCANS; i++) {
            int err = get_state(i,status);
            if (err == EXIT_SUCCESS)
                printf("    %d - %s\n", i, status);
            else
                printf("    error fetching %d\n", i);
        }
        free(status);
    } else if (strcmp(tok, "start") == 0) {
        int* vna_list = calloc(sizeof(int),MAXIMUM_VNA_PORTS);
        if (!vna_list) {
            fprintf(stderr, "couldn't assign space for vna ids");
            return;
        }
        tok = strtok(NULL, " \n");
        int nbr_vnas = (tok == NULL ? get_connected_vnas(vna_list) : get_vna_list_from_args(tok,vna_list));
        if (nbr_vnas < 1) {
            printf("%d vnas not enough", nbr_vnas);
            return;
        }
        start_sweep(nbr_vnas, vna_list, nbr_scans, start, stop, ONGOING, sweeps, pps, interactive_label, verbose);
    } 
    else {
        printf("Usage: sweep <command>\nSee 'help sweep' for more info.\n");
    }
}

int calculate_resolution(int res, int* nbr_scans, int* points_per_scan) {
    if (res <= 0) {
        return EXIT_FAILURE;
    } else if (res <= 101) {
        *nbr_scans = 1;
        *points_per_scan = res;
        return EXIT_SUCCESS;
    } else {
        *nbr_scans = (int)round(res / 101);
        *points_per_scan = 101;
        return EXIT_SUCCESS;
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
            fprintf(stderr,"ERROR: No value provided for start frequency.\n");
            return;
        }
        if (!isValidLong(tok)) {
            fprintf(stderr,"ERROR: Start frequency must be a number.\n");
            return;
        }
        
        long val = atol(tok);
        if (val <= 0) {
            fprintf(stderr,"ERROR: Start frequency must be a positive number.\n");
            return;
        }
        if (val < 10000 || val > 1500000000) {
            fprintf(stderr,"ERROR: Start frequency must be between 10kHz and 1.5GHz.\n");
            return;
        }
        if (val >= stop) {
            printf("ERROR: Start frequency must be less than stop frequency (%ld Hz).\n", stop);
            return;
        }

        start = val;
    } else if (strcmp(tok,"stop") == 0) {
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
    } else if (strcmp(tok, "resolution") == 0 || strcmp(tok, "res") == 0) {
        tok = strtok(NULL, " \n");
        if (tok == NULL) {
            printf("ERROR: No value provided for resolution.\n");
            return;
        }
        if (!isValidInt(tok)) {
            printf("ERROR: Resolution must be a valid integer.\n");
            return;
        }
        
        int val = atoi(tok);
        if (val <= 0) {
            printf("ERROR: Resolution must be a positive integer.\n");
            return;
        }

        resolution = val;
        calculate_resolution(resolution, &nbr_scans, &pps);
    } else if (strcmp(tok, "scans") == 0) {
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
        resolution = nbr_scans * pps;
    } else if (strcmp(tok, "points") == 0) {
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
        resolution = nbr_scans * pps;
    } else if (strcmp(tok, "sweeps") == 0) {
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
    } else if (strcmp(tok, "verbose") == 0) {
        tok = strtok(NULL, " \n");
        if (tok == NULL) {
            printf("ERROR: No value provided for verbosity.\n");
            return;
        }
        if (strcmp(tok, "true") == 0) {
            verbose = true;
        } else if (strcmp(tok, "false") == 0) {
            verbose = false;
        } else {
            printf("ERROR: verbose must be 'true' or 'false'\n");
            return;
        }
    } else {
        printf("Parameter not recognised. Available parameters: start, stop, scans, sweeps, points, verbose\n");
    }
}

void list() {
   printf("\
    Current settings:\n\
        Start frequency: %ld Hz\n\
        Stop frequency: %ld Hz\n\
        Resolution: %d\n\
            Number of scans: %d\n\
            Points per scan: %d\n\
        Number of sweeps: %d\n\
        Number of VNAs: %d\n\
        Verbose: %s\n", 
        start, stop, resolution, nbr_scans, pps, sweeps, get_vna_count(), verbose ? "true" : "false");
}


void list_vnas() {
    print_vnas();
    char* new_paths[MAXIMUM_VNA_PORTS];
    int new = find_vnas(new_paths,"/dev");
    if (new > 0) {
        printf("Other serial devices detected:\n");
        for (int i = 0; i < new; i++) {
            printf("    %s\n", new_paths[i]);
            free(new_paths[i]);
            new_paths[i] = NULL;
        }
    }
    else {
        printf("No other serial devices detected\n");
    }
}

void vna_commands() {
    char* tok = strtok(NULL, " \n");
    if (tok == NULL) {
        printf("Usage: vna <add/list> [name]\nSee 'help scan' for more info.\n");
        return;
    }
    if (strcmp(tok,"add") == 0) {
        tok = strtok(NULL, " \n");
        if (tok == NULL) {
            printf("Attempting to add all found vnas:\n");
            int added = add_all_vnas();
            printf("    %d VNAs successfully added\n",added);
            return;
        }
        int err = add_vna(tok);
        if (err < 0) {
            fprintf(stderr, "Error %i: %s\n", errno, strerror(errno));
            return;
        }
        switch(err) {
        case 1:
            fprintf(stderr, "Maximum number of VNAs already connected.\n");
            break;
        case 2:
            fprintf(stderr, "Port address too long, must be under %d characters\n",MAXIMUM_VNA_PATH_LENGTH);
            break;
        case 3:
            fprintf(stderr, "VNA is already connected\n");
            break;
        case 4:
            fprintf(stderr, "Serial device is not a NanoVNA-H\n");
            break;
        }
    } else if (strcmp(tok,"remove") == 0) {
        tok = strtok(NULL, " \n");
        if (tok == NULL) {
            fprintf(stderr, "please provide an address\n");
            return;
        }
        if (remove_vna_name(tok) != EXIT_SUCCESS) {
            fprintf(stderr,"could not remove VNA %s\n",tok);
            return;
        }
    } else if (strcmp(tok,"list") == 0) {
        list_vnas();
    } else if (strcmp(tok,"ping") == 0) {
        vna_ping();
    } else if (strcmp(tok,"id") == 0) {
        vna_id();
    } else if (strcmp(tok,"reset") == 0) {
        vna_reset();
    } else {
        printf("Usage: vna <add/list> [name]\nSee 'help scan' for more info.\n");
    }
}

int read_command() {
    char buff[50];
    fgets(buff, sizeof(buff), stdin);

    char* tok = strtok(buff, " \n");

    if (tok == NULL) {
        return 0;
    } else if (strcmp(tok,"scan") == 0) {
        scan();
    } else if (strcmp(tok,"sweep") == 0) {
        sweep();
    } else if (strcmp(tok,"exit") == 0) {
        return 1;
    } else if (strcmp(tok,"help") == 0) {
        help();
    } else if (strcmp(tok,"set") == 0) {
        set();
    } else if (strcmp(tok, "list") == 0) {
        list();
    } else if (strcmp(tok,"vna") == 0) {
        vna_commands();
    } else {
        printf("Command not recognised. Type 'help' for list of available commands.\n");
    }
    return 0;
}

int initialise_settings() {
    start = 50000000;
    stop = 900000000;
    resolution = 505;
    nbr_scans = 5;
    pps = 101;
    sweeps = 1;
    verbose = false;

    return initialise_port_array();
}

/**
 * Main method is not defined for testing (conflicts with Unity
 * main method) but otherwise is used.
 */
#ifndef TESTSUITE

/**
 * Initialises settings then repeatedly calls read_command
 * until it returns 1 (meaning the exit command has been sent)
 */
int main() {
    initialise_settings();
    int fin = 0;
    while (fin != 1) {
        printf(">>> ");
        fin = read_command();
    }
    return 0;
}
#endif