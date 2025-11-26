#include "VnaScanMultithreaded.h"
#include <libserialport.h>

int main(int argc, char *argv[]) {
    long start_freq = 50000000;
    long stop_freq = 900000000;
    int nbr_scans = 20;
    int nbr_sweeps = 5;
    int nbr_nanoVNAs = 1;

    if (argc > 1) {
        if (argc < 6) {
            fprintf(stderr,
                "Usage: %s <start_freq> <stop_freq> <nbr_scans> <nbr_sweeps> <nbr_nanoVNAs> [ports...]\n",
                argv[0]);
            return EXIT_FAILURE;
        }

        start_freq  = atol(argv[1]);
        stop_freq   = atol(argv[2]);
        nbr_scans   = atoi(argv[3]);
        nbr_sweeps  = atoi(argv[4]);
        nbr_nanoVNAs = atoi(argv[5]);

        if (argc < 6 + nbr_nanoVNAs) {
            fprintf(stderr, "Error: Expected %d port paths.\n", nbr_nanoVNAs);
            return EXIT_FAILURE;
        }
    } else {
        printf("Running default scan: %ld Hz to %ld Hz\n", start_freq, stop_freq);
        fprintf(stderr, "No ports specified, defaulting to /dev/ttyACM0\n");
    }

    if (signal(SIGINT, fatal_error_signal) == SIG_ERR) {
        fprintf(stderr, "Failed to set SIGINT handler\n");
        return EXIT_FAILURE;
    }

    struct timeval start, stop;
    gettimeofday(&start, NULL);

    const char **ports = (const char **)&argv[6];

    run_multithreaded_scan(
        nbr_nanoVNAs,
        nbr_scans,
        start_freq,
        stop_freq,
        nbr_sweeps,
        ports
    );

    gettimeofday(&stop, NULL);

    double elapsed =
        (double)(stop.tv_sec - start.tv_sec) +
        (double)(stop.tv_usec - start.tv_usec) / 1e6;

    int total_points = (nbr_scans * POINTS) * nbr_sweeps;

    printf("---\n");
    printf("Time: %.6f s\n", elapsed);
    printf("%.6f s per point\n", elapsed / total_points);
    printf("%.2f points/sec\n", total_points / elapsed);

    return EXIT_SUCCESS;
}
