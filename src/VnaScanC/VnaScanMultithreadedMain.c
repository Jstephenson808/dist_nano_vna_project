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
    int nbr_sweeps = 5;
    int nbr_nanoVNAs = 1;
    const char **ports;

    if (argc > 1) {
        if (argc < 6) {
            fprintf(stderr, "Usage: %s <start_freq> <stop_freq> <nbr_scans> <nbr_sweeps> <nbr_nanoVNAs> [port1] [port2] ...\n", argv[0]);
            fprintf(stderr, "Example: %s 50000000 900000000 20 5 2 /dev/ttyACM0 /dev/ttyACM1\n\n", argv[0]);
            fprintf(stderr, "Run without arguments for default scan\n");
            return EXIT_FAILURE;
        }

        start_freq = atol(argv[1]);
        stop_freq = atol(argv[2]);
        nbr_scans = atoi(argv[3]);
        nbr_sweeps = atoi(argv[4]);
        nbr_nanoVNAs = atoi(argv[5]);

        // Validation of arguments
        if (start_freq <= 0 || stop_freq <= 0 || nbr_scans <= 0 || 
            nbr_nanoVNAs <= 0 || nbr_sweeps <= 0) {
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

        if (argc < 6 + nbr_nanoVNAs) {
            fprintf(stderr, "Error: Must provide %d serial port path(s) after the 5 parameters\n", nbr_nanoVNAs);
            fprintf(stderr, "You provided: %d port(s), need: %d\n", argc - 6, nbr_nanoVNAs);
            return EXIT_FAILURE;
        }

    } else {
        printf("Running default scan: %ld Hz to %ld Hz, %d points, %d VNA(s), %d sweep(s)\n",
               start_freq, stop_freq, nbr_scans*POINTS, nbr_nanoVNAs, nbr_sweeps);
        
        // Default: try /dev/ttyACM0
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

    // assign ports
    if (argc >= 6) {
        ports = (const char **)&argv[6];
    }
    else {
        ports = (const char **)&"/dev/ttyACM0";
    }

    // call a scan
    run_multithreaded_scan(nbr_nanoVNAs, nbr_scans, start_freq, stop_freq, nbr_sweeps, ports);    

    // finish timing
    gettimeofday(&stop, NULL);
    double elapsed = (double)(stop.tv_sec - start.tv_sec) + 
                     (double)(stop.tv_usec - start.tv_usec) / 1000000.0;
    
    int total_points = (nbr_scans * POINTS) * nbr_sweeps;
    printf("---\ntook %.6f s\n", elapsed);
    printf("%.6f s per point measurement\n", elapsed / total_points);
    printf("%.2f points per second\n", total_points / elapsed);

    return EXIT_SUCCESS;
}