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
 * 
 * @fatal_error_in_progress is a flag to ensure errors during error handling do not cause infinite recursion
 * @SERIAL_PORTS is a pointer to an array of all serial port connections
 * @INITIAL_PORT_SETTINGS is a pointer to an array of all serial port's initial settings
 * @VNA_COUNT is the number of VNAs currently connected
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
 * We loop through the ports in reverse order to ensure that VNA_COUNT is
 * always accurate and if a fatal error occurs a new call of close_and_reset_all()
 * would not try to close an alread-closed port
 */
void close_and_reset_all();

/*
 * Opens a serial port
 * 
 * @param port The device path (e.g., "/dev/ttyACM0")
 * @return File descriptor on success, -1 on failure
 */
int open_serial(const char *port);

/*
 * Initialise port settings
 * 
 * Edits port settings to interact with a serial interface.
 * Sets up 115200 baud, 8N1, raw mode, no flow control, with timeout.
 * Flags should only be edited with bitwise operations.
 * Writes are permanent: initial settings are kept to restore on program close.
 * 
 * @param serial_port should already be opened successfully
 * @return initial settings to restore. Also stored in global variable.
 */
struct termios init_serial_settings(int serial_port);

/*
 * Writes a command to the serial port with error checking
 * 
 * @param fd The file descriptor of the serial port
 * @param cmd The command string to send (should include \r terminator)
 * @return Number of bytes written on success, -1 on error
 */
ssize_t write_command(int fd, const char *cmd);

/*
 * Fatal error handling. 
 * 
 * Calls close_and_reset_all before allowing the program to exit normally.
 * 
 * @param sig The signal number
 * @fatal_error_in_progress to prevent infinite recursion.
 */
void fatal_error_signal(int sig);

//------------------------
// SCAN LOGIC
//------------------------

/*
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
// flag for when the consumer has no more to read. Currently no support for multiple consumers.
short complete = 0;

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
 * @*thread_args, a pointer to the thread coordination variables created by scan_coordinator
 */
struct scan_producer_args {
    int serial_port;
    int points;
    int start;
    int stop;
    struct datapoint_NanoVNAH **buffer;
    struct coordination_args *thread_args;
};
void* scan_producer(void *args);

/*
 * A thread function to print scans from buffer
 * 
 * Accesses buffer according to the producer-consumer problem
 * Takes arrays of 101 readings from buffer and prints them until scans are done
 * 
 * @*args pointer to struct scan_consumer_args
 * @**buffer, a pointer to an array of N pointers to arrays of 101 readings
 * @*thread_args, a pointer to the thread coordination variables created by scan_coordinator
 */
struct scan_consumer_args {
    struct datapoint_NanoVNAH **buffer;
    struct coordination_args *thread_args;
};
void* scan_consumer(void *args);

/*
 * Coordinator. Creates scan_producer threads for a range of VNAs and a scan_consumer thread linked to them
 * 
 * Handles connections and settings for each VNA, also initialising global variables for ports.
 * Creates a buffer and a coordination_args struct to hold coordination variables for multithreading
 * Creates a consumer thread
 * Creates a producer thread for each VNA
 * Waits for threads to finish
 * Resets ports, frees memory, and exits
 * 
 * TODO: Make functional for multiple VNAs
 */
void scan_coordinator(int num_vnas, int points, int start, int stop);

#endif