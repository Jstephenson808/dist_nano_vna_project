#ifndef VNACOMMUNICATION_H_
#define VNACOMMUNICATION_H_

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <inttypes.h>
#include <ctype.h>
#include <signal.h>

#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <unistd.h>
#include <dirent.h>

#define MAXIMUM_VNA_PORTS 10
#define MAXIMUM_VNA_PATH_LENGTH 25

/**
 * Fatal error handling. 
 * 
 * Calls teardown_port_array before allowing the program to exit normally.
 * Exists to try to ensure that program restores port settings even if ctrl+c interrupt used.
 * 
 * @param sig The signal number
 */
void fatal_error_signal(int sig);

/**
 * Opens a serial port and configures its settings
 * 
 * @param port The device path (e.g., "/dev/ttyACM0")
 * @param init_tty Memory location to store the initial settings of the port
 * @return File descriptor on success, -1 on failure
 */
int open_serial(const char *port, struct termios *init_tty);

/**
 * Configures serial port settings for NanoVNA communication
 * 
 * Sets up 115200 baud, 8N1, raw mode, no flow control
 * Saves original settings for later restoration:
 * Will not be restored automatically.
 * 
 * @param serial_port The file descriptor of the open serial port
 * @param initial_tty A memory location to store the initial settings 
 * @return 0 on success, another number otherwise.
 */
int configure_serial(int serial_port, struct termios *initial_tty);

/**
 * Restores serial port to original settings
 * 
 * @param fd The file descriptor of the serial port
 * @param settings The original termios settings to restore
 * 
 * @return EXIT_SUCCESS on success, errno on error
 */
int restore_serial(int fd, const struct termios *settings);

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
 * Tests connection to NanoVNA by issuing info command
 * Sends "info" command and checks answered by NanoVNA
 * 
 * @param fd The file descriptor of the serial port
 * @return 0 on success, 1 on error / not a VNA
 */
int test_vna(int fd);

/**
 * Checks if a VNA is already in the connections list
 *
 * @param vna_path a string representing the path to the VNA's serial port
 * @return 1 if in list, 0 if not in list
 */
int in_vna_list(const char* vna_path);

/**
 * Adds a path to a file representing a VNA connection to ports
 * 
 * Checks that path is a valid length, there is space in ports,
 * can be connected to, and represents a NanoVNA-H connection.
 * 
 * @param vna_path a string pointing to the NanoVNA connection file
 * @return 0 if successful, -1 if system error, 1-4 for invalid strings of different types.
 */
int add_vna(char* vna_path);

/**
 * Closes, restores and removes a VNA given its file path.
 * 
 * May reorder the ports array.
 * Will free relevant memory but otherwise leave data,
 * but keeps total_vnas accurate.
 * 
 * @param vna_path a string pointing to the NanoVNA connection file
 * @return 0 if successful, 1 if fails.
 */
int remove_vna_name(char* vna_path);

/**
 * Closes, restores and removes a VNA given its index in the arrays.
 * 
 * May reorder the ports array.
 * Will free relevant memory but otherwise leave data,
 * but keeps total_vnas accurate.
 * 
 * @param vna_num index of VNA in internal arrays.
 * @return 0 if successful, 1 if fails.
 */
int remove_vna_number(int vna_num);

/**
 * Finds new VNAs and puts them in paths list
 * 
 * @param paths a char* array of size MAXIMUM_VNA_PORTS to put found ports in
 * @param search_dir a string representing the directory in which to search (usually "/dev")
 * @return number of paths found (between 0 and MAXIMUM_VNA_PORTS)
 */
int find_vnas(char** paths, const char* search_dir);

/**
 * 
 */
void vna_id();

/**
 * 
 */
void vna_ping();

/**
 * 
 */
int vna_reset(const char* vna_port);

/**
 * 
 */
void vna_status();

/**
 * Assigns memory for and initialises port array
 * 
 * @param init_port path to the initial VNA, pass NULL if none.
 * @return 0 on success, 1 on failure.
 */
int initialise_port_array();

/**
 * Closes all ports, restores their initial settings, and frees port arrays.
 * 
 * We loop through the ports in reverse order to ensure that total_vnas is
 * always accurate and if a fatal error occurs a new call of teardown_port_array()
 * would not try to close an already-closed port
 */
void teardown_port_array();

#endif