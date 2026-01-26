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

#define POINTS 101 // number of points per scan throughout program
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
    struct nanovna_raw_datapoint point[POINTS];    // Raw measurement from device
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
int configure_serial(int serial_port, struct termios *initial_tty);

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
 * A thread function to take scans from a NanoVNA onto buffer
 *
 * Accesses buffer according to the producer-consumer problem
 * Computes step (frequency distance between scans) from start stop and points,
 * then pulls scans from NanoVNA in increments of 101 points and appends to buffer.
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

struct datapoint_nanoVNA_H* pull_scan(int port, int vnaID, int start, int stop);

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
 * @param ports Array of serial port paths (e.g., ["/dev/ttyACM0", "/dev/ttyACM1"])
 */
typedef enum {
    NUM_SWEEPS,
    TIME
} SweepMode;
void run_multithreaded_scan(int num_vnas, int nbr_scans, int start, int stop, SweepMode sweep_mode, int sweeps, const char **ports);

// START OF PYTHON LIBRARY API
typedef struct {
    int vna_id;
    uint32_t frequency;
    float s11_re;
    float s11_im;
    float s21_re;
    float s21_im;
    int sweep_number;
    double send_time;
    double receive_time;
} DataPoint;

int fill_test_datapoint(DataPoint *point);

/**
 * Callback function types for async scanning
 * These are called by the C library to send data and status updates to Python
 */
typedef void (*DataCallback)(DataPoint *datapoint);
typedef void (*StatusCallback)(const char *message);
typedef void (*ErrorCallback)(const char *error_message);

/**
 * Start an asynchronous VNA scan with callbacks
 * 
 * The scan runs in a background thread and invokes callbacks for each data point.
 * Only one scan can run at a time; call stop_async_scan() first if a scan is active.
 * 
 * @param num_vnas Number of VNAs to scan with
 * @param nbr_scans Number of scan sweeps to perform
 * @param start Starting frequency in Hz
 * @param stop Stopping frequency in Hz
 * @param sweep_mode 0=NUM_SWEEPS, 1=TIME (determines meaning of sweeps parameter)
 * @param sweeps_or_time Number of sweeps or time limit in seconds
 * @param ports Array of serial port paths (e.g., ["/dev/ttyACM0", "/dev/ttyACM1"])
 * @param data_cb Callback function called for each data point received
 * @param status_cb Callback function for status messages (can be NULL)
 * @param error_cb Callback function for errors (can be NULL)
 * @return 0 on success, -1 on error (e.g., scan already running)
 */
int start_async_scan(int num_vnas, int nbr_scans, int start, int stop, 
                     int sweep_mode, int sweeps_or_time, const char **ports,
                     DataCallback data_cb, StatusCallback status_cb, ErrorCallback error_cb);

/**
 * Stop the currently running async scan
 * 
 * Safe to call even if no scan is running.
 * 
 * @return 0 on success, -1 if error during shutdown
 */
int stop_async_scan(void);

/**
 * Check if an async scan is currently running
 * 
 * @return 1 if scan is active, 0 if not
 */
int is_async_scan_active(void);

#endif