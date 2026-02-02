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

//--------------------------------
// Structs for data points
//--------------------------------

/**
 * Represents a complex number.
 * re - real component
 * im - imaginary component
 */
struct complex {
    float re;
    float im;
};

/**
 * Raw binary data from NanoVNA (exactly 20 bytes as sent over serial)
 */
struct nanovna_raw_datapoint {
    uint32_t frequency;       // 4 bytes
    struct complex s11;       // 8 bytes (2 floats)
    struct complex s21;       // 8 bytes (2 floats)
};

/**
 * Internal representation of a scan, with metadata
 */
struct datapoint_nanoVNA_H {
    int vna_id;                               // Which VNA produced this data
    struct timeval send_time, receive_time;   // Time information
    struct nanovna_raw_datapoint *point;      // Array of measurement datapoints
};

//----------------------------
// Bounded Buffer Logic
//----------------------------

/**
 * Struct and functions used for shared buffer and concurrency variables
 */
struct bounded_buffer {
    struct datapoint_nanoVNA_H **buffer;
    int count;
    int in;
    int out;
    int pps;
    atomic_int complete;
    pthread_mutex_t lock;
    pthread_cond_t take_cond;
    pthread_cond_t add_cond;
};

/**
 * Sets up a new bounded buffer
 * 
 * @param bb pointer to the space reserved for this struct (uninitialised)
 * @param pps the points to scan to associate with this buffer
 * @return EXIT_SUCCESS on success, error code otherwise
 */
int create_bounded_buffer(struct bounded_buffer *bb, int pps);

/**
 * Frees all memory associated with the given bounded buffer
 * 
 * @param buffer pointer to the buffer to destroy (will free that memory, do not reaccess)
 */
void destroy_bounded_buffer(struct bounded_buffer *buffer);

/**
 * Puts specified data pointer into the buffer
 * 
 * @param buffer pointer to the buffer to put in
 * @param data pointer to the array of data to put in the buffer
 */
void add_buff(struct bounded_buffer *buffer, struct datapoint_nanoVNA_H *data);

/**
 * Returns the first data pointer from the buffer.
 * 
 * @param buffer pointer to the buffer to take from
 * @return pointer to the data, or NULL if no more data to pull (scan finished)
 */
struct datapoint_nanoVNA_H* take_buff(struct bounded_buffer *buffer);

//--------------------------------
// Pulling Data Logic
//--------------------------------

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

/**
 * A function to pull a scan from a NanoVNA
 * 
 * Assigns space for array, sends command to the VNA,
 * and pulls each of the datapoints into the array.
 * 
 * @param vna_id the VnaCommunication ID of the VNA to pull from
 * @param start frequency in Hz
 * @param start frequency in Hz
 * @param pps points to pull in this scan
 * @return A pointer to the array of datapoints that have been pulled
 */
struct datapoint_nanoVNA_H* pull_scan(int vna_id, int start, int stop, int pps);

//----------------------------------------
// Producer/Consumer Thread Logic
//----------------------------------------

/**
 * Struct to hold arguments for producer threads
 */
struct scan_producer_args {
    int scan_id;
    int vna_id;
    int nbr_scans;
    int start;
    int stop;
    int nbr_sweeps;
    struct bounded_buffer *bfr;
};

/**
 * A thread function to take a specified number of scans from a NanoVNA onto buffer
 *
 * Accesses buffer according to the producer-consumer problem, using add_buff
 * Computes step (frequency distance between scans) from start stop and points,
 * then pulls scans from NanoVNA in increments of pps points and appends to buffer.
 * 
 * Decrements scan state when finished, if scan state == 0 sets scan to finished.
 * 
 * @param args pointer to scan_producer_args struct used to pass arguments into this function
 */
void* scan_producer(void *arguments);

/**
 * A thread function to take scans continuously from a NanoVNA onto buffer
 * 
 * Accesses buffer according to the producer-consumer problem, using add_buff.
 * Pulls until scan state is set to 0, either by scan_timer or destroy_scan.
 * 
 * @param args pointer to scan_producer_args struct used to pass arguments into this function
 */
void* sweep_producer(void *arguments);

/**
 * A thread function to wait a specified amount of time before signalling
 * a scan to stop (by setting scan state to 0).
 * 
 * Should probably be detatched.
 * 
 * @param args pointer to scan_timer_args struct
 */
struct scan_timer_args {
    int time_to_wait;
    int scan_id;
};
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
    struct bounded_buffer  *bfr;
    FILE *touchstone_file;
    char *id_string;
    char *label;
    bool verbose;
    struct timeval program_start_time;
};
void* scan_consumer(void *args);

/**
 * -----------------------------------------------
 * Touchstone Files
 * -----------------------------------------------
 */

/**
 * Opens a touchstone file with name format "vna_scan_at_%Y-%m-%d_%H-%M-%S.s2p"
 * 
 * Caller's responsibility to close.
 * 
 * @param tm_info time information
 * @return a pointer to the file as returned by fopen
 */
FILE * create_touchstone_file(struct tm *tm_info);

/**
 * enum for type of sweep to be used
 */
typedef enum {
    NUM_SWEEPS,
    TIME,
    ONGOING
} SweepMode;

//----------------------------------------
// Sweep Logic
//----------------------------------------

/**
 * Orchestrates creating a new run_sweep thread and returns an ID for that thread.
 * 
 * Calls initialise_scan to 
 * 
 * @param nbr_vnas Number of VNAs to scan with
 * @param nbr_scans Total number of scans per VNA, determining number of data points to collect (101 dp per scan)
 * @param start Starting frequency in Hz
 * @param stop Stopping frequency in Hz
 * @param sweep_mode Enum for type of scan to do.
 * @param nbr_sweeps Usage depends on sweep_mode.
 *  NUM_SWEEPS - Number of times to iterate over scan range
 *  TIME - Number of seconds to sweep for
 *  ONGOING - No effect
 * @param pps Number of points per scan
 * @param user_label 
 * @param verbose True -- prints scan data to stdout. False -- only produces file.
 * 
 * @return scan_id - used to reference this scan thread etc. again (e.g. when closing it)
 */
int start_sweep(int nbr_vnas, int nbr_scans, int start, int stop, SweepMode sweep_mode, int sweeps, int pps, const char* user_label);

/**
 * Signals specified scan to end, waits for it to finish and joins the thread.
 * Then frees/resets supporting data structures.
 * 
 * @param scan_id The ID used to reference this scan thread, returned by start_sweep
 * @return EXIT_SUCCESS on success, error code on failure.
 */
int destroy_scan(int scan_id);

#endif