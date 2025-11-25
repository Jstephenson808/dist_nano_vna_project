#ifndef VNASCANMULTITHREADED_H_
#define VNASCANMULTITHREADED_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <inttypes.h>
#include <math.h>
#include <time.h>
#include <stdatomic.h>
#include <pthread.h>

// Cross-platform serial port library
#include <libserialport.h>

// Platform-specific includes for timing
#ifdef _WIN32
    #include <windows.h>
    #include <sys/timeb.h>
    #include <sys/time.h>
#else
    #include <sys/time.h>
    #include <unistd.h>
#endif

#define POINTS 101
#define MASK 135
#define N 100

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
struct datapoint_NanoVNAH {
    int vna_id;                           // Which VNA produced this data
    struct timeval send_time, recieve_time;
    struct nanovna_raw_datapoint point[POINTS];    // Raw measurement from device
};

/**
 * Cross-platform timing function
 */
void get_current_time(struct timeval *tv);

/**
 * Closes all ports and restores their initial settings
 */
void close_and_reset_all();

/**
 * Restores serial port to original settings
 */
void restore_serial(struct sp_port *port, struct sp_port_config *settings);

/**
 * Opens a serial port
 */
struct sp_port* open_serial(const char *port);

/**
 * Initialise port settings
 */
struct sp_port_config* configure_serial(struct sp_port *port);

/**
 * Writes a command to the serial port with error checking
 */
ssize_t write_command(struct sp_port *port, const char *cmd);

/**
 * Reads exact number of bytes from serial port
 */
ssize_t read_exact(struct sp_port *port, uint8_t *buffer, size_t length);

/**
 * Finds the binary header in the serial stream
 */
int find_binary_header(struct sp_port *port, uint16_t expected_mask, uint16_t expected_points);

/**
 * Tests connection to NanoVNA by issuing info command
 */
int test_connection(struct sp_port *port);

/**
 * Fatal error handling
 */
void fatal_error_signal(int sig);

//------------------------
// SCAN LOGIC
//------------------------

/**
 * Coordination variables for multithreading
 */
struct coordination_args {
    int count;
    int in;
    int out;
    pthread_cond_t remove_cond;
    pthread_cond_t fill_cond;
    pthread_mutex_t lock;
};

extern volatile atomic_int complete;

struct scan_producer_args {
    int vna_id;
    struct sp_port *serial_port;
    int nbr_scans;
    int start;
    int stop;
    int nbr_sweeps; 
    struct datapoint_NanoVNAH **buffer;
    struct coordination_args *thread_args;
};
void* scan_producer(void *args);

struct scan_consumer_args {
    struct datapoint_NanoVNAH **buffer;
    struct coordination_args *thread_args;
};
void* scan_consumer(void *args);

/**
 * Orchestrates multithreaded VNA scanning
 */
void run_multithreaded_scan(int num_vnas, int nbr_scans, int start, int stop, int nbr_sweeps, const char **ports);

#endif