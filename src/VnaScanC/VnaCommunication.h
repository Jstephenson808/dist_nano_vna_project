#ifndef VNACOMMUNICATION_H_
#define VNACOMMUNICATION_H_

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <inttypes.h>

#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <unistd.h>

#define MAXIMUM_VNA_PORTS 10
#define MAXIMUM_VNA_PATH_LENGTH 25

/**
 * Opens a serial port
 * 
 * @param port The device path (e.g., "/dev/ttyACM0")
 * @return File descriptor on success, -1 on failure
 */
int open_serial(const char *port);

/**
 * Configures serial port settings for NanoVNA communication
 * 
 * Sets up 115200 baud, 8N1, raw mode, no flow control
 * Saves original settings for later restoration:
 * Will not be restored automatically.
 * 
 * @param serial_port The file descriptor of the open serial port
 * @return The original termios settings to restore later
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
 * Finds new VNAs and puts them in paths list
 */
int find_vnas(char** paths);

/**
 *
 */
int initialise_port_array(const char* init_port);

#endif