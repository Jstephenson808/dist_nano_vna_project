#include "VnaScanMultithreaded.h"

/*
 * Sends NanoVNA a scan command, prints formatted output
 * 
 * Output is recieved in binary, in frames as specified by the datapoint struct.
 * Output can have echoes and unexpected elements. When searching for the start of a 
 * specified output ("skip to binary header"), uses characters to represent bytes being
 * read and scans one byte at a time to match binary header pattern.
 * Scanning is done with larger buffers at all other points, as 1 byte buffers are inefficient.
 */
int main(int argc, char *argv[]) {
    // defaults
    long start_freq = 50000000;
    long stop_freq = 900000000;
    int nbr_scans = 20;
    int sweeps = 5;
    SweepMode sweep_mode = NUM_SWEEPS;
    int nbr_nanoVNAs = 1;
    const char **ports;
    const char* default_port = "/dev/ttyACM0";

    int pps = 101; // points per scan

    if (argc > 1) {
        if (argc < 8) {
            fprintf(stderr, "Usage: %s <start_freq> <stop_freq> <nbr_scans> <sweep_mode> <sweeps> <points_per_scan> <nbr_nanoVNAs> [port1] [port2] ...\n", argv[0]);
            fprintf(stderr, "Example: %s 50000000 900000000 20 -s 5 101 2 /dev/ttyACM0 /dev/ttyACM1\n\n", argv[0]);
            fprintf(stderr, "Run without arguments for default scan\n");
            return EXIT_FAILURE;
        }

        start_freq = atol(argv[1]);
        stop_freq = atol(argv[2]);
        nbr_scans = atoi(argv[3]);
        sweeps = atoi(argv[5]);
        pps = atoi(argv[6]);
        nbr_nanoVNAs = atoi(argv[7]);
        ports = (const char **)&argv[8];

        // Validation of arguments
        if ((strcmp("-s",argv[4]) == 0)) {
            sweep_mode = NUM_SWEEPS;
        }
        else if (strcmp("-t",argv[4]) == 0) {
            sweep_mode = TIME;
        }
        else {
            fprintf(stderr, "Error: sweep mode must be either '-s' (number of sweeps) or '-t' (time)\n");
            return EXIT_FAILURE;
        }

        if (start_freq <= 0 || stop_freq <= 0 || nbr_scans <= 0 || 
            nbr_nanoVNAs <= 0 || sweeps <= 0) {
            fprintf(stderr, "Error: All arguments must be positive numbers\n");
            return EXIT_FAILURE;
        }
        
        if (start_freq >= stop_freq) {
            fprintf(stderr, "Error: start_freq must be less than stop_freq\n");
            return EXIT_FAILURE;
        }

        if (start_freq < 10000 || stop_freq > 1500000000) {
            fprintf(stderr, "Error: minimum frequency 10kHz, maximum frequency 1.5GHzq\n");
            return EXIT_FAILURE;
        }

        if (argc < 8 + nbr_nanoVNAs) {
            fprintf(stderr, "Error: Must provide %d serial port path(s) after the 5 parameters\n", nbr_nanoVNAs);
            fprintf(stderr, "You provided: %d port(s), need: %d\n", argc - 7, nbr_nanoVNAs);
            return EXIT_FAILURE;
        }

        if (pps < 1 || pps > 101) {
            fprintf(stderr, "Error: minimum points per scan 1, maximum points per scan 101\n");
            return EXIT_FAILURE;
        }

    } else {
        printf("Running default scan: %ld Hz to %ld Hz, %d points, %d PPS, %d VNA(s), %d sweep(s)\n",
               start_freq, stop_freq, nbr_scans*pps, pps, nbr_nanoVNAs, sweeps);
        
        // Default: try /dev/ttyACM0
        ports = (const char **)&default_port;
        fprintf(stderr, "Warning: No ports specified, will use hardcoded /dev/ttyACM0\n");
    }

    // assign error handler
    if (signal(SIGINT, fatal_error_signal) == SIG_ERR) {
        fprintf(stderr, "An error occurred while setting a signal handler.\n");
        return EXIT_FAILURE;
    }

    // start timing
    struct timeval stop, start;
    gettimeofday(&start, NULL);

    // call a scan
    run_multithreaded_scan(nbr_nanoVNAs, nbr_scans, start_freq, stop_freq, sweep_mode, sweeps, pps, ports);   

    // finish timing
    gettimeofday(&stop, NULL);
    double elapsed = (double)(stop.tv_sec - start.tv_sec) + 
                     (double)(stop.tv_usec - start.tv_usec) / 1000000.0;
    
    int total_points = (nbr_scans * pps) * sweeps;
    printf("---\ntook %.6f s\n", elapsed);
    printf("%.6f s per point measurement\n", elapsed / total_points);
    printf("%.2f points per second\n", total_points / elapsed);

    return EXIT_SUCCESS;
}