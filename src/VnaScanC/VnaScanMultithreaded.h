#ifndef VNASCANMULTITHREADED_H_
#define VNASCANMULTITHREADED_H_

// needed for CRTSCTS macro
#define _DEFAULT_SOURCE

#include "VnaCommunication.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <inttypes.h>
#include <math.h>
#include <time.h>
#include <stdatomic.h>

#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>

#define MASK 135 // mask passed to VNAs, defining how to format output
#define N 100 // size of bounded buffer

/**
 * Declaring structs for data points
 */
struct complex {
    float re;
    float im;
};

// Raw binary data from NanoVNA (exactly 20 bytes as sent over serial)
struct nanovna_raw_datapoint {
    uint32_t frequency;       // 4 bytes
    struct complex s11;       // 8 bytes (2 floats)
    struct complex s21;       // 8 bytes (2 floats)
};

// Internal representation with metadata
struct datapoint_nanoVNA_H {
    int vna_id;                           // Which VNA produced this data
    struct timeval send_time, receive_time;
    struct nanovna_raw_datapoint *point;    // Raw measurement from device
};

/**
 * Finds the binary header in the serial stream
 * Scans byte-by-byte looking for the header pattern (mask + points)
 * 
 * @param fd The file descriptor of the serial port
 * @param first_point Pointer to location at which to store the first point of the output
 * @param expected_mask The expected mask value (e.g., 135)
 * @param expected_points The expected points value (e.g., 101)
 * @return EXIT_SUCCESS if header found, EXIT_FAILURE if timeout/header not found or error
 */
int find_binary_header(int fd, struct nanovna_raw_datapoint* first_point, uint16_t expected_mask, uint16_t expected_points);

//------------------------
// SCAN LOGIC
//------------------------

/**
 * Struct and functions used for shared buffer and concurrency variables
 */
typedef struct BoundedBuffer {
    struct datapoint_nanoVNA_H **buffer;
    int count;
    int in;
    int out;
    atomic_int complete;
    pthread_cond_t take_cond;
    pthread_cond_t add_cond;
    pthread_mutex_t lock;
} BoundedBuffer;

int create_bounded_buffer(BoundedBuffer *bb);
void destroy_bounded_buffer(BoundedBuffer *buffer);
void add_buff(BoundedBuffer *buffer, struct datapoint_nanoVNA_H *data);
struct datapoint_nanoVNA_H* take_buff(BoundedBuffer *buffer);

/**
 * 
 */
struct datapoint_nanoVNA_H* pull_scan(int port, int vnaID, int start, int stop);

/**
 * A thread function to take scans from a NanoVNA onto buffer
 *
 * Accesses buffer according to the producer-consumer problem
 * Computes step (frequency distance between scans) from start stop and points,
 * then pulls scans from NanoVNA in increments of pps points and appends to buffer.
 * 
 * @param args pointer to scan_producer_args struct used to pass arguments into this function
 */
struct scan_producer_args {
    int vna_id;
    int serial_port;
    int nbr_scans;
    int start;
    int stop;
    int nbr_sweeps; 
    BoundedBuffer *bfr;
};
void* scan_producer_num(void *arguments);

struct scan_timer_args {
    int time_to_wait;
    BoundedBuffer *b;
};
void* scan_producer_time(void *arguments);
void* scan_timer(void* arguments);

/**
 * A thread function to print scans from buffer
 * 
 * Accesses buffer according to the producer-consumer problem
 * Takes arrays of 101 readings from buffer and prints them until scans are done
 * 
 * @param args pointer to struct scan_consumer_args
 */
struct scan_consumer_args {
    BoundedBuffer *bfr;
    FILE *touchstone_file;
    char *id_string;
    char *label;
};
void* scan_consumer(void *args);

/**
 * Orchestrates multithreaded VNA scanning
 * 
 * Handles connections and settings for each VNA, also initialising global variables for ports.
 * Creates a buffer and a coordination_args struct to hold coordination variables for multithreading
 * Creates a consumer thread
 * Creates a producer thread for each VNA
 * Waits for threads to finish
 * Resets ports, frees memory, and exits
 * 
 * @param num_vnas Number of VNAs to scan with
 * @param nbr_scans Total number of scans per VNA, determining number of data points to collect (101 dp per scan)
 * @param start Starting frequency in Hz
 * @param stop Stopping frequency in Hz
 * @param nbr_sweeps Number of frequency sweeps to perform
 * @param pps Number of points per scan
 * @param ports Array of serial port paths (e.g., ["/dev/ttyACM0", "/dev/ttyACM1"])
 */
typedef enum {
    NUM_SWEEPS,
    TIME
} SweepMode;
void run_multithreaded_scan(int num_vnas, int nbr_scans, int start, int stop, SweepMode sweep_mode, int sweeps, int pps, int *vna_fds, const char *user_label);

#endif