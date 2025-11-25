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
    #include <sys/time.h>  // MinGW64 provides timeval here
#else
    #include <sys/time.h>
    #include <unistd.h>
#endif

#define POINTS 101
#define MASK 135
#define N 100

// Serial communication timeout configuration (in milliseconds)
// Windows USB-serial typically has higher latency than Linux
// Adjust these values for optimal performance vs reliability
#ifdef _WIN32
    // Windows optimized: separate timeouts for different operations
    #define SERIAL_WRITE_TIMEOUT 300   // Write command timeout
    #define SERIAL_READ_TIMEOUT  2000  // Bulk data read timeout (INCREASED - some systems need this)
    #define SERIAL_HEADER_TIMEOUT 300  // Header search chunk timeout (32 bytes)
    #define SERIAL_BYTE_TIMEOUT  300   // Legacy (not used in optimized code)
#else
    // Linux can use shorter timeouts due to better USB stack
    #define SERIAL_WRITE_TIMEOUT 100
    #define SERIAL_READ_TIMEOUT  100
    #define SERIAL_HEADER_TIMEOUT 100
    #define SERIAL_BYTE_TIMEOUT  100
#endif

// Timeout tuning notes:
// - SERIAL_READ_TIMEOUT: Used for reading 2020 bytes of scan data
//   Systems with high USB latency may need 2000-3000ms
// - SERIAL_HEADER_TIMEOUT: Used for reading 32-byte chunks during header search
// - If still getting timeouts, try 3000ms or check USB port/cable/system load

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
 * 
 * We loop through the ports in reverse order to ensure that VNA_COUNT is
 * always accurate and if a fatal error occurs a new call of close_and_reset_all()
 * would not try to close an already-closed port
 */
void close_and_reset_all();

/**
 * Restores serial port to original settings
 * 
 * @param port The serial port
 * @param settings The original port configuration to restore
 */
void restore_serial(struct sp_port *port, struct sp_port_config *settings);

/**
 * Opens a serial port
 * 
 * @param port_name The device path (e.g., "/dev/ttyACM0" on Linux, "COM3" on Windows)
 * @param port Output parameter for the opened port
 * @return 0 on success, -1 on failure
 */
int open_serial(const char *port_name, struct sp_port **port);

/**
 * Initialise port settings
 * 
 * Edits port settings to interact with a serial interface.
 * Sets up 115200 baud, 8N1, raw mode, no flow control, with timeout.
 * 
 * @param port The serial port to configure
 * @return Original port configuration to restore later, NULL on error
 */
struct sp_port_config* configure_serial(struct sp_port *port);

/**
 * Writes a command to the serial port with error checking
 * 
 * @param port The serial port
 * @param cmd The command string to send (should include \r terminator)
 * @return Number of bytes written on success, -1 on error
 */
ssize_t write_command(struct sp_port *port, const char *cmd);

/**
 * Reads exact number of bytes from serial port
 * Handles partial reads by continuing until all bytes are received
 * 
 * @param port The serial port
 * @param buffer The buffer to read data into
 * @param length The number of bytes to read
 * @return Number of bytes read on success, -1 on error, 0 on timeout
 */
ssize_t read_exact(struct sp_port *port, uint8_t *buffer, size_t length);

/**
 * Finds the binary header in the serial stream
 * Scans byte-by-byte looking for the header pattern (mask + points)
 * 
 * @param port The serial port
 * @param expected_mask The expected mask value (e.g., 135)
 * @param expected_points The expected points value (e.g., 101)
 * @return 1 if header found, 0 if timeout/not found, -1 on error
 */
int find_binary_header(struct sp_port *port, uint16_t expected_mask, uint16_t expected_points);

/**
 * Tests connection to NanoVNA by issuing info command
 * Sends "info" command and prints the response
 * 
 * @param port The serial port
 * @return 0 on success, 1 on error
 */
int test_connection(struct sp_port *port);

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
 * @param nbr_scans Total number of scans per VNA, determining number of data points to collect (101 dp per scan)
 * @param start Starting frequency in Hz
 * @param stop Stopping frequency in Hz
 * @param nbr_sweeps Number of frequency sweeps to perform
 * @param ports Array of serial port paths (e.g., ["/dev/ttyACM0", "/dev/ttyACM1"] on Linux, ["COM3", "COM4"] on Windows)
 */
void run_multithreaded_scan(int num_vnas, int nbr_scans, int start, int stop, int nbr_sweeps, const char **ports);

#endif