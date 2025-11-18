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
#include <stdatomic.h>

#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>

#define POINTS 101
#define MASK 135
#define N 100

/**
 * Declaring global variables (for error handling and port consistency)
 * 
 * @SERIAL_PORTS is a pointer to an array of all serial port connections
 * @INITIAL_PORT_SETTINGS is a pointer to an array of all serial port's initial settings
 * @VNA_COUNT is the number of VNAs currently connected
 */
int *SERIAL_PORTS = NULL;
struct termios* INITIAL_PORT_SETTINGS = NULL;
int VNA_COUNT = 0;

/**
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

/**
 * Closes all ports and restores their initial settings
 * 
 * We loop through the ports in reverse order to ensure that VNA_COUNT is
 * always accurate and if a fatal error occurs a new call of close_and_reset_all()
 * would not try to close an alread-closed port
 */
void close_and_reset_all();

/**
 * Restores serial port to original settings
 * 
 * @param fd The file descriptor of the serial port
 * @param settings The original termios settings to restore
 */
void restore_serial(int fd, const struct termios *settings);

/**
 * Opens a serial port
 * 
 * @param port The device path (e.g., "/dev/ttyACM0")
 * @return File descriptor on success, -1 on failure
 */
int open_serial(const char *port);

/**
 * Initialise port settings
 * 
 * Edits port settings to interact with a serial interface.
 * Sets up 115200 baud, 8N1, raw mode, no flow control, with timeout.
 * Flags should only be edited with bitwise operations.
 * Writes are permanent: initial settings are kept to restore on program close.
 * 
 * @param serial_port File descriptor of an already opened serial port
 * @return Original termios settings to restore later
 */
struct termios configure_serial(int serial_port);

/**
 * Writes a command to the serial port with error checking
 * 
 * @param fd The file descriptor of the serial port
 * @param cmd The command string to send (should include \r terminator)
 * @return Number of bytes written on success, -1 on error
 */
ssize_t write_command(int fd, const char *cmd);

/**
 * Reads exact number of bytes from serial port
 * Handles partial reads by continuing until all bytes are received
 * 
 * @param fd The file descriptor of the serial port
 * @param buffer The buffer to read data into
 * @param length The number of bytes to read
 * @return Number of bytes read on success, -1 on error, 0 on timeout
 */
ssize_t read_exact(int fd, uint8_t *buffer, size_t length);

/**
 * Finds the binary header in the serial stream
 * Scans byte-by-byte looking for the header pattern (mask + points)
 * 
 * @param fd The file descriptor of the serial port
 * @param expected_mask The expected mask value (e.g., 135)
 * @param expected_points The expected points value (e.g., 101)
 * @return 1 if header found, 0 if timeout/not found, -1 on error
 */
int find_binary_header(int fd, uint16_t expected_mask, uint16_t expected_points);

/**
 * Tests connection to NanoVNA by issuing info command
 * Sends "info" command and prints the response
 * 
 * @param serial_port The file descriptor of the serial port
 * @return 0 on success, 1 on error
 */
int test_connection(int serial_port);

/**
 * Fatal error handling. 
 * 
 * Calls close_and_reset_all before allowing the program to exit normally.
 * 
 * @param sig The signal number
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
// flag for when the consumer has no more to read. Currently no support for multiple consumers.
extern volatile atomic_int complete;

/**
 * A thread function to take scans from a NanoVNA onto buffer
 *
 * Accesses buffer according to the producer-consumer problem
 * Computes step (frequency distance between scans) from start stop and points,
 * then pulls scans from NanoVNA in increments of 101 points and appends to buffer.
 * 
 * @param args pointer to scan_producer_args struct used to pass arguments into this function
 */
struct scan_producer_args {
    int serial_port;
    int nbr_scans;
    int start;
    int stop;
    int nbr_sweeps; 
    struct datapoint_NanoVNAH **buffer;
    struct coordination_args *thread_args;
};
void* scan_producer(void *args);

/**
 * A thread function to print scans from buffer
 * 
 * Accesses buffer according to the producer-consumer problem
 * Takes arrays of 101 readings from buffer and prints them until scans are done
 * 
 * @param args pointer to struct scan_consumer_args
 */
struct scan_consumer_args {
    struct datapoint_NanoVNAH **buffer;
    struct coordination_args *thread_args;
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
 * @param nbr_scans Total number of scans, determining number of data points to collect (101 dp per scan)
 * @param start Starting frequency in Hz
 * @param stop Stopping frequency in Hz
 * @param nbr_sweeps Number of frequency sweeps to perform
 * 
 * TODO: Make functional for multiple VNAs
 */
void run_multithreaded_scan(int num_vnas, int nbr_scans, int start, int stop, int nbr_sweeps, const char **ports);

#endif