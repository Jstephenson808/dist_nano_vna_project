#ifndef VNASCANMULTITHREADED_H_
#define VNASCANMULTITHREADED_H_

// needed for CRTSCTS macro
#define _DEFAULT_SOURCE

#include "VnaCommunication.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <signal.h>
#include <errno.h>
#include <math.h>
#include <time.h>

#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <pthread.h>
#include <sys/time.h>

#define MASK 135 // mask passed to VNAs, defining how to format output
#define N 100 // size of bounded buffer
#define MAX_ONGOING_SCANS 5

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
 * @param vna_id The program id of the vna to be used
 * @param first_point Pointer to location at which to store the first point of the output
 * @param expected_mask The expected mask value (e.g., 135)
 * @param expected_points The expected points value (e.g., 101)
 * @return EXIT_SUCCESS if header found, EXIT_FAILURE if timeout/header not found or error
 */
int find_binary_header(int vna_id, struct nanovna_raw_datapoint* first_point, uint16_t expected_mask, uint16_t expected_points);

//------------------------
// SCAN LOGIC
//------------------------

/**
 * Struct and functions used for shared buffer and concurrency variables
 */
struct bounded_buffer {
    struct datapoint_nanoVNA_H **buffer;
    int count;
    int in;
    int out;
    atomic_int complete;
    pthread_mutex_t lock;
    pthread_cond_t take_cond;
    pthread_cond_t add_cond;
};

int create_bounded_buffer(struct bounded_buffer  *bb);
void destroy_bounded_buffer(struct bounded_buffer  *buffer);
void add_buff(struct bounded_buffer  *buffer, struct datapoint_nanoVNA_H *data);
struct datapoint_nanoVNA_H* take_buff(struct bounded_buffer  *buffer);

/**
 * 
 */
struct datapoint_nanoVNA_H* pull_scan(int vna_id, int start, int stop);

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
    int scan_id;
    int vna_id;
    int nbr_scans;
    int start;
    int stop;
    int nbr_sweeps;
    struct bounded_buffer  *bfr;
};
void* scan_producer_num(void *arguments);

struct scan_timer_args {
    int time_to_wait;
    struct bounded_buffer  *b;
};
void* scan_producer_time(void *arguments);
void* scan_timer(void* arguments);

struct sweep_producer_args {
    int scan_id;
    int vna_id;
    int nbr_scans;
    int start;
    int stop;
    bool* stop_flag;
    struct bounded_buffer *bfr;
};
void* sweep_producer(void *arguments);

struct sweep_stopper_args {
    pthread_mutex_t lock;
    pthread_cond_t take_cond;
    struct bounded_buffer *b;
};
void sweep_stopper(void* arguments);

/**
 * A thread function to print scans from buffer
 * 
 * Accesses buffer according to the producer-consumer problem
 * Takes arrays of 101 readings from buffer and prints them until scans are done
 * 
 * @param args pointer to struct scan_consumer_args
 */
struct scan_consumer_args {
    struct bounded_buffer  *bfr;
    FILE *touchstone_file;
    char *id_string;
    char *label;
    bool verbose;
    bool verbose;
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
 */
typedef enum {
    NUM_SWEEPS,
    TIME,
    ONGOING
} SweepMode;
void run_multithreaded_scan(int num_vnas, int nbr_scans, int start, int stop, SweepMode sweep_mode, int sweeps, int pps, const char *user_label);

#endif