#ifndef VNASCANMULTITHREADED_H_
#define VNASCANMULTITHREADED_H_

// needed for CRTSCTS macro
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <inttypes.h>
#include <math.h>
#include <time.h>

#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>

#define POINTS 101
#define MASK 135
#define N 100

/*
 * Declaring global variables (for error handling and port consistency)
 */
volatile sig_atomic_t fatal_error_in_progress = 0;
int *SERIAL_PORTS = NULL;
struct termios* INITIAL_PORT_SETTINGS = NULL;
int VNA_COUNT = 0;

/*
 * Declaring structs for data points
 */
struct complex {
    float re;
    float im;
};
struct datapoint_NanoVNAH {
    uint32_t frequency;
    struct complex s11;
    struct complex s21;
};

/*
 * Closes all ports and restores their initial settings
 * 
 * @SERIAL_PORTS
 * @INITIAL_PORT_SETTINGS
 */
void close_and_reset_all();

/*
 * Fatal error handling. 
 * 
 * Calls close_and_reset before allowing the program to exit normally.
 * 
 * @fatal_error_in_progress to prevent infinite recursion.
 */
void fatal_error_signal(int sig);

/*
 * Initialise port settings
 * 
 * Edits port settings to interact with a serial interface.
 * Flags should only be edited with bitwise operations.
 * Writes are permanent: initial settings are kept to restore on program close.
 * 
 * @serial_port should already be opened successfully
 * @return initial settings to restore. Also stored in global variable.
 */
struct termios init_serial_settings(int serial_port);

//------------------------
// SCAN LOGIC
//------------------------

/*
 * Coordination variables for multithreading
 */
int count = 0;
int in = 0;
int out = 0;
short complete = 0;
pthread_cond_t remove_cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t fill_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

/*
 * A thread function to take scans from a NanoVNA onto buffer
 *
 * Accesses buffer according to the producer-consumer problem
 * Computes step (frequency distance between scans) from start stop and points,
 * then pulls scans from NanoVNA in increments of 101 points and appends to buffer.
 * 
 * @*args pointer to scan_producer_args struct used to pass arguments into this function
 * @serial_port is the port to take a reading from
 * @points is the number of readings to take
 * @start is the starting frequency
 * @stop is the stopping frequency
 * @**buffer is a pointer to an array of N pointers to arrays of 101 readings
 */
struct scan_producer_args {
    int serial_port;
    int points;
    int start;
    int stop;
    struct datapoint_NanoVNAH **buffer;
};
void* scan_producer(void *args);

/*
 * A thread function to print scans from buffer
 * 
 * Accesses buffer according to the producer-consumer problem
 * Takes arrays of 101 readings from buffer and prints them until scans are done
 * 
 * @*args pointer to struct datapoint **buffer, an array of N pointers to arrays of 101 readings
 */
void* scan_consumer(void *args);

/*
 * Coordinator. Creates scan_producer threads for a range of VNAs and a scan_consumer thread linked to them
 * 
 * Handles connections and settings for each VNA, and the creation of a data buffer.
 * 
 * TODO
 */
void scan_coordinator(int num_vnas, int points, int start, int stop);

#endif